# AGENTS.md

Truffle uses portable repository guidance. Local project documents govern
project-specific scope and workflow; the Doctrine snapshot provides
contributor-neutral defaults.

## Agent Operating Rules

1. Read repository guidance first:
   - `PROJECT_CONTEXT.md`
   - `AI_CONTEXT.md`
   - `README.md`
   - `CONTRIBUTING.md`
   - `docs/handoff.md`
   - `docs/planning.md`
   - `docs/overview.md`
   - `docs/architecture.md`
   - `docs/architecture-review.md`
   - `docs/doctrine/README.md`
   - `docs/doctrine/*.md`
2. Prefer doctrine defaults:
   - Clarity over cleverness
   - Explicit interfaces and low coupling
   - Stable and maintainable structure
3. Keep governance and docs in sync:
   - Update docs when behavior or architecture changes
   - Preserve repository visibility rules
   - Avoid secrets in commits and docs
4. Make accepted work durable before implementation:
   - Treat local notes, conversations, and temporary plans as transient. Once
     work is accepted for implementation or deliberate deferral, create or
     update a repository issue before implementation continues or the planning
     session ends.
   - Add accepted issues to the Truffle delivery project and the applicable
     milestone. A broad roadmap issue is an epic, not an implementation unit;
     decompose it into focused sub-issues before production work begins.
   - Every implementation issue records context, required outcome, scope,
     non-goals, architecture ownership and allowed dependencies, public-contract
     effect, dependencies, acceptance criteria, and required validation or
     native evidence. Record a deferral reason and unblock condition when work
     cannot proceed yet.
   - Keep real GitHub parent/sub-issue relationships, Project status, roadmap,
     and issue state synchronized. Plain-text `Parent: #...` references are not
     a substitute for the structural relationship.
   - Record durable architectural or product decisions in repository docs or an
     ADR and link the deciding issue. Unaccepted brainstorming does not require
     a backlog item.
   - Follow `docs/planning.md` for the complete public planning contract.
5. Keep AI-authored commits handoff-current:
   - Before creating any AI-authored commit, update `docs/handoff.md`
   - Record changed status, decisions, verification, next steps, and open risks as applicable
   - Keep the handoff curated: do not add transcript dumps, secrets, or machine-private notes
6. Establish delivery policy before implementing a plan:
   - Before implementing a plan, ask the user to select either review-first
     delivery (leave all changes uncommitted) or commit-and-push delivery.
     Never infer commit authorization from an implementation request alone.
   - When offering commit-and-push delivery, recommend a commit topology that
     matches the approved plan: one cohesive commit, or explicitly named,
     independently validated phases. State the proposed contents and validation
     boundary of every commit before implementation begins.
   - If the user selects staged delivery, commit and push each approved phase
     before starting the next one. Do not create empty checkpoint commits just
     to match a plan step.
   - A user authorization to commit also authorizes the required paired push;
     there is no AI-authored "commit only" delivery mode. Push the same branch
     to its configured upstream immediately after every AI-authored commit.
   - If a push fails, report delivery as incomplete and retry through an
     approved, configured access path. Do not make another commit, switch
     branches, or claim the work is available on another machine until the
     push succeeds. Do not substitute another remote without explicit approval.
7. Preserve architecture and responsibility boundaries:
   - For every implementation plan that changes production code, state the
     owning module, permitted dependencies, non-goals, and whether a public
     contract changes. State "no architectural boundary change" when that is
     genuinely the case.
   - Follow `docs/architecture-review.md` during planning and review. A change
     may not add a reverse-layer dependency or make an optional higher layer a
     runtime dependency of a lower layer.
   - Treat file length as a review signal only, never as a God-file verdict.
     A file at or above the documented review threshold is a cohesion-review
     candidate; so is any smaller file that combines responsibilities with
     different owners, lifecycles, or reasons to change.
   - Orchestrators may compose owned interfaces, sequencing, and lifecycle
     transitions, but must not absorb the detailed implementation or policy of
     the subsystems they coordinate. Extract that behavior to its owning module.
   - Resolve a cohesion-review candidate by recording why its responsibilities
     remain cohesive or by creating a tightly scoped refactor issue. Do not
     silently grow a mixed-responsibility file.
8. Delivery quality bar:
   - Keep changes small and focused
   - If work is tracked in GitHub Projects, implement only against a clear issue
   - Prefer small, unambiguous issues; split broad tasks into manageable subtasks
   - Keep commits small and issue-scoped; default to one issue -> one small commit set
   - Exceptions are allowed for non-diff tasks, discovery-first work, or unavoidable architecture-level changes; document rationale in the issue or PR
   - Do not push directly to protected branches (`master`, `develop`); use PR flow even when operating with admin credentials or AI automation
   - Ensure lint/tests/build pass for touched areas
9. Use detached implementation companion automation by default:
   - Feature, fix, refactor, and public contract work may be followed by
     background companion work instead of blocking the implementation session.
   - `.github/workflows/companion-automation.yml` classifies same-repository PR
     diffs targeting protected integration branches, then starts lane-specific
     Copilot cloud-agent tasks when configured with a `COPILOT_AGENT_TOKEN` user
     token.
   - Companion tasks must target the source branch as their base branch and open
     separate pull requests; they must not push directly to `develop` or `master`.
   - Keep lane instructions in `.github/scripts/companion_router.py` so detached
     Copilot tasks have one authoritative prompt source.
   - Tests are expected for behavior or API contract changes.
   - Examples are expected only when public or consumer-facing usage changes; do
     not churn examples for internal-only edits.
   - Docs/handoff updates are expected when architecture, workflow, public
     contracts, release/distribution behavior, or commit-ready AI handoff state
     changes.
   - GitHub Actions remains the detached validation gate.
10. If local repo policy conflicts with doctrine snapshot, follow local repo files and call out the conflict explicitly.
