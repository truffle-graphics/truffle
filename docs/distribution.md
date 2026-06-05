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

## CI Packaging

`build.yml` includes a package job that:

- configures install-enabled builds
- builds and installs Truffle
- runs CPack
- uploads generated package artifacts
- emits backend parity report artifacts

## Tagged Release Flow

`release.yml` handles tag-driven release publication:

- builds and tests release artifacts
- runs package generation
- attaches package archives to GitHub Releases
- publishes parity matrix artifacts with release outputs

## Backend Packaging Notes

- `Truffle::BackendNull` is the baseline exported backend.
- `Truffle::Assets`, `Truffle::AssetRender`, and `Truffle::Diagnostics` are
  exported high-level opt-in targets. Consumers link them only when they need
  asset declarations, metadata-only asset-to-render planning, or
  diagnostics/tooling helpers. Diagnostics links assets so bundle reports can
  include catalog metadata without making render or scene depend on diagnostics.
- `Truffle::BackendMetal`, `Truffle::BackendVulkan`, and
  `Truffle::BackendOpenGL` are exported when enabled at configure time.
- `Truffle::BackendDirect3D` is exported when enabled at configure time.
