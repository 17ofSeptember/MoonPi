# Linux GPIO output phase

The GPIO output backend is implemented. Windows and Linux x64 software tests pass;
physical Pi verification and ARM64 execution remain pending. Simulation remains
the default on every platform. This phase supports LED and generic digital output
using the existing compiler, leases, driver and interval scheduler.

## Build and start on Pi 3 B+

Use Raspberry Pi OS Lite 64-bit with Linux GPIO character-device API v2 (kernel
5.10 or later), C++20, CMake >=3.24 and Linux development headers containing
`linux/gpio.h`. No libgpiod or Python package is required by the application.
Build `frontend/dist` on Windows and copy it with the source, then:

```sh
sh scripts/build-pi.sh
./build-pi/moonpi --simulation --web frontend/dist
```

For physical output, first verify the wiring and choose the GPIO chip:

```sh
ls -l /dev/gpiochip*
./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist
```

Use an account permitted to open the GPIO device, normally through the device's
`gpio` group on Raspberry Pi OS. Check the device permissions on the actual OS;
the application does not alter groups, device permissions or kernel configuration.
Do not grant access to unrelated devices or run the service as root to bypass a
permission error.

The selected device must be `/dev/gpiochipN`, with chip label `pinctrl-bcm2835` and
54 lines. The device tree must contain `raspberrypi,3-model-b-plus`. Chip numbering
is configurable because it is not an identity guarantee. Unsupported devices,
missing permissions and already-owned lines produce errors. The UI and health
endpoint report hardware mode. No lines are requested on startup; Apply prepares
LOW outputs and Run starts the automation. Stop/E-STOP release requests.

`--gpio-chip` overrides `MOONPI_GPIO_CHIP`, which overrides configuration key
`gpio_chip`. `--hardware`/`--simulation` override `MOONPI_MODE` and key `mode`.
Mode defaults to simulation. `--dry-run` validates without opening a GPIO chip,
even when `--hardware` is supplied; it does not test device availability or wiring.

## Physical LED verification (not yet run)

Use only a low-current indicator LED for the first test. With the Pi powered off,
connect physical pin 11 / GPIO17 through a suitable series resistor (the example
uses 1000 ohms) to the LED anode, and its cathode to physical pin 6 / GND. Check the
actual LED rating and polarity. Disconnect other loads from GPIO17. A graph's
virtual wiring is not evidence that the physical circuit matches it.

This test makes three 250 ms pulses on GPIO17. It requires both a build option and
an environment flag, and is skipped in ordinary test runs:

```sh
cmake -S . -B build-pi -DCMAKE_BUILD_TYPE=Release -DMOONPI_BUILD_HARDWARE_TESTS=ON
cmake --build build-pi --parallel 2
MOONPI_TEST_GPIO17=1 MOONPI_GPIO_CHIP=/dev/gpiochip0 \
  ctest --test-dir build-pi -L hardware --output-on-failure
```

The test checks requests, values and release. Observe the LED separately and
record the board, OS/kernel, chip, wiring and result. Then test the same circuit
through the browser: Apply, Run, Stop, E-STOP, Save and clean restart. Confirm that
another consumer holding GPIO17 causes Apply to fail without leaving other
outputs requested.

## Boundaries and implementation references

GPIO2-27 are the only accepted output offsets; GPIO0/1 and non-header lines are
rejected. Output requests are exclusive and initialized LOW. Fresh kernel reads
supply actual telemetry. Failure during preparation releases all prepared lines;
readback failure during telemetry stops automation and remains visible in the UI.
Closing a line request releases ownership. LOW on shutdown is best effort, not a
guarantee after a device failure, process kill or power loss. This is not a
safety-rated controller. I2C, SPI, UART, PWM and 1-Wire are not implemented here.

Implementation follows the [Linux GPIO v2 API](https://docs.kernel.org/userspace-api/gpio/chardev.html),
[line request API](https://docs.kernel.org/userspace-api/gpio/gpio-v2-get-line-ioctl.html)
and [value read API](https://docs.kernel.org/userspace-api/gpio/gpio-v2-line-get-values-ioctl.html).
The board identity and line mapping were checked against the
[Raspberry Pi device tree](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm/boot/dts/broadcom/bcm2837-rpi-3-b-plus.dts)
and [GPIO driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/pinctrl/bcm/pinctrl-bcm2835.c).
