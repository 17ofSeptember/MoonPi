# Verification record

## M8: Linux deployment and installation check, 0.8.0 (2026-09-10)

- Windows and Linux x64 native suites pass: 87 cases (30 core, 14 GPIO, 43 advanced).
  The installation-check case verifies no data directory is created and detects
  missing frontend/schema resources. The physical LED test remains skipped.
- TypeScript/Vite production build passes.
- Linux tarball builds with architecture metadata and SHA-256 file manifest.
  `tests/deployment.py` passes against the extracted x64 bundle: manifest validation,
  no-write doctor, staged installation, systemd unit syntax, token permissions,
  config/project preservation, repeat installation, unlisted files, manifest
  traversal, tampering and symlink-target rejection. No live service was installed.
- Packaged Linux execution passes 20 BME280 simulation Stop/E-STOP cycles, descriptor
  bounds, Save and clean SIGTERM/restart, using bundled frontend/resources/examples.
  Evidence: `build-linux/smoke-1789060353635625047/`.
- ARM64 binaries, live systemd installation and actual Pi device access/reboot are
  not verified. The x64 archive is not a Pi binary; build on the target Pi OS.

## M7: guided starter projects, 0.7.0 (2026-09-10)

- Windows Release: 86 native cases pass (29 core, 14 GPIO, 43 advanced).
  New coverage validates all five gallery examples and proves that Validate/Apply
  do not mark a changed project as saved; Load restores the saved status and design.
- TypeScript/Vite production build and full browser acceptance pass in
  `build-win/integration-1789058733094/`. Gallery coverage checks search, wiring
  preview, replacement cancellation, atomic Undo/Redo, fresh identities, explicit
  Apply, saved-state feedback, guide persistence across reload and protection of a
  running graph. `starter-gallery.png` was visually reviewed.
- Starter data is bundled offline. Opening creates only a draft and never runs or
  overwrites the saved project automatically.
- This phase adds onboarding, not new hardware support or physical verification.
- A Linux advanced-suite run stalled at worker shutdown. Inspection identified
  unsynchronized stop-predicate changes that could lose condition-variable wakeups.
  Runtime and sensor workers now request stop under their wait mutex. A new 500-cycle
  idle construction/shutdown stress case and 60-second native suite timeout cover
  this regression; the total suite is now 86 cases.

## M6: components, Lua pin scripts and board image, 0.6.0 (2026-09-10)

- Windows Release build: 83 native cases pass (27 core, 14 GPIO, 42 advanced).
- Linux x64 Release build and native suites pass; the physical LED test remains explicitly skipped.
- New native coverage checks script syntax/source limits, binary rejection, isolated state,
  event values, unavailable external/bypass APIs, strict Boolean writes, instruction and
  memory exhaustion, exclusive wiring, Start/Run/E-STOP/restart, fault diagnostics and
  LOW output after failure. All digital component profiles compile with their required wiring.
- TypeScript/Vite production build passes. Full browser suite passes in
  `build-win/integration-1789057100269/`: reference-image loading, all 40 handles,
  existing node/wire editor features, Lua editor, syntax rejection, live scripted output,
  manual trigger, Stop and export of edited source. `pin-script-board.png` was visually reviewed.
- Protected-service/browser security suite passes in `build-win/security-1789013371753/`.
- Vendored Lua archive verified against the official SHA-256. No external interpreter is needed.

Hardware simulation and Linux x64 checks do not establish physical Pi or specific module
compatibility. These remain unverified. See PIN_SCRIPTS.md for electrical and scripting limits.

## M5: sessions, health and audit hardening, 0.5.0 (2026-09-09)

- Windows Release build and 75 native cases pass (27 core, 14 GPIO, 34 advanced).
  New coverage includes cookie ambiguity, OS session generation, revocation,
  fixed expiry, bounded session allocation, audit write recovery and three-archive
  rotation. The Linux x64 Release build also passes.
- Protected-service acceptance passes with a temporary random credential:
  bearer-to-session exchange, unrelated cookies, rejection of raw-token/duplicate
  cookies, origin checks, command and login throttling, Stop/E-STOP bypass,
  audit-disk failure/recovery reporting, browser login/reload/sign-out and telemetry
  revocation. It verifies the access token is absent from browser storage and
  server logs, and the session cookie is HttpOnly. Script: `tests/security.mjs`.
- Security artifacts: `build-win/security-1789011916354/`; authenticated Health
  panel screenshot was visually reviewed. The full unprotected editor and
  automation suite also passes (`build-win/integration-1789011660125/`).
- TypeScript/Vite production build passes. The access token is exchanged in memory
  and cleared from React state after login; the browser uses the opaque cookie.

These checks do not establish internet-facing security. TLS, independent user
roles, hostile-network testing and physical Pi verification remain pending.
See SECURITY.md for the actual session, health and HTTP limits.

## M4: automation sequences and manual triggers, 0.4.0 (2026-09-09)

- Windows Release build and 70 native cases pass: 27 core, 14 GPIO and 29 advanced.
  New cases cover step order and output state, completion, cancellation, restart
  versus ignore policies, emergency stop, duration bounds, Trigger fan-in,
  duplicate connections, value-writer conflicts and stale manual-trigger commands.
- Headless Edge passes the full existing editor/API suite plus sequence import,
  manual-start availability, live step display, LED state, cancel/reset, restarting
  a cancelled sequence and completion. Artifacts:
  `build-win/integration-1788990844415/`.
- Frontend TypeScript/Vite build passes. Manual triggers appear near the top of
  the component inspector so actions remain accessible for nodes with many wires.
- The Linux x64 Release build passes with the same native sequencer implementation.
  Physical hardware timing and behavior remain unverified.

See SCHEDULING.md for sequence timing, retrigger and cancellation semantics.
Sequences are not resumed midway after restart, and cancellation needs explicit
reset wiring for actions that earlier steps already performed.

## M3: sensing, inputs, logic, scheduling and editing, 0.3.0 (2026-09-09)

- Windows x64 Release build and all three native suites pass. Coverage includes
  GPIO input release, I2C sharing/address conflicts, sensor compensation, retries,
  blocked-read emergency stop, input debounce, typed logic, scheduler clock jumps,
  restart replay protection and checkpoint corruption/storage failure.
  Latest native totals: 27 core, 14 Windows GPIO and 23 advanced cases.
- Frontend TypeScript/Vite production build passes. Headless Edge acceptance adds
  BME280 alarm import, live sensor values and LED output, project export, simulated
  button press/release, subgraph copy/paste with fresh IDs and internal wires,
  atomic undo/redo and text-field shortcut protection. Existing deletion,
  persistence and native execution tests continue to pass.
- Browser artifacts: `build-win/integration-1788965064450/`. The sensor screenshot
  was visually inspected; the example interval was moved below the sensor to
  avoid covering its final port.
- Linux x64 WSL build and native suites pass for the calendar implementation;
  the physical GPIO test remains explicitly skipped without its opt-in flag.
  The expanded Linux smoke test also passes 20 BME280 Start/Stop/E-STOP cycles,
  checks the process descriptor count, saves, sends SIGTERM and verifies a clean
  stopped restart. Artifacts: `build-linux/smoke-1788989842744853800/`.
- Single-file component inspect succeeds with the BME280 definition.
- Packaging now installs into a fresh staging folder, preserves the previous
  release directory, writes per-file SHA-256/size metadata and a ZIP checksum.
  `MOONPI_PACKAGE` enables full integration testing from an extracted package and
  verifies all manifest entries and absence of extra files before launch.
- The initial 0.3.0 extracted package passed manifest verification and the full
  browser suite (`build-win/integration-1788965493946/`). Subsequent editor work
  adds explicit component Unlink, wire selection/endpoint editing/Delete wire,
  component labels, cut/select-all, group movement, alignment and duplication.
  These pass in `build-win/integration-1788990158032/`; wire inspector screenshot
  was visually reviewed. Cut testing found and fixed a deferred-state Undo bug:
  history now captures the prior design before enqueuing any React updates.

Physical GPIO/I2C/input behavior, ARM64 execution and actual Needle inference
remain unverified. SPI/UART/PWM/1-Wire, sequences and native AI remain unfinished.
See I2C.md and SCHEDULING.md for operational limits, including kernel transfer
waits and at-most-once calendar delivery.

## M2: deletion and Linux GPIO software phase, 0.2.0 (2026-09-08)

- Windows x64 Release: C++ build and 40 native cases pass (27 core plus 13 GPIO
  device/runtime cases). Frontend TypeScript/Vite production build passes.
- Headless Edge: the existing circuit acceptance stages pass, plus inspector
  deletion, Delete/Backspace, individual-wire deletion, toolbar multi-selection, attached-wire removal,
  atomic undo/redo, board protection, input-field focus and saved deletion.
  Deletion during Run leaves the applied hardware snapshot unchanged until Stop
  and Apply. Browser script: `tests/integration.mjs`.
- Linux x64 under WSL Ubuntu, GCC 15.2.0, CMake 4.0.3: full native Release build
  passes, including `gpio_linux.cpp`; all 40 native cases pass. The compiled
  `hardware_led` test is explicitly skipped without its opt-in environment flag.
- Linux simulation API smoke: static assets, mode/health, LED interval execution,
  save, SIGTERM shutdown and stopped clean restart pass. Hardware mode on this
  non-Pi host rejects missing device-tree identity.
- Native GPIO tests cover identity parsing, incorrect chips, reserved lines,
  exclusive ownership, fresh readback, safe initialization/release, partial apply,
  read/write failures, failed Start, telemetry failure, and repeated lifecycles.
  Tests use a fake chip underneath the real character-device backend; this is not
  a physical GPIO test.
- Packaged 0.2.0 ZIP: extracted independently, launched with bundled default
  resource/schema/web paths, and checked in headless Edge. Adding an Interval,
  deleting it and restoring it with Undo passed. Package check artifacts are in
  `build-win/package-check-1788922042025/`.

Local artifacts: `build-win/integration-1788921976373/` (including `delete-controls.png`) and
`build-linux/smoke-1788921723996017254/`. Linux build reused the already pinned
dependency sources under `build-native/_deps`; CMake was installed into the local
ignored `.cache/linux-tools` directory without changing system packages.

Physical Pi 3 B+ GPIO, ARM64 execution, sanitizers and long soak remain unverified.
Needle and bus drivers remain unavailable. See `LINUX_GPIO.md` for the physical
test and `IMPLEMENTATION_PLAN.md` for outstanding specification work.

## M1 historical evidence: Windows simulation 0.1.0

Verified on 2026-09-08 on Windows x64 with MSVC 19.51 / Visual Studio Build
Tools 2026, CMake 4.4.0 and Node.js 24.13.0.

### Build and automated checks

| Check | Result |
| --- | --- |
| `npm.cmd run build --prefix frontend` | TypeScript and Vite production build passed |
| `cmake --build build-native --config Debug --parallel 4` | Passed |
| `cmake --build build-native --config Release --parallel 4` | Passed |
| `ctest --test-dir build-native -C Debug --output-on-failure` | Passed: 27 native cases |
| `ctest --test-dir build-native -C Release --output-on-failure` | Passed: 27 native cases |
| Release executable `--validate-components` | Passed |
| Release executable `--version` | Moon Pi 0.1.0 by 17ofSeptember |
| `node tests/integration.mjs` with `MOONPI_EXE` pointing to Release | All four acceptance stages passed in headless Microsoft Edge |
| `scripts/package-release.ps1` | Created `release/MoonPi-windows-x64.zip` |
| Extracted ZIP smoke check | Started from extracted folder; state, HTML and referenced JS/CSS requests passed |

Native tests cover component/board validation, physical BCM mappings, typed
connections, rail protection, missing ground, LED resistance, resource conflicts,
transaction rollback, native intervals, emergency stop, repeated lifecycle,
atomic saves/backups, revisions, safe reload, malformed data, unavailable drivers,
future-version preservation, design/runtime separation, audit-disk failure,
process ownership and bounded JSON depth.

Browser/API acceptance covers creating and wiring LED plus Interval from an empty
design, Apply/Run, WebSocket state updates, all 40 board pins, emergency stop,
stale revision rejection, origin and traversal rejection, save, process restart,
and native execution continuing after browser closure. Restart restores the graph
with no active hardware leases and automation stopped. Browser runtime errors
were not observed.

Local acceptance artifacts: `build-win/integration-1788920193526/` contains
`running.png` and `server.log`. Component validation output is stored in
`build-native/component-validation.json`. Build directories are ignored artifacts.

The package smoke check extracted to `build-win/package-smoke-1788920282370/`
and launched on loopback port 18081 using only the packaged default resource,
schema and web paths. It confirmed stopped simulation with no hardware leases.
The test process was terminated afterward. Packaging used a process-only
PowerShell execution-policy override because local scripts are disabled on this
machine. The final package includes this report and the launch instructions.

### M1 limits at the time

That verification covered the first Windows simulation milestone. Real Raspberry Pi hardware,
Linux/ARM64 execution, sanitizers, long-duration soak testing and native Needle
inference were not tested at that point. Hardware mode and AI were explicitly unavailable.
The remaining specification work is tracked in `IMPLEMENTATION_PLAN.md`.
