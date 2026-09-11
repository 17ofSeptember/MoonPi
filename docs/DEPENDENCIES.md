# Dependency manifest

Build dependencies are pinned in CMake and `frontend/package-lock.json`. Initial
inspected source revisions:

| Dependency | Version / commit | License | Purpose |
|---|---|---|---|
| nlohmann/json | 3.12.0 / 55f93686c01528224f448c19128836e7df245f72 | MIT | JSON |
| json-schema-validator | 2.3.0 / 349cba9f7e3cb423bbc1811bdd9f6770f520b468 | MIT | Draft 7 validation |
| CivetWeb | 1.16 / d7ba35bbb649209c66e582d5a0244ba988a15159 | MIT | Native HTTP/WebSocket |
| React / React DOM | 19.2.8 | MIT | UI |
| @xyflow/react | 12.11.6 | MIT | Node canvas |
| TypeScript | 5.9.3 | Apache-2.0 | Build-time type checks |
| Vite | 8.2.2 | MIT | Static asset build |
| Playwright | 1.58.2 | Apache-2.0 | Browser acceptance tests |
| Bosch BME280 SensorAPI | c90d419492e26dd95586598a794e65eb2760753a | BSD-3-Clause | Native sensor compensation and acquisition |

React Flow's MIT core supports the required editing primitives without a paid
runtime dependency. No remote fonts, icons, scripts or styles are required.
CivetWeb compiles without CGI, TLS, filesystem serving or dynamic scripting.
Moon Pi provides its own static allowlist handler. A checked source transformation
in CMake adds a 4096-byte WebSocket frame limit before payload allocation; changing
the upstream version requires rechecking that patch and security tests.

Dependency source licenses remain in fetched source and npm package directories.
Release redistribution must include copied license texts; packaging work tracks
this explicitly. This project has not assigned an open-source license on behalf
of its author. Do not infer one from its dependency licenses.
