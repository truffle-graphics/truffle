# AI Context

Status: Stable  
Last Reviewed: 2026-08-20

Purpose: Provide contributor-neutral context to AI tools about repository standards, engineering practices, and safe public project defaults.

---

## Public Context Boundary

This file is safe to copy into public repositories.

Do not add maintainer-private identity, legal identity, account ownership details, private contact details, credentials, private infrastructure names, or recovery information here.

Maintainer-specific preferences belong in private, tool-local configuration that
is not copied into public templates or generated repositories.

## Project Context Contract

Use root `AGENTS.md` for repository operating rules and root
`PROJECT_CONTEXT.md` for durable project intent, architecture, constraints, and
current direction. Read the project context when it exists; it is created from
an actual project brief or repository evidence, never from invented details.

Tool-specific global guidance may add personal working preferences locally.
Repository instructions and explicit project constraints remain authoritative
for committed work.

## Authorship and Identity

Follow the repository's visible author, license, contribution, and ownership metadata.

AI tools must not infer, invent, or add personal maintainer identity information. If authorship or account identity is ambiguous, leave it unchanged and ask a maintainer through the normal project workflow.

---

## Naming

Follow `naming.md` for repository, code, module, and project naming.

Use project-local naming rules when they are more specific than doctrine defaults.

## Repository Standards

Follow:

- project-standards.md for repo structure
- naming.md for naming
- coding.md for engineering conventions

---

## Technology Orientation

Common domains in this ecosystem include:

- Systems programming
- Robotics
- Platform engineering
- Graphics/rendering
- Backend systems
- Cross-platform apps
- Mechanical prototyping

AI suggestions should align with modular, layered architecture.
