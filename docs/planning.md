# Durable Planning And Work Tracking

## Purpose

Truffle keeps accepted work portable across maintainers, contributors,
automation, and development machines. Repository issues and the
[Truffle delivery project](https://github.com/orgs/truffle-graphics/projects/1) are
the durable execution record. Local notes, conversations, and the rolling
handoff may provide working context, but they are not sufficient as the only
record of an accepted plan.

This document is contributor-neutral and public-safe. Tracker content must not
contain credentials, private infrastructure, personal notes, transcripts,
machine-specific secrets, or material that is unsuitable for a public
repository.

## When Tracking Is Required

Create or update a repository issue when any of these becomes accepted work:

- a feature, fix, refactor, documentation change, test, release task, or
  operational repository change;
- an implementation plan or a separately deliverable plan step;
- a discovered gap, risk, or follow-up that will require later work;
- an architectural or product decision that implies implementation;
- work deliberately deferred because dependencies, hardware, access, evidence,
  or priority prevent immediate implementation.

Exploration and brainstorming do not require an issue while options are still
being evaluated. Once an outcome is accepted for implementation or deliberate
deferral, record it before implementation continues or the planning session
ends. Rejected alternatives belong in the deciding issue or ADR when the
rationale is important; they should not become misleading open work.

## Hierarchy

Use the smallest hierarchy that remains understandable:

- **Epic:** a module-owned backend, platform, or capability group containing
  multiple independently deliverable results. Epics are top-level; a module
  may have several distinct epics.
- **Implementation issue:** a focused, reviewable result with one owning module
  and a clear validation boundary.

Epics organize work. Implementation begins from an implementation
issue, not directly from a broad epic. Use GitHub's parent/sub-issue feature so
the hierarchy is machine-readable and visible in the Project. A textual parent
reference may aid reading, but does not replace the structural relationship.

Use a short ownership prefix in issue titles, such as `RHI:`, `Shader:`,
`Renderer:`, `GUI:`, `Base:`, `CI:` or `Docs:`. Identify the module and,
when applicable, the parent epic at the start of an open issue's body.
The Module Project field records ownership; the epic/sub-issue hierarchy
records the area within that module. The completed core RHI foundation (#25)
is history, not a parent for ongoing module work.

Apply the `epic` label to organizing epics, including those awaiting their first
sub-issues. This keeps the overview independent of whether decomposition has
started.

Split an issue when it contains independently testable outcomes, different
owners, incompatible platform evidence, or multiple changes that should be
reviewed separately. An architecture-level issue may remain larger only when
its shared contract genuinely cannot be reviewed or validated in smaller
units; record that rationale.

## Required Issue Content

Every implementation or accepted-deferred issue records:

1. **Context:** why the work exists and the verified current state.
2. **Required outcome:** the observable result, not only intended activity.
3. **Scope:** the owned changes required for that outcome.
4. **Out of scope:** adjacent behavior that must not be absorbed.
5. **Architecture boundary:** owning module, allowed dependencies, and public
   contract effect.
6. **Acceptance criteria:** objective, checkable completion conditions.
7. **Validation and evidence:** tests, builds, native output, platform runs,
   package checks, documentation, or operational readback required to close.
8. **Dependencies and sequencing:** prerequisites, followers, and safe parallel
   work.
9. **Deferral record, when applicable:** reason, current risk, and a concrete
   unblock or review condition.

Unknown implementation details should be marked as questions or discovery
work, not invented. If discovery changes the plan materially, update or split
the issue before implementation continues.

## GitHub Project Workflow

All accepted Truffle issues belong in the delivery project. Use its fields as
follows:

- **Status:** `Backlog` for accepted work not queued, `Next` for deliberately
  selected focus, `In Progress` for active work, `In Review` for work awaiting
  pull-request review, and `Done` after the final disposition is recorded.
- **Module:** `Base`, `RHI`, `Shader`, `Renderer`, `GUI`, `GUI Dock`, `EnCom`,
  `Tooling & CI` or `Docs & Process`.
- **Priority:** `P0` critical/next, `P1` required, `P2` planned, or `P3`
  opportunistic.
- **Size:** `S`, `M`, `L`, or `XL` as a planning estimate rather than a
  promise.
- **Target evidence:** `Contract`, `Cross-compiles`, `Native smoke`,
  `Conformant`, `Supported` or `Release`, according to the intended evidence gate.

There are no Phase or Workstream fields, fixed dates, or iterations. Use `Next`
to choose focus without implying a calendar commitment. Project views are
different presentations of the same items, not separate backlogs.

The saved views provide complementary entry points:

| View | Layout and purpose | Filter |
| --- | --- | --- |
| Work board | Priority-sorted Status board with Module slicing for all unfinished work | `-status:Done` |
| By module | Module-grouped table with expandable hierarchy | None |
| Epics | Module-grouped overview with sub-issue progress | `label:epic` |
| Backlog | Module-grouped, priority-sorted flat planning table | `status:Backlog` |
| Done | Module-grouped completion history | `status:Done` |

Use the `deferred` label for accepted work intentionally scheduled after the
current priorities. Deferred does not mean forgotten or accepted as complete.
The issue remains open until implemented, explicitly removed from scope, or
superseded with a linked rationale.

Update Project and issue state when work starts, a pull request opens, a
dependency blocks progress, scope changes, evidence arrives, or the final
disposition changes. Pull requests link the implementation issue and describe
the validation boundary. Closing automation is useful, but the acceptance
criteria and evidence remain the authority.

## Roadmaps, Decisions, And Handoffs

- `docs/roadmap.md` summarizes sequencing and links durable epics and work
  groups; issue bodies hold the executable detail.
- ADRs and architecture documents hold durable decisions and constraints;
  deciding issues link them to implementation.
- `docs/handoff.md` records current execution state and immediate resumption
  guidance; it does not replace the issue backlog.

When stopping or switching environments, ensure that accepted new work,
changed scope, blockers, evidence, and next steps have been reflected in the
issue and Project. A handoff may then point to those records instead of copying
an entire plan.

## Completion Audit

An epic or phase closes only when:

- every required child issue is complete or has an explicit disposition
  compatible with the phase exit criteria;
- implementation, tests, CI, documentation, and risk gates pass;
- Project status, issue state, roadmap, support claims, and handoff agree;
- evidence links remain sufficient for another contributor to verify the claim.

RHI 1 additionally follows `docs/adr/0009-phase-completion-governance.md` and
the backend maturity gates in `docs/rhi1/support-matrix.md`.
