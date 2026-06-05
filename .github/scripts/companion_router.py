#!/usr/bin/env python3
"""Route Truffle implementation changes to detached Copilot companion tasks."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Iterable


COMPANION_BRANCH_MARKERS = (
    "copilot/",
    "github-copilot/",
    "copilot-swe-agent/",
    "companion/",
)


@dataclass(frozen=True)
class Lane:
    key: str
    title: str
    instructions: str


LANES = {
    "tests": Lane(
        key="tests",
        title="Add focused tests",
        instructions=(
            "Add or update focused tests for behavior/API changes in the source "
            "branch. Own tests/ and test CMake wiring only unless a compile "
            "issue proves the implementation contract is inconsistent."
        ),
    ),
    "examples": Lane(
        key="examples",
        title="Update consumer examples",
        instructions=(
            "Update examples only for public or consumer-facing usage changes. "
            "Own examples/ and example CMake wiring only, and avoid duplicating "
            "test coverage in sample code."
        ),
    ),
    "docs": Lane(
        key="docs",
        title="Update docs and handoff",
        instructions=(
            "Update docs/handoff.md and directly relevant public docs for "
            "architecture, workflow, public contract, release, distribution, or "
            "resume-critical AI handoff changes."
        ),
    ),
}


def run_git(args: list[str]) -> str:
    result = subprocess.run(
        ["git", *args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def changed_files(base_sha: str, head_sha: str) -> list[str]:
    output = run_git(["diff", "--name-only", f"{base_sha}...{head_sha}"])
    return [line.strip() for line in output.splitlines() if line.strip()]


def is_companion_branch(branch: str) -> bool:
    return branch.startswith(COMPANION_BRANCH_MARKERS) or "companion-" in branch


def is_test_file(path: str) -> bool:
    return path.startswith("tests/")


def is_example_file(path: str) -> bool:
    return path.startswith("examples/")


def is_doc_file(path: str) -> bool:
    return (
        path.startswith("docs/")
        or path
        in {
            "AGENTS.md",
            "AI_CONTEXT.md",
            "CONTRIBUTING.md",
            "README.md",
        }
    )


def is_durable_doc_file(path: str) -> bool:
    return is_doc_file(path) and path != "docs/handoff.md"


def is_workflow_or_automation(path: str) -> bool:
    return (
        path.startswith(".github/workflows/")
        or path.startswith(".github/scripts/")
    )


def is_build_file(path: str) -> bool:
    return (
        path in {"CMakeLists.txt", "CMakePresets.json"}
        or path.startswith("cmake/")
        or (
            path.endswith("/CMakeLists.txt")
            and not path.startswith(("examples/", "tests/"))
        )
    )


def is_implementation_file(path: str) -> bool:
    return path.startswith("include/") or path.startswith("src/") or is_build_file(path)


def is_public_usage_surface(path: str) -> bool:
    return (
        path.startswith("include/")
        or path.startswith("src/backends/")
        or is_build_file(path)
        or path in {"README.md", "CONTRIBUTING.md"}
    )


def route_lanes(files: Iterable[str]) -> list[str]:
    paths = list(files)
    if not paths:
        return []

    has_tests = any(is_test_file(path) for path in paths)
    has_examples = any(is_example_file(path) for path in paths)
    has_durable_docs = any(is_durable_doc_file(path) for path in paths)
    implementation_changed = any(is_implementation_file(path) for path in paths)
    public_usage_changed = any(is_public_usage_surface(path) for path in paths)
    workflow_changed = any(is_workflow_or_automation(path) for path in paths)

    lanes: list[str] = []
    if implementation_changed and not has_tests:
        lanes.append("tests")
    if public_usage_changed and not has_examples:
        lanes.append("examples")
    if (public_usage_changed or workflow_changed) and not has_durable_docs:
        lanes.append("docs")
    return lanes


def self_test() -> None:
    assert route_lanes(["include/truffle/rhi/rhi.hpp"]) == ["tests", "examples", "docs"]
    assert route_lanes(["include/truffle/rhi/rhi.hpp", "docs/handoff.md"]) == [
        "tests",
        "examples",
        "docs",
    ]
    assert route_lanes(["include/truffle/rhi/rhi.hpp", "docs/architecture.md"]) == [
        "tests",
        "examples",
    ]
    assert route_lanes(["src/render/renderer.cpp"]) == ["tests"]
    assert route_lanes(["src/render/renderer.cpp", "tests/render_flow_tests.cpp"]) == []
    assert route_lanes(["tests/new_tests.cpp", "tests/CMakeLists.txt"]) == []
    assert route_lanes(["examples/host_workspace/src/main.cpp", "examples/CMakeLists.txt"]) == []
    assert route_lanes(["src/backends/metal/metal_backend.mm", "tests/metal_backend_tests.cpp"]) == [
        "examples",
        "docs",
    ]
    assert route_lanes([".github/workflows/build.yml"]) == ["docs"]
    assert route_lanes([".github/workflows/build.yml", "AGENTS.md"]) == []
    assert route_lanes(["docs/handoff.md"]) == []
    assert is_companion_branch("copilot/add-tests")
    assert is_companion_branch("feature/companion-tests")


def github_request(token: str, method: str, path: str, payload: dict | None = None) -> dict:
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    request = urllib.request.Request(
        f"https://api.github.com{path}",
        data=data,
        method=method,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        details = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GitHub API {method} {path} failed: {error.code} {details}") from error
    if not body:
        return {}
    return json.loads(body)


def create_agent_task(
    token: str,
    repo: str,
    branch: str,
    head_sha: str,
    lane: Lane,
    files: list[str],
    source_url: str,
) -> str:
    changed_list = "\n".join(f"- `{path}`" for path in files[:80])
    if len(files) > 80:
        changed_list += f"\n- ...and {len(files) - 80} more"

    prompt = f"""Detached Truffle companion task.

Source branch: `{branch}`
Source head: `{head_sha}`
Source event: {source_url or "n/a"}
Companion lane: `{lane.key}`

## Task

{lane.instructions}

Create a pull request targeting `{branch}`. Do not target `develop` or `master`.
Keep the diff lane-owned and minimal. If no companion change is needed, explain
why in the task log instead of making speculative edits.

## Changed files

{changed_list}
"""
    payload = {
        "prompt": prompt,
        "base_ref": branch,
        "create_pull_request": True,
    }
    response = github_request(token, "POST", f"/agents/repos/{repo}/tasks", payload)
    if not response:
        raise RuntimeError("Copilot agent-task API returned an empty response")
    return str(
        response.get("html_url")
        or response.get("url")
        or response.get("id")
        or response.get("state")
        or "(task URL unavailable)"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", help="owner/repo")
    parser.add_argument("--base-ref", help="source branch for companion PRs")
    parser.add_argument("--base-sha", help="diff base SHA")
    parser.add_argument("--head-sha", help="diff head SHA")
    parser.add_argument("--source-url", default="", help="workflow, PR, or commit URL")
    parser.add_argument("--dry-run", action="store_true", help="print planned tasks without API calls")
    parser.add_argument("--self-test", action="store_true", help="run router classification checks")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        print("companion_router self-test passed")
        return 0

    missing = [
        name
        for name in ("repo", "base_ref", "base_sha", "head_sha")
        if getattr(args, name) is None
    ]
    if missing:
        joined = ", ".join(f"--{name.replace('_', '-')}" for name in missing)
        raise SystemExit(f"missing required arguments: {joined}")

    files = changed_files(args.base_sha, args.head_sha)
    lanes = route_lanes(files)

    print(f"Changed files: {len(files)}")
    for path in files:
        print(f"- {path}")

    if args.base_ref in {"develop", "master"} or is_companion_branch(args.base_ref):
        print(f"Skipping branch {args.base_ref!r} to avoid protected-branch or companion loops.")
        return 0

    if not lanes:
        print("No companion lanes needed.")
        return 0

    print("Companion lanes needed: " + ", ".join(lanes))
    if args.dry_run:
        return 0

    token = os.environ.get("COPILOT_AGENT_TOKEN", "").strip()
    if not token:
        print(
            "::warning::COPILOT_AGENT_TOKEN is not configured; "
            "companion Copilot tasks were planned but not started."
        )
        return 0

    for lane_key in lanes:
        task_url = create_agent_task(
            token=token,
            repo=args.repo,
            branch=args.base_ref,
            head_sha=args.head_sha,
            lane=LANES[lane_key],
            files=files,
            source_url=args.source_url,
        )
        print(f"Created companion agent task for {lane_key}: {task_url}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
