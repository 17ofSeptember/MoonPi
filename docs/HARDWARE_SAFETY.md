# Hardware safety policy

Simulation remains the default. `--hardware` enables the Linux GPIO output backend
only after Pi 3 B+ device-tree and chip identity checks; it rejects Windows and
unsupported boards and never silently falls back. This backend has software tests
but has not been verified on physical electronics. See `LINUX_GPIO.md`.

Deterministic validation checks schema, component/driver references, GPIO conflicts,
rail misuse, reserved pins, signal voltage, supported interfaces, required ground
and supply, current bounds, LED resistance, port types and cycles. Unsupported
I2C/SPI/UART/PWM/1-Wire assignments are rejected, not partially configured.

Outputs start LOW. Apply while running is rejected. Configuration failure releases
all prepared outputs; restarting never restores previous output states. Stop
cancels timers and releases GPIO. EmergencyStop also latches a backend lockout
until ClearEmergency. This software feature is not a physical emergency circuit.

Desired and actual values are distinct in telemetry. The simulated backend
supports injected operation failures for transaction testing. The Linux backend
uses exclusive kernel line requests, reads actual line values, and initializes
outputs LOW in the request itself. Its device seam tests ownership conflicts,
partial preparation, read/write failure and cleanup. Telemetry read failures stop
the runtime and surface an error. Shutdown attempts LOW before closing requests,
but a failed write, process kill or power loss cannot guarantee an electrical
state. External pull resistors and physical safety circuitry remain necessary for
loads that need a guaranteed state. Bus and physical error behavior remain untested.

The server defaults to `0.0.0.0:8080` for LAN use. Bind `127.0.0.1` to restrict
development access. Do not port-forward it to the Internet. Optional MOONPI_TOKEN
requires a bearer/session token; HTTP does not encrypt LAN traffic. Same-origin
checks reject browser cross-origin API and WebSocket access. Commands are bounded
to 1 MiB, WebSocket frames to 4 KiB before allocation, and normal commands to 30/s.
No arbitrary shell, file, cloud-AI or raw-GPIO API is available. Static files use
an asset allowlist and canonical containment checks. Production TLS, hardened
session issuance and per-client admission/rate policies remain follow-up work.

Audit records rotate at 1 MiB with one retained file. Individual sensor ticks are
not written to disk. Save commands flush the file and retain one previous backup.
Never claim these software checks prove the electrical correctness of a physical
circuit that has not been inspected and tested.
