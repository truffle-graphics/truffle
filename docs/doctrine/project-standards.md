# Project Standards

Status: Stable  
Last Reviewed: 2026-08-20

## Purpose

Defines structural standards applied to project repositories.
These standards reduce decision fatigue and ensure consistency across projects.

These are not suggestions — they are defaults unless a project has a strong reason to differ.

---

## Repository Layout

### Default Structure
    /docs  
    /src  
    /include (C/C++ projects)  
    /tests  
    /examples  
    /tools  
    README.md  
    LICENSE  

Not all folders must exist, but structure should remain predictable.

### Language-Specific Notes

**C/C++**
- Use `/src` and `/include`
- Use CMake as build system

**Python**
- Use package-style layout
- Separate runtime code from scripts

**Flutter/Web**
- Follow ecosystem conventions while keeping docs consistent.

---

## Setup Friction Minimization

Repositories should minimize setup work for first-time contributors and users.

Defaults:

- Keep clone-to-first-build steps short and explicit.
- For non-trivial prerequisites, provide setup scripts under `tools/setup/`.
- Prefer a doctor/validation path before install automation.
- Classify dependencies as required vs optional.
- Ensure optional tooling never blocks the default build/run path.
- Document setup checks and fallback commands in README.

---

## README Standard

Every repository README must answer:

1. What this project is
2. Why it exists
3. What problem it solves
4. How to build/run
5. How to use
6. Current status
7. High-level architecture (if applicable)

README is the entry point, not full documentation.

---

## Documentation Standard

Documentation lives in `/docs`.

Add documentation when:

- Architecture is non-trivial
- Public usage is expected
- System has multiple modules
- Design decisions matter long term

Typical docs:

- Architecture overview
- Module structure
- Data flow
- Build system explanation
- API usage
- Design decisions

### Project Context

Active or long-lived repositories should keep a concise root
`PROJECT_CONTEXT.md`. It is the durable re-entry point for maintainers and AI
agents, covering:

- Project intent and non-goals
- Current maturity and next meaningful direction
- Architecture, constraints, and decisions that must not be changed casually
- Relevant validation and deeper documentation

Create the document from a real project brief or repository evidence. Keep it
specific to the project; do not fill it with generic doctrine or invented
claims. Use `templates/project-context-template.md` as a starting structure.

---

## Demo / Proof Requirement

Projects should include proof of functionality when possible:

- Screenshots
- CLI output examples
- Videos (robotics/hardware)
- Render output (graphics)
- Benchmarks (if performance related)

This is not marketing — it proves existence.

---

## Naming Within Repositories

Follow `naming.md`.

Consistency across:

- Modules
- Packages
- Binaries
- Config files

---

## Quality Gate

Before considering a repo shareable:

- Builds successfully
- README present
- Basic documentation exists
- Example usage available
- Repo structure consistent

---

## Deviations

Deviations are allowed when:

- Ecosystem tooling requires it
- Experimentation requires it
- Simplicity is improved

But deviation should be intentional.
