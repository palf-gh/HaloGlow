# HaloGlow Agent Guide

## Scope

HaloGlow is a standalone, MIT-licensed After Effects SDK glow effect. It must
reference only the Adobe SDK Examples tree and files inside `HaloGlow`; never
add dependencies on `Palf_Plugins` or any other workspace project.

The normative design is `docs/HaloGlow_仕様書.md` (Japanese). Read the chapter
that covers the code you touch before changing it, and update the specification
in the same change when behaviour, parameters or guarantees move.

## Local Files

- `research/` is git-ignored and holds local design notes, verification
  scripts and test images that are not redistributable. Keep it out of commits
  and do not link to it from product files.

## Branch Policy

- `main` is the release branch; `develop` is the integration branch.
- Cut feature and fix branches from `develop`; do not commit directly to `main`
  without explicit user approval.
- Do not rewrite history, force-push, squash, or rebase unless explicitly asked.

## Compatibility Rules

- `PF_ParamDef.uu.id` values in `HG_ParamId` are persisted project data (spec
  §18.2). Never renumber or reuse an ID; append new IDs before `HG_NUM_PARAMS`.
- `MATCHNAME`, PiPL identifiers and the hidden `HG_ALGORITHM_VERSION` contract
  are frozen once released. Coefficient-table changes need a new algorithm
  version; old versions keep rendering with their own tables.

## Colour-management Contract (spec §2.2, §3.12)

- Process the working RGB the host supplies. Never guess and decode a transfer
  function inside the effect.
- Never call AEGP from render selectors (`SMART_PRE_RENDER`, `SMART_RENDER`,
  GPU render). The Luminance Space Auto cache is refreshed only from main-thread
  UI commands and published as an immutable snapshot.
- Do not serialise renders behind a global lock.

## GPU Parity

- CUDA (Windows), OpenCL (Windows) and Metal (macOS) share `Source/HG_Core.h`
  definitions. Keep the host upload struct and every kernel's parameter struct
  in the same field order.
- The CPU backend is the FP32 reference for CPU/GPU tolerance tests (§12.5).

## Build And Validation

- Windows development build: `cmake -S . -B build\Win` then
  `cmake --build build\Win --config Debug`.
- Run `ctest --test-dir build\Win -C Debug` after core changes; the test
  executable keeps asserts enabled in every configuration.
- Record every agent-authored project-change session in
  `../.agents/production-record.md`.
