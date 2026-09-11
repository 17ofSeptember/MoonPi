# BME280 and I2C

The BME280 driver uses the vendored Bosch SensorAPI in forced-measurement mode.
Its read Trigger produces temperature in degrees Celsius, pressure in hPa,
relative humidity in percent, and a combined JSON reading. The simulation uses
register-level calibration fixtures through the same sensor driver; it is not a
physical sensor calibration or accuracy test.

Import `examples/bme280-alarm.moonpi.json`, Apply and Run to sample every 500 ms
and drive an LED from a temperature comparison. The example simulation reads
about 25.08 C. Import `examples/button-led.moonpi.json` for a debounced button.

## Linux hardware support

Physical use is implemented but remains unverified on a Pi. Hardware mode also
requires the Pi 3 B+ identity checks described in LINUX_GPIO.md. Enable the OS I2C
interface and give the service account access to `/dev/i2c-1`. Connect SDA to
physical pin 3, SCL to pin 5, 3.3 V supply to pin 1 and ground to pin 6. The driver
accepts addresses 0x76 or 0x77; the address must match the module configuration.
Use a module compatible with 3.3 V supply and logic.

The graph allocator shares the two bus lines between distinct addresses, rejects
duplicate addresses, and prevents their use as GPIO at the same time. Transfers
require a compiled per-node lease and are bounded to 256 bytes. The Linux backend
uses combined I2C_RDWR transactions and checks kernel address ownership without
I2C_SLAVE_FORCE. It takes an advisory flock on the adapter; unrelated programs
that ignore that lock can still interfere, so dedicate the bus to this service.

The backend sets adapter-wide timeout to 100 ms and retries to zero. These Linux
settings affect other users of the adapter and are not restored on release.
They cannot guarantee that a faulty kernel driver will return on time.
Sensor transfers run on a separate worker, so a blocked read does not block
Emergency stop of GPIO outputs. Cancellation drops stale readings. Shutdown or a
subsequent Apply can still wait for a kernel transfer to return.

Sensor failures appear as DEGRADED with an error and retry with a one-second
backoff. No failed measurement is fabricated as a valid reading. The last
successful value remains available while the node reports the error.

Implementation references: [Linux userspace I2C interface](https://docs.kernel.org/i2c/dev-interface.html)
and the [pinned Bosch SensorAPI](https://github.com/boschsensortec/BME280_SensorAPI/tree/c90d419492e26dd95586598a794e65eb2760753a).
