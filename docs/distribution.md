# Distribution Guide

This guide captures the current package and release path for Truffle.

## Local Package Generation

Use a package-focused build when you want a quick distribution smoke test.

```sh
cmake -S . -B build/package-smoke \
  -DTRUFFLE_INSTALL=ON \
  -DTRUFFLE_BUILD_TESTS=OFF \
  -DTRUFFLE_BUILD_EXAMPLES=OFF \
  -DTRUFFLE_BUILD_BACKEND_VULKAN=ON \
  -DTRUFFLE_BUILD_BACKEND_OPENGL=ON
cmake --build build/package-smoke
cmake --install build/package-smoke --prefix build/package-smoke/install
(cd build/package-smoke && cpack --verbose)
```

Expected output archive: `Truffle-<version>-<platform>.tar.gz`.

## Consumer Verification

The install smoke gate is available in test presets when `TRUFFLE_INSTALL=ON`:

```sh
ctest --preset dev -R truffle_package_consumer --output-on-failure
```

This verifies:

- package config export availability through `find_package(Truffle CONFIG REQUIRED)`
- target linkability for exported Truffle targets
- end-to-end consumer configure/build success

## Optional Hosted Packaging

Local package generation is the default. Hosted workflows are manual-only and
administratively disabled; pushing a PR, branch, or tag does not request a
build. See `CONTRIBUTING.md` before explicitly enabling and running Build.

The optional `build.yml` run includes package and parity-report automation that:

- configures install-enabled builds
- builds and installs Truffle
- runs CPack
- uploads generated package artifacts
- emits backend parity report artifacts from the CI test build
  (`parity-matrix.md`,
  `parity-matrix.json`, and `rhi-parity-report.json`)

## Manual Tagged Release Flow

Prepare a release through the Git Flow release/hotfix PRs described in
`CONTRIBUTING.md`, then create and push a signed `v*` tag on stable `master`.
The tag push does not build packages or publish a GitHub Release.

For explicitly requested hosted publication, temporarily enable Release and
dispatch against that existing signed tag, not a branch:

```sh
gh workflow enable release.yml
gh workflow run release.yml --ref vX.Y.Z
```

The tag must contain the manual-only workflow definition; older tags are not
supported by this dispatch path. The workflow rejects branch refs before
building. Disable Release again after the requested run completes:

```sh
gh workflow disable release.yml
```

An explicitly dispatched `release.yml` run:

- builds release package artifacts and parity-report test targets
- runs package generation
- attaches package archives to GitHub Releases
- publishes parity matrix Markdown/JSON and live RHI parity JSON artifacts with
  release outputs

## Backend Packaging Notes

- `Truffle::BackendNull` is the baseline exported backend.
- `Truffle::Assets`, `Truffle::AssetRender`, and `Truffle::Diagnostics` are
  exported high-level opt-in targets. Consumers link them only when they need
  asset declarations, metadata-only asset-to-render planning, or
  diagnostics/tooling helpers. Diagnostics links asset-render planning so tools
  can summarize planned layouts without making render or scene depend on
  diagnostics.
- `Truffle::BackendMetal`, `Truffle::BackendVulkan`, and
  `Truffle::BackendOpenGL` are exported when enabled at configure time.
- `Truffle::BackendDirect3D` is exported when enabled at configure time.
- Enabled Metal, Vulkan, OpenGL, OpenGL ES, and Direct3D targets call their
  native graphics APIs on the repository's proven host lanes. Backend-platform
  maturity and individual capabilities remain authoritative: unproven platform
  rows stay `source_only`, and unsupported operations fail explicitly.
- RHI 1 packaging will acquire dependencies only for enabled backends and will
  publish maturity per backend-platform pair rather than one aggregate support
  claim.
