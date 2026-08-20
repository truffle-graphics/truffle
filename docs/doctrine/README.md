# Doctrine Snapshot

This folder contains a local doctrine snapshot copied from the Doctrine repository.

## Source of Truth

The canonical source remains the Doctrine repository.

## Refresh

Run bootstrap again to add missing baseline files. Use `--refresh-baseline` to
refresh only this Doctrine-owned snapshot without replacing local agent or
project-context files.

## Export Boundary

This snapshot contains the public doctrine baseline only.
Identity-specific files and maintainer-local overlays are intentionally not copied by default.
