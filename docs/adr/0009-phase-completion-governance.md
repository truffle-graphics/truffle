# ADR 0009: Phase Completion Governance

## Status

Accepted

## Context

Truffle now operates across a multi-phase roadmap where implementation, tests,
CI, and documentation evolve together. Prior sessions revealed status drift:
code may advance while roadmap and handoff state lag, or documentation may
suggest a later phase while earlier phase work is still incomplete.

Without explicit governance gates, phase completion claims become ambiguous and
delivery quality regresses.

## Decision

A phase is considered complete only when all phase gates pass:

1. Implementation gate: required code deliverables for the phase are merged.
2. Test gate: phase-specific tests and full regression suite pass.
3. CI gate: configured CI checks for touched backends/platforms pass.
4. Documentation gate: roadmap, handoff, and architecture/docs reflect the same
   status and decisions.
5. Risk gate: open risks are explicitly tracked with owners or resolved.

Additionally:

- No phase may be marked complete if a required roadmap deliverable is still a
  stub or deferred implementation in phase-owned code paths.
- A later phase may start in parallel only if it does not relabel earlier phase
  status as complete.
- Handoff updates are mandatory before AI-authored commits.

## Consequences

### Positive

- Phase completion becomes objectively auditable.
- Engineering and documentation state remain synchronized.
- Multi-backend work (Metal, Vulkan, future targets) can be tracked by explicit
  quality gates instead of implicit assumptions.

### Tradeoffs

- Additional process overhead per phase.
- More frequent documentation updates are required during active delivery.

### Neutral

- This ADR does not define individual phase technical content.
- This ADR defines completion policy only; technical scope remains in the
  roadmap and phase-specific ADRs.
