# Contributing

Truffle uses Git Flow with local-first validation, merge-commit history, and
signed commits. The local doctrine snapshot supplies defaults where the
repository-specific policy below does not override them.

## Branching Strategy

- `master` is the stable branch for established Truffle baselines.
- `develop` is the active integration branch.
- Create `feat/<topic>` for new work and `fix/<topic>` for corrections from
  `develop`.
- Open normal feature and fix pull requests into `develop`.
- Cut `release/<version>` from `develop` when preparing a stable release.
  Stabilize there, merge its PR into `master`, then merge the same release
  branch into `develop` before deleting it.
- Cut emergency `hotfix/<topic>` branches from `master`. Merge into `master`
  first, then back into `develop`; include the fix in any active release branch.
- Create signed `v*` release tags on the resulting stable `master` commit.
- Use normal merge commits to preserve feature and release history. Squash or
  rebase merges are exceptional choices requiring explicit justification and
  maintainer agreement, not the default. Do not rewrite old squash history.
- Do not push directly to protected branches, force-push them, or bypass their
  PR and signed-commit protections.

## Pull Requests

- Keep changes focused and prefer small reviewable pull requests.
- Target `develop` except for release/hotfix promotion into `master`.
- Update docs when public contracts, architecture, or workflow behavior changes.
- Record the relevant focused local checks before requesting merge. Do not
  require a full build or hosted matrix for unrelated or documentation changes.
- Merge only signed commits into protected branches.
- Use merge commits, not routine squash/rebase merges.

## Local-First Validation And Optional Hosted Work

Ordinary development runs locally: inspect the change, run the smallest relevant
checks, then commit and push. Use affected build targets and filtered tests for
behavior changes; do not rerun a complete build/test/package matrix merely
because a commit, push, or PR is being made. Documentation-only work needs no
C++ build. Backend support claims still require objective native evidence;
local-first is not permission to label untested platforms supported.

There are no automatic cloud companions or AI review requests. Do not install
cloud-agent credentials or setup workflows as part of normal contribution.
Issue [#131](https://github.com/truffle-graphics/truffle/issues/131) records the
deliberate retirement of that integration, not a pending credential repair.

Hosted GitHub Actions builds are separate from AI-agent usage, but are still
optional resource-consuming work. Build and Release use only
`workflow_dispatch`; PRs, integration pushes, and tag pushes do not request
them. Existing repository-defined workflows are administratively disabled to
also stop legacy triggers still present on `master`. No hosted status check is
required by branch protection.

When a maintainer explicitly requests cross-platform/native evidence, enable
only Build and dispatch it against the desired ref, normally `develop`:

```bash
gh workflow enable build.yml
gh workflow run build.yml --ref develop
```

Disable it again after the requested run completes:

```bash
gh workflow disable build.yml
```

Do not enable the retired companion/setup workflows. Do not dispatch against
old refs containing automatic triggers. For deliberately hosted release
publication, see [`docs/distribution.md`](docs/distribution.md).

This repository-local policy overrides the Doctrine snapshot's generic
required-CI and optional linear-history guidance. Signed commits, PR-only
protected branches, and protection against force pushes/deletion remain.

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

Truffle checks its layered CMake target graph with a lightweight local guard,
also available in the optional manual Build workflow. It rejects an undeclared
production-target dependency or a reverse-layer public-header import:

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
- Record focused local checks and any genuine evidence gaps; do not repeat a
  full build solely to stop, resume, or push.
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
- The local foundation path uses `cmake --preset dev`,
  `cmake --build --preset dev`, and `ctest --preset dev`. Configure only when
  needed and prefer `--target <affected-target>` and `-R <test-pattern>` over
  rebuilding or retesting unrelated areas.
- Run `cmake --build --preset dev --target truffle_format_check` when
  `clang-format` is available; use `truffle_format` to apply the repository
  formatting baseline.
- Keep module boundaries explicit and public contracts documented.
- Keep the default CMake build path reproducible and optional tooling
  non-blocking.
