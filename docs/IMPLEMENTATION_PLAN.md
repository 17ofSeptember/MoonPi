# Moon Pi implementation checklist

Author: 17ofSeptember. Authority: the complete root specification (4,111 lines,
including all 88 additions), read before edits on 2026-09-08.

## M1 — Windows executable simulation slice
- [x] Read specification and inspect initial repository (specification only).
- [x] Inspect current Needle README, API documentation and native ctypes bindings.
- [x] Establish C++20/CMake, pinned dependencies and React/TypeScript/React Flow.
- [x] Versioned board, component, project and command schemas; registry validation.
- [x] Accurate Pi 3 B+ header, typed ports, UUID graph identities.
- [x] Deterministic compiler/allocator, electrical diagnostics and explicit leases.
- [x] Hardware authority, transactional apply, desired/actual state and safe lifecycle.
- [x] Native interval execution, generic GPIO driver and simulated GPIO.
- [x] HTTP/static assets, versioned REST/WebSocket, bounded telemetry and origin checks.
- [x] Data-driven canvas/library/inspector, wiring, live state and emergency stop.
- [x] Atomic project save/backup, safe restart and reload.
- [x] Windows build, native failure tests and browser/API integration acceptance.
- [x] Build/run/package documentation and recorded test results.

Verification resumed on 2026-09-08; see [test evidence](TEST_RESULTS.md).
These checks cover the initial Windows simulation slice only.

## Following slices (mandatory specification work, not implied complete by M1)
- [x] Linux GPIO character-device backend and opt-in hardware test harness.
- [ ] Run the physical GPIO verification on a Raspberry Pi 3 B+.
- [x] I2C allocator/sharing/address conflicts, BME280 simulation and Linux I2C.
- [ ] SPI, UART, PWM, 1-Wire implementations and device recovery.
- [ ] Representative 16-component standard deliverable and developer tools.
- [x] Button/debounce, logic, sequences, persistent calendar scheduling.
- [ ] Native isolated Needle 2 worker, constrained proposals, confidence and circuit breaker.
- [ ] AI proposal diff and atomic design patches through deterministic validation.
- [ ] Extended graph editing, groups, copy/paste and onboarding.
- [ ] Authentication, rate limits, rotating audit logs, health and recovery hardening.
- [ ] Windows/Linux/ARM64 CI, sanitizers, fuzz/property and long soak tests.
- [ ] Pi installation, systemd, offline release manifests and transfer testing.

## Architectural decisions
1. Product metadata is centralized; specification examples using the old name do
   not override the explicit Moon Pi identity requirement.
2. Native application owns graph/runtime/hardware. Browser edits a design snapshot;
   applying validates and compiles an independent runtime snapshot. Applying while
   running requires Stop. Revision checks reject stale clients.
3. Components and board capabilities are JSON data with actual JSON Schemas.
   Drivers are compiled and versioned; unavailable drivers fail closed.
4. Hardware outputs require explicit wiring including ground and electrical
   protection. Rails can be connected for power but can never become GPIO leases.
5. One native scheduler owns timer execution; hardware changes funnel through one
   controller. Simulation substitutes hardware only. Startup never auto-runs.
6. React Flow (MIT) supplies the canvas. Static Vite output is packaged locally;
   no Node.js or CDN is needed at runtime.
7. Native AI integration follows the observed upstream ABI only, after M1. The
   initial unavailable/mock engines have no hardware access.
8. Linux GPIO input/output and BME280 I2C backends are implemented; the GPIO opt-in
   test harness is available. Physical verification remains pending. Unsupported
   platforms and boards reject hardware mode rather than simulate silently.

## M2 software progress
- [x] Visible node/wire deletion, Delete/Backspace, multi-selection, atomic undo/redo.
- [x] Linux GPIO v2 output requests, chip/model checks and actual readback.
- [x] Backend injection and mode reporting through native runtime, API and UI.
- [x] Failure/lifetime tests for the GPIO backend and runtime recovery.
- [x] Linux x64 build, native tests, simulation API and graceful shutdown checks.
- [x] Opt-in GPIO17 physical LED test, skipped unless explicitly enabled.
- [ ] Physical Pi 3 B+ validation and ARM64 build/execution evidence.

## M3 software progress
- [x] Shared I2C allocation, Bosch BME280 driver, Linux transport and register simulation.
- [x] Separate sensor worker, bounded requests, retries and cancellation after Stop.
- [x] Debounced GPIO inputs and simulated press/release controls.
- [x] Typed constants, arithmetic, Boolean logic, counters, display, delay, latch and condition.
- [x] Persistent UTC cron and one-time schedules with explicit missed-event policies.
- [x] Project import/export, subgraph copy/paste and keyboard undo/redo.
- [x] Visible wire unlink/delete, endpoint inspector and reconnect grips.
- [x] Component labels, cut/select-all, group movement, alignment and duplication.
- [x] Single-definition component validate/inspect CLI.
- [ ] Remaining physical profiles/protocols and isolated native AI.
- [ ] Physical BME280 and input verification on Pi hardware.

## M4 automation sequences
- [x] Native four-step sequencer with per-step duration, progress and completion.
- [x] Cancel, explicit retrigger policy and pending timer replacement.
- [x] Manual Trigger command and inspector controls with runtime/revision checks.
- [x] Distinct Trigger events may share an input; value writer and duplicate-wire checks remain enforced.
- [x] Portable LED/latch sequence example and execution semantics documentation.

## M5 service hardening
- [x] OS-generated browser sessions, HttpOnly cookies, expiry and revocation.
- [x] Browser login without persisted access tokens and explicit sign-out.
- [x] Separate login limits; authenticated Stop/E-STOP bypass command throttling.
- [x] Subsystem health API and visible health panel, including audit failure status.
- [x] Three retained audit archives and failure/recovery coverage.
- [ ] TLS, user roles, comprehensive watchdogs and hostile-network qualification.

## M6 component and scripting expansion
- [x] Five passive switches, four digital sensor profiles and three external output-module profiles.
- [x] Vendored Lua, bounded per-node memory/instructions, staged output writes and compile-time syntax validation.
- [x] Script editor, event/Boolean inputs, output events, manual trigger and portable example.
- [x] Reference-photo canvas board with all 40 labeled connection handles.
- [ ] Physical qualification of specific sensor and output module models.

## M7 guided starter projects
- [x] Offline searchable gallery of five working examples with parts and wiring previews.
- [x] Replacement confirmation, export option, fresh identities and atomic Undo/Redo.
- [x] Running-graph protection and explicit Apply/Run after opening.
- [x] Persistent starter guide and separate backend saved-to-disk status.
- [x] Native example validation and saved-state regression coverage.
- [x] Shutdown wakeups synchronized with worker wait predicates; repeated idle lifecycle coverage.
- [x] Packaging preserves an in-use extracted release and writes the new build beside it.

## M8 Linux deployment
- [x] Read-only installation check with JSON results and no device requests or application writes.
- [x] Linux tarball packaging, architecture metadata and per-file SHA-256 manifest.
- [x] Explicit staged/live installer, versioned code, preserved config/data and protected token file.
- [x] Non-root systemd unit with bounded restarts and documented device access.
- [ ] Physical Pi installation, ARM64 execution and reboot/hardware validation.
