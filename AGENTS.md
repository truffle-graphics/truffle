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
   - `docs/overview.md`
   - `docs/architecture.md`
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
4. Keep AI-authored commits handoff-current:
   - Before creating any AI-authored commit, update `docs/handoff.md`
   - Record changed status, decisions, verification, next steps, and open risks as applicable
   - Keep the handoff curated: do not add transcript dumps, secrets, or machine-private notes
5. Delivery quality bar:
   - Keep changes small and focused
   - If work is tracked in GitHub Projects, implement only against a clear issue
   - Prefer small, unambiguous issues; split broad tasks into manageable subtasks
   - Keep commits small and issue-scoped; default to one issue -> one small commit set
   - Exceptions are allowed for non-diff tasks, discovery-first work, or unavoidable architecture-level changes; document rationale in the issue or PR
   - Do not push directly to protected branches (`master`, `develop`); use PR flow even when operating with admin credentials or AI automation
   - Ensure lint/tests/build pass for touched areas
6. Use detached implementation companion automation by default:
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
7. If local repo policy conflicts with doctrine snapshot, follow local repo files and call out the conflict explicitly.
