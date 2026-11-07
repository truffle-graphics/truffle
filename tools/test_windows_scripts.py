#!/usr/bin/env python3
"""Exercise PowerShell entry points without installing tools or fetching sources."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == "nt", "Windows PowerShell entry points")
class WindowsScripts(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="truffle-windows-tests-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.repo = self.root / "repo with spaces"
        (self.repo / "scripts").mkdir(parents=True)
        for name in ("setup.ps1", "build.ps1"):
            shutil.copy2(REPOSITORY / name, self.repo / name)
        self.log = self.root / "calls.jsonl"
        self.environment = dict(
            os.environ, SCRIPT_TEST_LOG=str(self.log),
            SCRIPT_TEST_ROOT=str(self.root), SCRIPT_TEST_REPO=str(self.repo),
        )
        (self.repo / "scripts" / "WindowsToolchain.ps1").write_text("""
function Initialize-WindowsToolchain {
    function global:ninja {}
    function global:cmake {
        Add-Content $env:SCRIPT_TEST_LOG (ConvertTo-Json -Compress -InputObject @{
            arguments = @($args); location = (Get-Location).Path
        })
        $global:LASTEXITCODE = 0
        if ($args[0] -eq '--preset') {
            $global:LASTEXITCODE = [int]$env:SCRIPT_TEST_CONFIGURE_EXIT
        } elseif ($args[0] -eq '--build') {
            $global:LASTEXITCODE = [int]$env:SCRIPT_TEST_BUILD_EXIT
        }
    }
}
""")
        self.wrapper = self.root / "run.ps1"
        self.wrapper.write_text("""
$ErrorActionPreference = 'Stop'
${env:ProgramFiles(x86)} = $env:SCRIPT_TEST_ROOT
function global:cl.exe {}
function global:git {
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'ls-files') {
        '160000 ' + ('a' * 40) + " 0`t" + $args[-1]
    } elseif ($args[-1] -eq 'HEAD') {
        if ($env:SCRIPT_TEST_WRONG_PIN) { 'b' * 40 } else { 'a' * 40 }
    } elseif ($args[-1] -eq '--porcelain') {
        $env:SCRIPT_TEST_DIRTY
    } elseif ($args[0] -eq 'submodule') {
        throw 'Tests must never update submodules.'
    }
}
function global:winget { throw 'Tests must never install tools.' }
""")
        for path in (
            "vulkan-headers", "volk", "glslang", "freetype",
            "harfbuzz", "sheenbidi", "utf8proc",
        ):
            marker = self.repo / "vendor" / path / ".git"
            marker.parent.mkdir(parents=True)
            marker.touch()

    def run_script(self, script, *arguments):
        values = " ".join(
            value if value.startswith("-") else "'" + value.replace("'", "''") + "'"
            for value in arguments
        )
        command = (
            f". '{self.wrapper}'; & '{self.repo / script}' {values}; "
            "exit $LASTEXITCODE"
        )
        return subprocess.run(
            ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-Command", command],
            cwd=self.root, env=self.environment, text=True,
            capture_output=True, timeout=30,
        )

    def calls(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_setup_finds_tools_from_developer_environment(self):
        result = self.run_script("setup.ps1", "-Check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls()[0]["arguments"], ["--list-presets"])

    def test_setup_check_rejects_wrong_pin(self):
        self.environment["SCRIPT_TEST_WRONG_PIN"] = "1"
        result = self.run_script("setup.ps1", "-Check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("out-of-date submodule", result.stderr)

    def test_setup_preserves_dirty_submodule(self):
        self.environment.update(SCRIPT_TEST_WRONG_PIN="1", SCRIPT_TEST_DIRTY=" M file")
        result = self.run_script("setup.ps1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("dirty submodule", result.stderr)

    def test_build_forwards_arguments_from_repository(self):
        result = self.run_script(
            "build.ps1", "debug", "-WarningsAsErrors", "on", "--parallel", "2",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual(calls[0]["arguments"],
                         ["--preset", "debug", "-DTFL_WARNINGS_AS_ERRORS=ON"])
        self.assertEqual(calls[1]["arguments"],
                         ["--build", "--preset", "debug", "--parallel", "2"])
        self.assertTrue(all(Path(call["location"]).samefile(self.repo) for call in calls))

    def test_configure_failure_stops_build(self):
        self.environment["SCRIPT_TEST_CONFIGURE_EXIT"] = "7"
        result = self.run_script("build.ps1")
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertEqual(len(self.calls()), 1)

    def test_build_failure_propagates(self):
        self.environment["SCRIPT_TEST_BUILD_EXIT"] = "8"
        self.assertEqual(self.run_script("build.ps1").returncode, 8)


if __name__ == "__main__":
    unittest.main()
