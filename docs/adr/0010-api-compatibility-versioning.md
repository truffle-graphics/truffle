# ADR 0010: API Version Identity

- Status: Accepted (amended by #163)
- Date: 2026-06-03

## Context

Truffle exposes package versions through CMake metadata and needs a code-level
API version identity that consumers and tests can read at compile time.

## Decision

`truffle/base/version.hpp` provides version identity only:

- `ApiVersion { major, minor, patch }` is the canonical semantic version shape.
- `kApiVersion` is the current API identity at compile time.
- `pack_api_version()` and `kApiVersionPacked` provide deterministic numeric
  packing.

`truffle_api_version_tests` verifies this identity.

## Amendment (#163)

The compatibility (`is_api_compatible`) and deprecation-window
(`DeprecationWindow`, `is_symbol_available`, `is_symbol_deprecated`) helpers
were removed. Truffle has no consumers yet and keeps no backward-compatibility
or deprecation machinery. Compatibility and deprecation policy is decided at
the first release.

## Consequences

- Version identity stays available without speculative compatibility rules.
- ABI compatibility remains backend/toolchain dependent and is out of scope.
