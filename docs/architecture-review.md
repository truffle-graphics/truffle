# Architecture Review

## Purpose

This checklist prevents accidental coupling and mixed-responsibility files
before they become expensive refactors. It supplements the mechanically checked
target and public-header boundaries; it does not replace engineering judgment.

## Required Plan Note

For every plan that changes production code, record:

- **Owner:** the existing module or the proposed new module that owns the
  behavior and its lifecycle.
- **Allowed dependencies:** the lower-layer contracts the change consumes; do
  not name a higher layer as a convenience dependency.
- **Non-goals:** adjacent responsibilities intentionally left outside the
  change.
- **Public contract effect:** `none`, documented extension, or breaking change.

For internal-only work with no boundary change, say so explicitly. This keeps a
small implementation from silently becoming a policy or integration layer.

## Cohesion Review

Ask these questions for each changed production file, regardless of length:

1. Does this file have one clear owner and a coherent reason to change?
2. Does it combine policy, orchestration, and detailed subsystem behavior that
   have different owners or lifecycles?
3. Would another subsystem need to modify this file for a reason unrelated to
   its primary responsibility?
4. Does it reach around an existing interface instead of extending the owning
   module's contract?
5. Can tests name the behavior's owning module without pulling in an unrelated
   higher layer?

An affirmative answer to questions 2–4 is a cohesion-review candidate even for
a small file. A negative answer to all of them is stronger evidence of cohesion
than line count alone.

## Orchestrators

An orchestrator may select collaborators, sequence calls, translate an
operation-level request into owned interface calls, and manage the transition
between lifecycle states. It must not become the detailed implementation of
those collaborators, own their low-level data representation, or hide their
policies. Extract behavior when it needs independent tests, evolves on a
different cadence, or belongs to the subsystem that owns the underlying data.

## Source-Length Signal

The architecture guard's default 1,000-line report threshold is intentionally
non-blocking. It makes a changed file visible for a cohesion review; it does not
classify a file as a God file and does not mandate a split. The threshold may be
adjusted as the project gathers evidence.

When a candidate is reviewed, record one of these outcomes in the PR, issue, or
AI handoff when one is created:

- **Cohesive:** state the single owner and why the responsibilities share a
  lifecycle.
- **Refactor required:** create a narrowly scoped issue with the intended
  boundary and a safe migration path.
- **Deferred deliberately:** document the reason, the risk, and the condition
  that will trigger the refactor.

## Mechanical Guard

Run this before requesting review:

```bash
python3 tools/check_architecture.py --report
```

The command fails when a production target links a forbidden Truffle target or
a production source/public header imports a forbidden public module. Update the
guard only alongside an intentional, reviewed architecture decision.
