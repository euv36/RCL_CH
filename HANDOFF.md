# HANDOFF: native C++ rewrite (`camera_cpp`)

Prepared: 2026-11-19, at user request before manual notebook shutdown.

This is a continuation handoff, **not a completion report**. Do not claim the native rewrite is production-ready or fully verified.

## Repositories

- New native project: `C:\Users\sweetener\Desktop\camera_cpp`
- Branch: `native-optimization`
- Native `HEAD`: `ce71c3f485bf70ead3ea1122fc233c6ffdb346d6`
- Remote: **none configured in `camera_cpp`**. The user had requested push, but push did not happen.
- Original reference project: `C:\Users\sweetener\Desktop\camera`
  - Do not reset, clean, delete, or overwrite its dirty/untracked user work.
  - Approved spec/plan were read from this repository.
- The native project is the continuation target; the original project is legacy/reference.

## Approved design and implementation plan

- Design: `C:\Users\sweetener\Desktop\camera\docs\superpowers\specs\2026-11-18-native-optimization-design.md`
- Implementation plan: `C:\Users\sweetener\Desktop\camera\docs\superpowers\plans\2026-11-19-native-optimization.md`
- Native repo contains copies of both:
  - `C:\Users\sweetener\Desktop\camera_cpp\docs\spec.md`
  - `C:\Users\sweetener\Desktop\camera_cpp\docs\implementation_plan.md`
- Execution ledger:
  - `C:\Users\sweetener\Desktop\camera_cpp\.superpowers\sdd\2026-11-19-native-optimization\progress.md`

## What is committed and currently green

Tasks 1–11 are committed on `native-optimization`:

1. `617ab99` — scaffold and CMake skeleton
2. same root commit — spec/plan copies; docs boundary ledgered
3. `39ce1f8` — atomic config/JSON foundation
4. `336e00e` — laptop dependency gate
5. `c8ab8c8` — HSV/LAB conversion core
6. `7f60e39` — native gate clustering + false-positive shape guard
7. `d3f32e2` — distance estimation and self mask
8. `b8d11a9` — gate tracking pipeline
9. `a91fad5` — gate/camera/field-map persistence
10. `a2c4690` — V4L2 camera capture core
11. `ce71c3f` — camera service, status JSON, ZMQ listeners

Fresh verification in this session:

```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
# 17/17 passed
```

```powershell
$env:Path = 'C:\Users\sweetener\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;' + $env:Path
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build-win --output-on-failure
# 12/12 passed
```

Important: invoking bare `ctest` without prepending the appropriate compiler/runtime PATH fails with `0xc0000135`. That is an environment/PATH issue, not a test regression. Use the exact commands above.

Git status at verification:

```text
## native-optimization
```

Working tree was clean before this `HANDOFF.md` was added. This handoff file is intentionally untracked unless someone explicitly commits it later.

## Readiness by requested original bugs and rewrite goals

These are **local native-core readiness**, not live deployment readiness.

| Area | Status | Evidence / caveat |
|---|---|---|
| False gate detection shape guard | Implemented + unit/parity verified | Tasks 6, 8, C++ tests. Tape/lamp rejection and split-post merge are covered. Not validated on live Pi/camera scene. |
| Radar zero configured gates | Not applicable/readied in native project | Native UI/radar is not implemented. Old Python behavior was partially fixed, not live verified. |
| Radar window close by X | Not implemented in native project | Old Python path was mocked/tested, not live click-verified. |
| Ctrl+V clipboard paste | Not implemented in native laptop UI | Plan Task 13 only. |
| Camera settings apply path | Native core/service listener implemented; not live verified | `CamSettings`, config ranges, ZMQ 5559 listener and service wiring exist; Pi not tested this run. |
| HSV/INFO UI | Not implemented in native project | Plan Tasks 13–14. Russian INFO content remains unmet. |
| Local ELP live smoke | Not run for native app | Plan Task 19. |
| Pi temporary native smoke | Not run | Plan Task 20. Pi was unreachable in final probe. |
| Robot core native port | Not started | Plan Task 15. |
| Mega firmware protocol port | Not started; no flash authorized by this handoff | Plan Task 16. |
| GitHub push | Not done | No remote in `camera_cpp`. |
| Notebook sleep | Not done and must not be done automatically | User is manually shutting down. |

## What is not done

- Task 12 — Pi native service Linux build/test
- Task 13 — SDL2/Dear ImGui laptop shell, CONNECT, clipboard paste
- Task 14 — laptop UI panels/help parity, including Russian INFO
- Task 15 — robot core native port
- Task 16 — Mega firmware port; no live flash unless separately approved and safety gates are met
- Task 17 — Windows laptop release build/parity report
- Task 18 — Pi build and native service verification
- Task 19 — local ELP live smoke on laptop
- Task 20 — temporary Pi native live smoke
- Task 21 — final metrics, final report, push/finish handling, sleep

## Pi status

Final probe saw:

- SSH `10.132.122.51:22`: unreachable
- TCP `10.132.122.51:5555`: unreachable

Therefore:

- No Pi native build was run.
- No Pi service was deployed.
- No Pi service was started or stopped.
- No Pi calibration/config files were overwritten.
- Old Python camera service state at shutdown time is unknown.
- Port assumptions and smoke steps must be rechecked when Pi is back online.

SSH key for future use was generated earlier:

- Private: `C:\Users\sweetener\.ssh\rpi_native_optimization_ed25519`
- Public: `C:\Users\sweetener\.ssh\rpi_native_optimization_ed25519.pub`
- Fingerprint: `SHA256:d5RqiC3u6jsZzt04/25V/4YoCknSpofmGeF8jGgH7u4`

Do not put its contents, passwords, or tokens in files/commits/reports. Earlier user approved installing the key and said Pi would be reachable; direct key login was never verified in this session.

## Known environment notes

- Native Windows full dependency path uses MSYS2:
  - `C:\msys64\mingw64`
  - OpenCV 5.0.0 found
  - libzmq found after dependency discovery fix
  - SDL2 installed
  - Dear ImGui is **not** packaged/found as `mingw-w64-x86_64-imgui`; laptop UI Task 13 cannot be completed by assuming a package exists. Vend or otherwise supply ImGui deliberately.
- Pure no-OpenCV Windows build at `build-win` intentionally skips OpenCV-gated tracking/capture/self-mask/Pi camera targets and passes 12/12.
- MSYS2 build at `build-msys2` contains the complete native test suite and passes 17/17 when the MinGW PATH is prepended.
- `local/` in native repo contains runtime contract/calibration seed data and is ignored; do not commit it or push private camera calibration.
- `build-msys2/` and `build-win/` are build outputs and ignored.
- No secrets belong in Git.

## Exact continuation order

1. Read this file and the execution ledger:
   ```powershell
   Get-Content HANDOFF.md
   Get-Content .superpowers\sdd\2026-11-19-native-optimization\progress.md
   ```
2. Confirm branch/HEAD:
   ```powershell
   git status --short --branch
   git log --oneline -5
   ```
3. Re-run both existing suites with the exact PATH-prefixed commands above. Do not interpret bare `ctest` failure as missing code.
4. Resume at **Task 12** in the approved plan:
   - plan line reference: Task 12, around line 588 of `docs\implementation_plan.md` / approved plan in original repo.
   - Scope: Pi Linux native build/test, temporary smoke, no overwrite of Pi calibration/config.
   - Pre-step: re-check Pi reachability. If unreachable, do not deploy; document blocker and continue Tasks 13–16 where possible.
5. Then Tasks 13–16. Keep laptop UI and robot/firmware work separate. Do not flash Mega firmware without explicit approval and the plan’s safety gates.
6. Tasks 17–18: Windows and Pi release builds/reports.
7. Tasks 19–20: live smoke only with camera attached and after reachability/config safety checks.
8. Task 21: final report only after evidence exists. If Pi remains unreachable, mark Pi items unverified; never claim them verified.
9. Do not push until a remote is deliberately configured and the user/project decision is explicit. Do not force-push.
10. Do not put notebook to sleep unless the user explicitly asks again after completion.

## Verification rules for completion

A task may be marked complete only after its plan-named tests ran and passed in the applicable environment. For final handoff/report, separate:

- Build-verified code
- Unit/parity-verified code
- Windows-verified code
- Linux/Pi-verified code
- Live camera-verified behavior
- Unverified/blocked items

Do not merge these categories.

## Ledger discrepancy already recorded

Task 10 ledger initially recorded wrong hash `1c88de6`; the correct commit is `a2c4690`, and the ledger contains a correction line. Keep it.

## Handoff integrity

- Native repo working tree was clean before this file.
- This file is intentionally untracked.
- Original dirty repository was not modified by this handoff.
- No Pi was changed by this handoff.
- No credentials were copied into this file.
