# ADR 0002: Render Data Ingestion Lanes

## Status

Accepted

## Context

The bootstrap renderer already proves ECS component extraction. That path is
useful for normal scene workflows, but dense simulation workloads can contain
millions of detections or instances. Rebuilding renderer-owned objects by
walking every high-level entity each frame can dominate the cost of rendering.

## Decision

Renderer-facing design is data-oriented and not strictly ECS-centered. Truffle
will support ECS extraction as a first-party scene integration lane and preserve
room for later bulk, streaming, and GPU-resident render-data lanes.

The foundation pass records this boundary before introducing new high-volume
public APIs.

## Consequences

- Future renderer APIs must not require Truffle EnCom adoption.
- ECS adapters may optimize extraction, but they cannot be the only path into
  renderer-facing data.
- Later render-data and frame orchestration work must evaluate dense LiDAR,
  radar, and simulation-style datasets as performance drivers.
