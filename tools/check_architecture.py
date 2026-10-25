#!/usr/bin/env python3
"""Check Truffle's production target and public-header dependency direction."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "src"
INCLUDE_ROOT = ROOT / "include" / "truffle"

DIRECT_DEPENDENCIES = {
    "truffle_core": set(),
    "truffle_assets": {"truffle_core"},
    "truffle_ecs": {"truffle_core"},
    "truffle_rhi": {"truffle_core"},
    "truffle_render": {"truffle_rhi"},
    "truffle_asset_render": {"truffle_assets", "truffle_render"},
    "truffle_scene": {"truffle_ecs", "truffle_render"},
    "truffle_diagnostics": {
        "truffle_asset_render",
        "truffle_assets",
        "truffle_render",
    },
}

PUBLIC_MODULE_TARGETS = {
    "core": "truffle_core",
    "assets": "truffle_assets",
    "ecs": "truffle_ecs",
    "rhi": "truffle_rhi",
    "render": "truffle_render",
    "asset_render": "truffle_asset_render",
    "scene": "truffle_scene",
    "diagnostics": "truffle_diagnostics",
}


def remove_cmake_comments(text: str) -> str:
    return re.sub(r"(?m)#.*$", "", text)


def cmake_calls(text: str, command: str) -> list[str]:
    calls: list[str] = []
    pattern = re.compile(rf"\b{re.escape(command)}\s*\(")
    for match in pattern.finditer(text):
        depth = 1
        index = match.end()
        while index < len(text) and depth:
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
            index += 1
        if depth:
            raise ValueError(f"unterminated {command} call")
        calls.append(text[match.end() : index - 1])
    return calls


def cmake_tokens(call: str) -> list[str]:
    return re.findall(r'"[^"\n]*"|\$<[^>]*>|\$\{[^}]*\}|[^\s()]+', call)


def allowed_dependencies(target: str) -> set[str] | None:
    if target.startswith("truffle_backend_"):
        return {"truffle_rhi"}
    return DIRECT_DEPENDENCIES.get(target)


def permitted_public_imports(target: str) -> set[str] | None:
    direct = allowed_dependencies(target)
    if direct is None:
        return None
    permitted = {target}
    pending = list(direct)
    while pending:
        dependency = pending.pop()
        if dependency in permitted:
            continue
        permitted.add(dependency)
        transitive = allowed_dependencies(dependency)
        if transitive is None:
            return None
        pending.extend(transitive)
    return permitted


def target_for_source(path: Path) -> str | None:
    try:
        relative = path.relative_to(SOURCE_ROOT)
    except ValueError:
        relative = None
    if relative is not None:
        top = relative.parts[0]
        if top == "backends":
            return "truffle_backend_"
        return PUBLIC_MODULE_TARGETS.get(top)

    try:
        relative = path.relative_to(INCLUDE_ROOT)
    except ValueError:
        return None
    return PUBLIC_MODULE_TARGETS.get(relative.parts[0])


def check_target_dependencies(errors: list[str]) -> tuple[int, int]:
    declared = 0
    checked = 0
    for path in sorted(SOURCE_ROOT.rglob("CMakeLists.txt")):
        text = remove_cmake_comments(path.read_text(encoding="utf-8"))
        for call in cmake_calls(text, "add_library"):
            tokens = cmake_tokens(call)
            if not tokens or not tokens[0].startswith("truffle_"):
                continue
            target = tokens[0]
            if allowed_dependencies(target) is None:
                errors.append(
                    f"{path.relative_to(ROOT)}: unknown production target {target}; "
                    "add its reviewed dependency rule to tools/check_architecture.py"
                )
            declared += 1
        for call in cmake_calls(text, "target_link_libraries"):
            tokens = cmake_tokens(call)
            if not tokens:
                continue
            target = tokens[0]
            if not target.startswith("truffle_"):
                continue
            allowed = allowed_dependencies(target)
            if allowed is None:
                errors.append(
                    f"{path.relative_to(ROOT)}: unknown production target {target}; "
                    "add its reviewed dependency rule to tools/check_architecture.py"
                )
                continue
            dependencies = {token for token in tokens[1:] if token.startswith("truffle_")}
            forbidden = sorted(dependencies - allowed)
            if forbidden:
                errors.append(
                    f"{path.relative_to(ROOT)}: {target} may not link "
                    f"{', '.join(forbidden)}"
                )
            checked += 1
    return declared, checked


INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"]truffle/([^/]+)/', re.MULTILINE)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm"}


def check_public_imports(errors: list[str]) -> int:
    checked = 0
    roots = (SOURCE_ROOT, INCLUDE_ROOT)
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            target = target_for_source(path)
            if target is None:
                errors.append(
                    f"{path.relative_to(ROOT)}: unknown production source location; "
                    "add its reviewed ownership rule to tools/check_architecture.py"
                )
                continue
            permitted = permitted_public_imports(target)
            if permitted is None:
                errors.append(f"{path.relative_to(ROOT)}: unknown production target {target}")
                continue
            text = path.read_text(encoding="utf-8")
            for public_module in INCLUDE_PATTERN.findall(text):
                imported = PUBLIC_MODULE_TARGETS.get(public_module)
                if imported is not None and imported not in permitted:
                    errors.append(
                        f"{path.relative_to(ROOT)}: {target} may not import public "
                        f"module truffle/{public_module}"
                    )
                checked += 1
    return checked


def report_large_files(threshold: int) -> None:
    candidates: list[tuple[int, Path]] = []
    for root in (SOURCE_ROOT, INCLUDE_ROOT):
        for path in root.rglob("*"):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            lines = len(path.read_text(encoding="utf-8").splitlines())
            if lines >= threshold:
                candidates.append((lines, path))

    if not candidates:
        print(f"No source-length cohesion-review candidates at {threshold} lines.")
        return

    print(f"Cohesion-review candidates at {threshold} lines (informational only):")
    for lines, path in sorted(candidates, reverse=True):
        print(f"  {path.relative_to(ROOT)}: {lines} lines")
    print("Review ownership and responsibility cohesion; this is not a God-file verdict.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--report",
        action="store_true",
        help="report source-length cohesion candidates without failing",
    )
    parser.add_argument(
        "--review-line-threshold",
        type=int,
        default=1000,
        help="informational source-length candidate threshold (default: 1000)",
    )
    args = parser.parse_args()
    if args.review_line_threshold <= 0:
        parser.error("--review-line-threshold must be positive")

    errors: list[str] = []
    target_declarations, target_checks = check_target_dependencies(errors)
    include_checks = check_public_imports(errors)

    if errors:
        print("Architecture guard failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(
        "Architecture guard passed: "
        f"{target_declarations} production targets, "
        f"{target_checks} target dependency declarations, and "
        f"{include_checks} public-header imports checked."
    )
    if args.report:
        report_large_files(args.review_line_threshold)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
