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

## Stop And Resume

Before switching machines or stopping active work:

- Update `docs/handoff.md` with the current status, verification, and next steps.
- Promote durable decisions from the handoff into project docs or ADRs.
- Run the relevant build and test checks.
- Commit and push the branch together with the handoff update.

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
