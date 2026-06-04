# AGENTS.md

This repository follows the Doctrine baseline. Truffle's local repository docs
govern project-specific scope and workflow; the Doctrine snapshot provides
ecosystem defaults.

## Agent Operating Rules

1. Read repository guidance first:
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
6. If local repo policy conflicts with doctrine snapshot, follow local repo files and call out the conflict explicitly.
