# Contributing to FOH Gauge

## Module ownership
| Area | Owner | Paths |
|---|---|---|
| DSP core + tests | **Orion** | `core/`, `tests/` |
| JUCE UI + guides | **Lyra** | `app/` (Source, Resources) |
| Integration, CI, build system | **Vega** (integration) | `.github/`, top-level `CMakeLists.txt`, `README.md`, this file |

Changes outside your area are fine when needed, but call them out in the PR description and tag the owner.
`core/` must stay JUCE-free (it is built and tested on Linux without JUCE).

## Worktrees
Each engineer works in their own git worktree of the one repo at `/workspace/foh-gauge`:
- Orion: `/workspace/foh-gauge-orion`
- Lyra: `/workspace/foh-gauge-lyra`
```
git -C /workspace/foh-gauge worktree add /workspace/foh-gauge-orion -b orion/<topic> origin/master
```
Use a separate build dir per worktree (`build/` is git-ignored). Don't commit in `/workspace/foh-gauge` itself (integration only).

## Branches
- `orion/<topic>` for core/DSP work (e.g. `orion/gcc-phat-tuning`)
- `lyra/<topic>` for UI/guides (e.g. `lyra/tf-annotations`)
- `ci/<topic>` for build/CI
- `fix/<topic>` for urgent cross-cutting fixes
Short-lived; rebase on `origin/master` before opening or updating a PR.

## Pull requests
- Target `master`. No direct pushes to `master` except integration fixes.
- CI must be green: **build-macos** (macos-14 arm64 Release build + ctest + `.app` artifact) and **linux-tests** (core + Catch2).
- New DSP behaviour needs a Catch2 test against synthetic signals.
- UI PRs: attach a screenshot. On Linux you can grab one headlessly:
  `FOH_START_PAGE=1 xvfb-run -a bash -c '"build/app/FOHGauge_artefacts/Release/FOH Gauge" & sleep 5; import -window root shot.png; kill $!'`
  (`FOH_START_PAGE` 0-3, `FOH_GUIDE` 0-2, `FOH_STEP` n.)
- Squash-merge; keep the subject line imperative.

## Local build
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build --output-on-failure
```
Core only: add `-DFOH_BUILD_APP=OFF`.
