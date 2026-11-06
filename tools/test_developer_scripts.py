#!/usr/bin/env python3
"""Exercise native entry points with isolated tools; never install or fetch."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPOSITORY = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]


class DeveloperScripts(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="truffle-script-tests-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.repo = self.root / "repo with spaces"
        self.repo.mkdir()
        for name in ("setup.sh", "build.sh"):
            shutil.copy2(REPOSITORY / name, self.repo / name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        for name in ("bash", "dirname", "awk"):
            (self.bin / name).symlink_to(shutil.which(name))
        self.log = self.root / "calls.jsonl"
        self.environment = dict(os.environ, PATH=str(self.bin), SCRIPT_TEST_LOG=str(self.log))
        stub = f"""#!{sys.executable}
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['SCRIPT_TEST_LOG'], 'a') as log:
    log.write(json.dumps([name, args, os.getcwd()]) + '\\n')
if name == 'uname':
    print(os.environ.get('SCRIPT_TEST_OS', 'Darwin') if args == ['-s'] else 'arm64')
elif name == 'git':
    if args[:2] == ['ls-files', '--stage']:
        print('160000 ' + 'a' * 40 + ' 0\\t' + args[-1])
    elif args[-2:] == ['rev-parse', 'HEAD']:
        print(os.environ.get('SCRIPT_TEST_HEAD', 'a' * 40))
    elif args[-2:] == ['status', '--porcelain']:
        print(os.environ.get('SCRIPT_TEST_DIRTY', ''), end='')
elif name == 'dpkg-query':
    print('install ok installed', end='')
elif name == 'cmake':
    if args and args[0] == '--preset':
        sys.exit(int(os.environ.get('SCRIPT_TEST_CONFIGURE_EXIT', '0')))
    if args and args[0] == '--build':
        sys.exit(int(os.environ.get('SCRIPT_TEST_BUILD_EXIT', '0')))
"""
        for name in ("uname", "git", "cmake", "ninja", "xcrun", "brew", "sudo", "apt-get", "dpkg-query"):
            tool = self.bin / name
            tool.write_text(stub)
            tool.chmod(0o755)
        self.paths = [
            "vendor/vulkan-headers", "vendor/volk", "vendor/glslang",
            "vendor/freetype", "vendor/harfbuzz", "vendor/sheenbidi", "vendor/utf8proc",
            "external/truffle", "external/SDL", "external/occt",
            "external/truffle/vendor/freetype", "external/truffle/vendor/harfbuzz",
            "external/truffle/vendor/sheenbidi", "external/truffle/vendor/utf8proc",
        ]
        for path in self.paths:
            marker = self.repo / path / ".git"
            marker.parent.mkdir(parents=True)
            marker.touch()

    def run_script(self, name, *args):
        return subprocess.run(
            [str(self.repo / name), *args], cwd=self.root, env=self.environment,
            text=True, capture_output=True, timeout=10,
        )

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_help(self):
        for name in ("setup.sh", "build.sh"):
            self.assertEqual(self.run_script(name, "--help").returncode, 0)

    def test_read_only_check(self):
        result = self.run_script("setup.sh", "--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(any(name in ("brew", "sudo") or args[:2] == ["submodule", "update"] for name, args, _ in self.calls()))

    def test_missing_tool_never_installs_during_check(self):
        (self.bin / "ninja").unlink()
        result = self.run_script("setup.sh", "--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ninja", result.stderr)
        self.assertFalse(any(name == "brew" for name, _, _ in self.calls()))

    def test_installation_requires_confirmation(self):
        (self.bin / "ninja").unlink()
        result = subprocess.run(
            [str(self.repo / "setup.sh")], cwd=self.root, env=self.environment,
            input="n\n", text=True, capture_output=True, timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Installation declined", result.stderr)
        self.assertFalse(any(name == "brew" for name, _, _ in self.calls()))

    def test_linux_platform_gate(self):
        self.environment["SCRIPT_TEST_OS"] = "Linux"
        result = self.run_script("setup.sh", "--check")
        if "Linux)" in (REPOSITORY / "setup.sh").read_text():
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Apple Silicon", result.stderr)

    def test_missing_submodules_are_explicit_and_nonrecursive(self):
        for path in self.paths:
            (self.repo / path / ".git").unlink()
        result = self.run_script("setup.sh")
        self.assertEqual(result.returncode, 0, result.stderr)
        updates = [args for name, args, _ in self.calls() if name == "git" and args[:2] == ["submodule", "update"]]
        self.assertTrue(updates)
        self.assertTrue(all(args[:4] == ["submodule", "update", "--init", "--"] for args in updates))

    def test_dirty_submodule_is_not_moved(self):
        self.environment.update(SCRIPT_TEST_HEAD="b" * 40, SCRIPT_TEST_DIRTY=" M file\n")
        result = self.run_script("setup.sh")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("dirty submodule", result.stderr)
        self.assertFalse(any(args[:2] == ["submodule", "update"] for _, args, _ in self.calls()))

    def test_build_arguments_and_location(self):
        result = self.run_script("build.sh", "debug", "--target", "some_target", "--parallel", "2")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = [entry for entry in self.calls() if entry[0] == "cmake"]
        self.assertEqual(calls[0][1], ["--preset", "debug"])
        self.assertEqual(calls[1][1], ["--build", "--preset", "debug", "--target", "some_target", "--parallel", "2"])
        self.assertTrue(all(Path(entry[2]).samefile(self.repo) for entry in calls))

    def test_default_is_debug_without_a_warning_override(self):
        self.assertEqual(self.run_script("build.sh").returncode, 0)
        calls = [entry for entry in self.calls() if entry[0] == "cmake"]
        self.assertEqual(calls[0][1], ["--preset", "debug"])

    def test_warnings_toggle_uses_the_same_preset(self):
        option = "STUDIO_WARNINGS_AS_ERRORS" if REPOSITORY.name == "truffle-studio" else "TFL_WARNINGS_AS_ERRORS"
        for value in ("on", "off"):
            result = self.run_script("build.sh", "release", "--warnings-as-errors", value, "--parallel", "2")
            self.assertEqual(result.returncode, 0, result.stderr)
        calls = [entry for entry in self.calls() if entry[0] == "cmake"]
        self.assertEqual(calls[0][1], ["--preset", "release", f"-D{option}=ON"])
        self.assertEqual(calls[2][1], ["--preset", "release", f"-D{option}=OFF"])
        self.assertEqual(calls[1][1], ["--build", "--preset", "release", "--parallel", "2"])
        self.assertEqual(calls[3][1], calls[1][1])

    def test_warning_option_without_an_explicit_preset(self):
        result = self.run_script("build.sh", "--warnings-as-errors", "on")
        self.assertEqual(result.returncode, 0, result.stderr)
        configure = [args for name, args, _ in self.calls() if name == "cmake"][0]
        self.assertEqual(configure[:2], ["--preset", "debug"])
        self.assertTrue(configure[2].endswith("_WARNINGS_AS_ERRORS=ON"))

    def test_invalid_warning_setting_never_configures(self):
        for arguments in (("--warnings-as-errors",), ("--warnings-as-errors", "maybe")):
            result = self.run_script("build.sh", *arguments)
            self.assertEqual(result.returncode, 2)
            self.assertIn("requires on or off", result.stderr)
        self.assertFalse(any(name == "cmake" for name, _, _ in self.calls()))

    def test_standard_presets_do_not_encode_warning_policy(self):
        presets = json.loads((REPOSITORY / "CMakePresets.json").read_text())
        configurations = {preset["name"]: preset for preset in presets["configurePresets"]}
        expected = {"debug": "Debug", "release": "Release", "relwithdebinfo": "RelWithDebInfo"}
        self.assertTrue(set(expected).issubset(configurations))
        self.assertFalse({"dev", "ci", "strict"} & configurations.keys())
        for name, build_type in expected.items():
            self.assertEqual(configurations[name]["cacheVariables"]["CMAKE_BUILD_TYPE"], build_type)
        for preset in configurations.values():
            self.assertFalse(any(option.endswith("_WARNINGS_AS_ERRORS") for option in preset.get("cacheVariables", {})))

    def test_configure_failure_stops_build(self):
        self.environment["SCRIPT_TEST_CONFIGURE_EXIT"] = "7"
        self.assertEqual(self.run_script("build.sh").returncode, 7)
        self.assertFalse(any(args[:1] == ["--build"] for _, args, _ in self.calls()))

    def test_build_failure_propagates(self):
        self.environment["SCRIPT_TEST_BUILD_EXIT"] = "8"
        self.assertEqual(self.run_script("build.sh").returncode, 8)


if __name__ == "__main__":
    unittest.main()
