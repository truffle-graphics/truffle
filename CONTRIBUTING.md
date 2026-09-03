# Contributing

Truffle follows the local doctrine snapshot for branch workflow, commit
messages, and signed repository history.

## Branching Strategy

- `master` is the stable branch for established Truffle baselines.
- `develop` is the active integration branch.
- Use `feat/<topic>` for new work.
- Use `fix/<topic>` for corrections.
- Open normal feature and fix pull requests into `develop`.
- Merge `develop` into `master` only when the integrated code is stable enough
  to become the next established baseline.
- Do not use direct pushes for normal work on protected branches.

## Pull Requests

- Keep changes focused and prefer small reviewable pull requests.
- Target `develop` unless the pull request is a stable promotion into `master`.
- Update docs when public contracts, architecture, or workflow behavior changes.
- Run the relevant build and test checks before requesting merge.
- Merge only signed commits into protected branches.

## Durable Planning And Traceability

Accepted work must be recoverable from the public repository and GitHub
tracker. A local planning document, chat, or handoff is not the only record of
work that the project has decided to implement or deliberately defer.

Before implementation begins:

1. Create or refine a focused repository issue.
2. Add it to the [Truffle delivery project](https://github.com/users/loosewired/projects/5)
   and the applicable milestone.
3. Link it as a real sub-issue when it belongs to a roadmap epic.
4. Record the context, required outcome, scope, non-goals, owning module,
   permitted dependencies, public-contract effect, dependencies, acceptance
   criteria, and required validation or native evidence.
5. If the work is deferred, keep it open and record why it is deferred and what
   condition makes it actionable.

Broad roadmap issues organize work; they are not implementation branches.
Production changes should normally use one focused issue and one reviewable
commit set. Update the issue and Project status when scope, sequencing,
evidence, or disposition changes. Close an issue only when its acceptance
criteria are satisfied or its final disposition is explicit.

Durable architectural and product decisions belong in project documentation or
an ADR linked from the issue. Brainstorming that has not been accepted as work
does not need an issue. See [`docs/planning.md`](docs/planning.md) for the full
planning contract.

## AI Planning And Delivery Authorization

Before an AI implements an approved plan, the user must choose a delivery mode:

- **Review-first (default):** implement and validate the plan, then leave the
  changes uncommitted for review.
- **Commit-and-push:** commit the approved work and immediately push the same
  branch to its configured upstream.

An implementation request alone does not authorize a commit. When
commit-and-push delivery is requested, state and agree the proposed commit
structure before implementation: either one cohesive commit or named,
independently validated phases. Each phase must have a clear scope and
validation boundary; do not create empty checkpoint commits merely to mirror a
plan.

Every AI-authored commit has an immediate paired push. There is no AI-authored
"commit only" delivery mode. If a push fails, report the delivery as incomplete
and retry through an approved, configured access path before making another
commit, switching branches, or claiming the work is available from another
machine. Do not substitute a different remote without explicit approval.

Protected branches remain pull-request-only; this policy permits pushes only to
the approved non-protected implementation branch.

## Architecture And Cohesion Review

Truffle protects its layered CMake target graph in CI. The guard rejects an
undeclared production-target dependency or a reverse-layer public-header import;
run it locally with:

```bash
python3 tools/check_architecture.py --report
```

This mechanical guard is necessary but not sufficient. Review every production
code change for one clear owner, a coherent reason to change, and dependencies
that follow the architecture documented in `docs/architecture.md`.
`docs/architecture-review.md` is the required checklist for AI-assisted plans
and reviews.

Source length is only a review signal. The default 1,000-line report threshold
identifies candidates for cohesion review; it does not label a file a God file
or require an automatic split. A smaller file is equally a candidate when it
mixes unrelated policy, orchestration, and implementation that should have
separate owners. Resolve a candidate by recording why its responsibilities
belong together or by creating a focused refactor issue.

## Stop And Resume

Before switching machines or stopping active work:

- Update `docs/handoff.md` with the current status, verification, and next steps.
- Promote durable decisions from the handoff into project docs or ADRs.
- Run the relevant build and test checks.
- For commit-and-push delivery, commit and push the branch together with the
  handoff update, following the authorization and retry rules above.

## Commit Message Format

Use Doctrine commit format:

- `<type>(<scope>): <summary>`
- `(<scope>)` is optional.

Allowed `type` values:

- `feat`
- `fix`
- `docs`
- `chore`
- `refactor`
- `test`
- `ci`
- `build`
- `perf`
- `revert`

Rules:

- Use imperative mood.
- Keep subjects concise and prefer 72 characters or fewer.
- Do not end the subject with a period.
- For breaking changes, use `!` and include `BREAKING CHANGE:` in the body.

Examples:

- `feat(rhi): add buffer mapping contract`
- `fix(ecs): reject stale entity mutation`
- `docs(workflow): clarify protected branch rules`

## Authorship And Signing

- Use each contributor's own public commit identity.
- Keep maintainer-private identity and account details out of public repository
  guidance.
- All local commits must be signed.
- Protected branch commits must be signed.
- Configure Git signing before creating repository history intended to be kept.

## Code Quality

- Follow `.editorconfig`.
- Use `cmake --preset dev`, `cmake --build --preset dev`, and
  `ctest --preset dev` for the default local foundation check path.
- Run `cmake --build --preset dev --target truffle_format_check` when
  `clang-format` is available; use `truffle_format` to apply the repository
  formatting baseline.
- Keep module boundaries explicit and public contracts documented.
- Keep the default CMake build path reproducible and optional tooling
  non-blocking.
