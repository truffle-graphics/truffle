#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
check=false
case "${1:-}" in
    --check) check=true ;;
    --help|-h)
        echo "Usage: ./setup.sh [--check]"
        echo "Prepare the standard presets; --check verifies without installing or fetching."
        exit 0 ;;
    "") ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
esac
(( $# <= 1 )) || { echo "Too many arguments." >&2; exit 2; }

confirm() {
    local answer
    if ! read -r -p "$1 [y/N] " answer; then
        echo "Setup needs confirmation. Rerun in a terminal or install the tools manually." >&2
        return 1
    fi
    if [[ "$answer" != y && "$answer" != Y ]]; then
        echo "Installation declined; no dependencies were installed." >&2
        return 1
    fi
}
case "$(uname -s)" in
    Darwin)
        if ! xcrun --find clang++ >/dev/null 2>&1; then
            if $check; then echo "Missing Xcode command-line tools." >&2; exit 1; fi
            confirm "Open Apple's installer for Xcode command-line tools?" || exit 1
            xcode-select --install
            echo "Finish Apple's installation, then rerun ./setup.sh."
            exit 1
        fi
        packages=()
        for tool in git cmake ninja; do
            command -v "$tool" >/dev/null || packages+=("$tool")
        done
        if (( ${#packages[@]} )); then
            if $check; then echo "Missing tools: ${packages[*]}" >&2; exit 1; fi
            command -v brew >/dev/null || {
                echo "Install Homebrew from https://brew.sh, or install ${packages[*]} manually, then rerun." >&2
                exit 1
            }
            confirm "Install missing tools with: brew install ${packages[*]}?" || exit 1
            brew install "${packages[@]}"
        fi ;;
    Linux)
        command -v apt-get >/dev/null && command -v dpkg-query >/dev/null || {
            echo "Automatic setup supports Debian/Ubuntu Linux. Install CMake 3.23+, Ninja, Git, a C++20 compiler and GLFW X11/Wayland development dependencies manually on other distributions." >&2
            exit 1
        }
        packages=()
        for package in build-essential cmake ninja-build git libwayland-dev \
            libxcb1-dev libx11-dev libxcursor-dev libxext-dev libxi-dev \
            libxinerama-dev libxkbcommon-dev libxrandr-dev wayland-protocols \
            libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev; do
            if [[ "$(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true)" != "install ok installed" ]]; then
                packages+=("$package")
            fi
        done
        if (( ${#packages[@]} )); then
            if $check; then echo "Missing packages: ${packages[*]}" >&2; exit 1; fi
            confirm "Install missing packages with apt-get: ${packages[*]}?" || exit 1
            sudo apt-get update
            sudo apt-get install "${packages[@]}"
        fi ;;
    *) echo "Use setup.ps1 on Windows; this shell setup supports macOS and Debian/Ubuntu." >&2; exit 1 ;;
esac

for tool in git cmake ninja; do
    command -v "$tool" >/dev/null || { echo "Still missing $tool." >&2; exit 1; }
done
cmake --list-presets >/dev/null
prepare_submodule() {
    local path="$1" expected actual
    expected="$(git ls-files --stage -- "$path" | awk '$1 == "160000" {print $2}')"
    [[ -n "$expected" ]] || { echo "No pinned submodule at $path." >&2; exit 1; }
    actual=""
    if [[ -e "$path/.git" ]]; then actual="$(git -C "$path" rev-parse HEAD)"; fi
    if [[ "$actual" == "$expected" ]]; then return; fi
    if $check; then echo "Missing or out-of-date submodule: $path. Run ./setup.sh." >&2; exit 1; fi
    if [[ -n "$actual" ]]; then
        local dirty
        dirty="$(git -C "$path" status --porcelain)"
        if [[ -n "$dirty" ]]; then
            echo "Refusing to move dirty submodule $path. Preserve its changes before rerunning setup." >&2
            exit 1
        fi
    fi
    git submodule update --init -- "$path"
}
for path in vendor/vulkan-headers vendor/volk vendor/glslang \
    vendor/freetype vendor/harfbuzz vendor/sheenbidi vendor/utf8proc; do
    prepare_submodule "$path"
done
echo "Setup ready. Run ./build.sh (debug), ./build.sh release, or ./build.sh relwithdebinfo."
