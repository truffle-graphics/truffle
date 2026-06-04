# ADR 0010: API Compatibility And Versioning Baseline

- Status: Accepted
- Date: 2026-06-03

## Context

Phase 6 requires explicit and testable compatibility boundaries for public
contracts. Truffle previously exposed package versions through CMake metadata,
but did not provide a code-level compatibility contract that consumers and tests
could evaluate at runtime or compile time.

## Decision

Truffle defines a public API version contract in `truffle/core/version.hpp`:

- `ApiVersion { major, minor, patch }` is the canonical semantic version shape.
- `kApiVersion` is the current API identity at compile time.
- `pack_api_version()` provides deterministic numeric packing for telemetry and
  gate checks.
- `is_api_compatible(requested, provided)` adopts same-major and
  forward-minor compatibility (`requested.minor <= provided.minor`).
- `DeprecationWindow` plus `is_symbol_available()` / `is_symbol_deprecated()`
  define deterministic deprecation and removal windows by minor version.

Compatibility tests are mandatory in CI via `truffle_api_version_tests`.

## Consequences

Positive:

- Consumers can assert compatibility boundaries without parsing build metadata.
- CI can gate accidental compatibility regressions early.
- Phase 6 API stabilization has a concrete baseline instead of policy-only text.

Tradeoffs:

- Compatibility semantics are intentionally conservative and may need future
  refinement for feature-flagged interfaces.
- ABI compatibility remains backend/toolchain dependent and is not fully solved
  by semantic API version checks alone.

## Follow-up

- Add compatibility docs in consumer-facing integration guides.
- Introduce explicit deprecation windows tied to minor releases.
- Add ABI audit checks when stable shared-library distribution becomes a
  requirement.
