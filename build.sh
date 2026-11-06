#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    echo "Usage: ./build.sh [debug|release|relwithdebinfo|package] [--warnings-as-errors on|off] [CMake build arguments]"
    echo "Example: ./build.sh debug --warnings-as-errors on --target truffle_rhi_contract_tests --parallel 4"
    echo "Warnings default to off in a new build; the selected build remembers the setting."
    exit 0
fi
preset=debug
if (( $# )) && [[ "$1" != -* ]]; then preset="$1"; shift; fi
configure_args=(--preset "$preset")
build_args=(--build --preset "$preset")
while (( $# )); do
    case "$1" in
        --warnings-as-errors)
            if (( $# < 2 )); then
                echo "--warnings-as-errors requires on or off." >&2; exit 2
            fi
            case "$2" in
                on|ON) configure_args+=("-DTFL_WARNINGS_AS_ERRORS=ON") ;;
                off|OFF) configure_args+=("-DTFL_WARNINGS_AS_ERRORS=OFF") ;;
                *) echo "--warnings-as-errors requires on or off, not '$2'." >&2; exit 2 ;;
            esac
            shift 2 ;;
        --) shift; build_args+=("$@"); break ;;
        *) build_args+=("$1"); shift ;;
    esac
done
for tool in cmake ninja; do
    command -v "$tool" >/dev/null || { echo "Missing $tool. Run ./setup.sh first." >&2; exit 1; }
done
cmake "${configure_args[@]}"
cmake "${build_args[@]}"
