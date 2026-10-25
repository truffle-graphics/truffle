"""Offline credential diagnostics and routing checks; no network or secrets."""

import contextlib
import io
import os
import unittest
import urllib.error
from unittest.mock import patch

import companion_router as router


class CompanionRouterTests(unittest.TestCase):
    def test_classification(self):
        router.self_test()

    def test_credential_check_only_reads_identity_and_repository(self):
        with patch.object(
            router, "github_request",
            side_effect=[{"login": "synthetic"}, {"permissions": {"push": True}}],
        ) as request:
            router.verify_credentials("synthetic-token", "owner/repo")
        self.assertEqual(
            request.call_args_list,
            [
                unittest.mock.call("synthetic-token", "GET", "/user"),
                unittest.mock.call("synthetic-token", "GET", "/repos/owner/repo"),
            ],
        )

    def test_valid_identity_without_write_access_fails(self):
        with patch.object(
            router, "github_request",
            side_effect=[{"login": "synthetic"}, {"permissions": {"push": False}}],
        ):
            with self.assertRaisesRegex(RuntimeError, "lacks repository write access"):
                router.verify_credentials("synthetic-token", "owner/repo")

    def test_auth_failure_has_actionable_diagnostic_without_token(self):
        error = urllib.error.HTTPError(
            "https://api.github.com/user", 401, "Unauthorized", {}, io.BytesIO(b"{}"),
        )
        with patch.object(router.urllib.request, "urlopen", side_effect=error):
            with self.assertRaisesRegex(RuntimeError, "Replace the repository Actions secret") as raised:
                router.github_request("synthetic-token", "GET", "/user")
        self.assertNotIn("synthetic-token", str(raised.exception))

    def test_permission_failure_does_not_claim_bad_credentials(self):
        error = urllib.error.HTTPError(
            "https://api.github.com/agents/repos/owner/repo/tasks",
            403, "Forbidden", {}, io.BytesIO(b'{"message":"Forbidden"}'),
        )
        with patch.object(router.urllib.request, "urlopen", side_effect=error):
            with self.assertRaisesRegex(RuntimeError, "coding-agent entitlement"):
                router.github_request("synthetic-token", "POST", "/agents/repos/owner/repo/tasks")

    def test_verify_auth_mode_cannot_dispatch_tasks(self):
        with (
            patch("sys.argv", ["router", "--verify-auth", "--repo", "owner/repo"]),
            patch.dict(os.environ, {"COPILOT_AGENT_TOKEN": "synthetic-token"}),
            patch.object(router, "verify_credentials") as verify,
            patch.object(router, "create_agent_task") as create,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(router.main(), 0)
        verify.assert_called_once_with("synthetic-token", "owner/repo")
        create.assert_not_called()

    def test_missing_auth_secret_fails_manual_check(self):
        with (
            patch("sys.argv", ["router", "--verify-auth", "--repo", "owner/repo"]),
            patch.dict(os.environ, {}, clear=True),
        ):
            with self.assertRaisesRegex(RuntimeError, "not configured"):
                router.main()

    def test_failed_preflight_prevents_dispatch(self):
        with (
            patch("sys.argv", [
                "router", "--repo", "owner/repo", "--base-ref", "feat/example",
                "--base-sha", "base", "--head-sha", "head",
            ]),
            patch.dict(os.environ, {"COPILOT_AGENT_TOKEN": "synthetic-token"}),
            patch.object(router, "changed_files", return_value=["src/example.cpp"]),
            patch.object(router, "verify_credentials", side_effect=RuntimeError("HTTP 401")),
            patch.object(router, "create_agent_task") as create,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            with self.assertRaisesRegex(RuntimeError, "HTTP 401"):
                router.main()
        create.assert_not_called()

    def test_dry_run_does_not_check_credentials_or_dispatch(self):
        with (
            patch("sys.argv", [
                "router", "--repo", "owner/repo", "--base-ref", "feat/example",
                "--base-sha", "base", "--head-sha", "head", "--dry-run",
            ]),
            patch.object(router, "changed_files", return_value=["src/example.cpp"]),
            patch.object(router, "verify_credentials") as verify,
            patch.object(router, "create_agent_task") as create,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(router.main(), 0)
        verify.assert_not_called()
        create.assert_not_called()


if __name__ == "__main__":
    unittest.main()
