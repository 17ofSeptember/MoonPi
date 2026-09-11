# Development and verification

Windows: install Visual Studio C++ Build Tools, Windows SDK, CMake >=3.24 and
Node.js 24. Use `scripts/build-windows.ps1`. The repository was initially empty
apart from the specification. Development machine: Windows 10 x64, Visual Studio
Build Tools 2026 / MSVC 19.51, CMake 4.4.0, Node 24.13.0.

```powershell
npm.cmd ci --prefix frontend
npm.cmd run build --prefix frontend
cmake -S . -B build-native -G 'Visual Studio 18 2026' -A x64
cmake --build build-native --config Debug --parallel 4
ctest --test-dir build-native -C Debug --output-on-failure
node tests/integration.mjs
.\build-native\Debug\moonpi.exe --simulation --web frontend/dist
```

VS 2022 users can use `-G "Visual Studio 17 2022"` or omit the generator to allow
CMake to detect the installed version. `build-native` is the active build directory;
earlier `build`/`build-win` dependency-extraction attempts are disposable artifacts.

If Windows blocks local PowerShell scripts, invoke the build or package script
with a process-only policy override, for example
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-release.ps1`.
This does not change the machine's execution policy.

Browser tests use installed Microsoft Edge on Windows. Set MOONPI_BROWSER to
`chromium` and install Playwright Chromium on Linux; set MOONPI_EXE to the native
executable path. Tests launch only simulation and use temporary workspace data.
They deliberately terminate the service to verify crash recovery. The GPIO device
seam has native failure/lifetime tests; an opt-in physical GPIO17 test is documented
in `LINUX_GPIO.md`. A long soak suite remains pending. Test evidence is recorded separately
in `docs/TEST_RESULTS.md` when executed.

Linux/Pi source build: `sh scripts/build-pi.sh`. Raspberry Pi target is ARM64
Raspberry Pi OS Lite. The first build needs Git/network for dependencies; retain
the CMake dependency cache for offline rebuilds. Core operation has no internet
requirement after packaging. Build web assets on Windows and copy frontend/dist
to the Pi; Node.js is unnecessary on the deployed Pi.

Sanitizers: `-DMOONPI_ENABLE_ASAN=ON` and on GCC/Clang
`-DMOONPI_ENABLE_UBSAN=ON`. Use a separate build directory. These options are not a
claim that sanitizer runs have been completed.

Linux x64 build and simulation verification now pass under WSL Ubuntu. To use
existing downloaded dependency sources offline, configure with
`-DFETCHCONTENT_SOURCE_DIR_JSON=build-native/_deps/json-src`,
`-DFETCHCONTENT_SOURCE_DIR_JSON_VALIDATOR=build-native/_deps/json_validator-src`, and
`-DFETCHCONTENT_SOURCE_DIR_CIVETWEB=build-native/_deps/civetweb-src`.
These override source locations only; Linux uses its own `build-linux` binary tree.

To verify an independently extracted Windows package, set `MOONPI_PACKAGE` to
its `MoonPi` directory and run `node tests/integration.mjs`. The test verifies the
bundled manifest before launch, then uses packaged schemas, components and web
assets for the full browser/API suite. Test data stays under build-win.

The release ZIP has a `.sha256` sidecar. `manifest.json` inside the package lists
every bundled file's relative path, byte length and SHA-256 hash. Packaging keeps
the prior release directory as `MoonPi-previous-<id>` so saved data is preserved;
these prior directories are not included in the fresh ZIP.

Run `node tests/security.mjs` for the protected-service suite. It generates a
temporary test credential, starts a simulation service on loopback port 18082,
and checks session exchange, browser login/reload/sign-out, cookie protection,
rate limits, telemetry revocation, health and audit-disk recovery. It accepts
`MOONPI_EXE` or `MOONPI_PACKAGE` like the main integration suite and never prints
the generated access token. It uses a separate temporary data directory.
