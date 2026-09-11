<div align="center">
# MoonPi
<img width="500" height="500" alt="logo-removebg-preview" src="https://github.com/user-attachments/assets/d257b744-3d3a-4edb-9d9b-f99af665692a" />

**MoonPi** is a visual, node-based hardware workstation for the Raspberry Pi.

Instead of writing a full program every time you want to control an LED, read a button, run a timer, or connect hardware together, MoonPi lets you build your project visually in a web browser.

You place components on a canvas, connect them to Raspberry Pi pins, connect logic nodes together, validate the design, and run it on the Raspberry Pi.

> **Current status:** Beta  
> **Version:** 0.8.0 Beta  
> **Creator and maintainer:** [17ofSeptember](https://github.com/17ofSeptember)  
> **Website:** https://www.awrynetwork.com/links  
> **Contact:** awrynetwork@gmail.com
> **Buy Me A Coffee!** https://buymeacoffee.com/17ofseptember

---

## What can MoonPi do?
<img width="1579" height="765" alt="1" src="https://github.com/user-attachments/assets/bf41300a-a0fa-410d-8a06-dd3373cce481" />

MoonPi is designed to make Raspberry Pi hardware projects easier to understand and experiment with.

Current features include:

- Visual node-graph project editor
- Raspberry Pi GPIO output control
- Raspberry Pi GPIO input support
- Hardware wiring between the Raspberry Pi and components
- Logic connections between nodes
- Timers and intervals
- Built-in starter projects
- Project validation before hardware is activated
- Apply and Run workflow for safer hardware control
- Emergency Stop
- Project saving, loading, importing, and exporting
- Built-in diagnostics
- Hardware mode for a real Raspberry Pi
- Simulation mode for experimenting without controlling real GPIO pins
- Browser-based user interface
- Custom pin scripting
- Native background runtime for automation

MoonPi is intended to be understandable by beginners while still being useful for more advanced Raspberry Pi projects.

---

## Current hardware support

MoonPi 0.8.0 Beta currently targets:

- **Raspberry Pi 3 Model B+**
- Linux / Raspberry Pi OS
- Standard 40-pin GPIO header
- GPIO character device interface, normally `/dev/gpiochip0`

Hardware support is still being developed. Other Raspberry Pi models may be added in future releases.

---

# Installing MoonPi on a Raspberry Pi

These instructions assume you are using a **Raspberry Pi 3 B+** running a recent version of Raspberry Pi OS.

## 1. Update your Raspberry Pi

Open a terminal and run:

```bash
sudo apt update
sudo apt upgrade -y
```

## 2. Install the basic build tools

MoonPi is a C++ project built with CMake.

Install the basic compiler and build tools:

```bash
sudo apt install -y build-essential cmake git
```

## 3. Download MoonPi

Clone the repository:

```bash
cd ~
git clone https://github.com/17ofSeptember/MoonPi.git
cd MoonPi
```

If you downloaded a release archive instead, extract it and enter the extracted MoonPi folder.

Example:

```bash
cd ~/MoonPi
```

## 4. Build MoonPi

Create a Raspberry Pi build directory:

```bash
cmake -S . -B build-pi
```

Compile MoonPi:

```bash
cmake --build build-pi -j$(nproc)
```

When the build finishes, the MoonPi executable should be located at:

```text
build-pi/moonpi
```

You can confirm it exists with:

```bash
ls -l build-pi/moonpi
```

---

# Running MoonPi on real Raspberry Pi hardware

From the MoonPi project folder, run:

```bash
./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist
```

This tells MoonPi to:

- use **real hardware mode**
- use `/dev/gpiochip0` for GPIO access
- serve the MoonPi web interface from `frontend/dist`

When MoonPi starts successfully, open a browser on the Raspberry Pi and go to:

```text
http://localhost:8080
```

The top of the MoonPi interface should say:

```text
HARDWARE MODE
```

If it says **SIMULATION MODE**, MoonPi is not controlling the Raspberry Pi GPIO pins.

---

## Opening MoonPi from another computer

MoonPi can also be opened from another device on the same local network.

First, find the Raspberry Pi's IP address:

```bash
hostname -I
```

For example, if the Pi reports:

```text
192.168.1.50
```

open this on another computer:

```text
http://192.168.1.50:8080
```

You may need to start MoonPi with a network bind address:

```bash
./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist --bind 0.0.0.0
```

Only expose MoonPi on networks you trust.

---

# Making MoonPi easier to start

The full hardware command is long:

```bash
./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist
```

You can create a simple `moonpi` command.

Run:

```bash
cat >> ~/.bashrc <<'EOF2'

moonpi() {
    (cd ~/MoonPi && ./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist)
}
EOF2

source ~/.bashrc
```

After that, you can start MoonPi from any terminal with:

```bash
moonpi
```

---

# Checking GPIO access

MoonPi hardware mode needs access to the Raspberry Pi GPIO device.

Check that it exists:

```bash
ls -l /dev/gpiochip*
```

On a Raspberry Pi 3 B+, you should normally see `/dev/gpiochip0`.

You can also check your user groups:

```bash
groups
```

Look for:

```text
gpio
```

If your user does not have GPIO access, add it to the GPIO group:

```bash
sudo usermod -aG gpio $USER
```

Then reboot:

```bash
sudo reboot
```

---

# Using MoonPi

MoonPi uses a visual workspace.

A basic project follows this pattern:

1. Add a component.
2. Add any logic or timing nodes you need.
3. Connect the Raspberry Pi pin to the hardware component.
4. Connect logic nodes together.
5. Validate the design.
6. Apply the design.
7. Press **Run**.
8. Press **Stop** when finished.

---

## The main parts of the interface

### Component Library

The component library contains hardware, timing, logic, and other nodes that can be added to your project.

Click or drag a component into the workspace.

### Raspberry Pi board

The Raspberry Pi board represents the physical GPIO header.

Pins are shown by their physical pin number and function.

For example:

```text
Physical Pin 11 = GPIO17
```

Be careful not to confuse **physical pin numbers** with **BCM GPIO numbers**.

### Wires

MoonPi uses two general types of connections:

**Hardware connections** connect a Raspberry Pi pin to a physical component.

**Logic connections** connect nodes together to control how the project behaves.

### Inspector

Select a node or wire to view and change its settings.

### Validate

Before running a project, use:

```text
Validate design
```

MoonPi checks the project for wiring and configuration problems.

Fix any reported errors before continuing.

### Apply

**Apply** sends the current validated design to the runtime.

Editing the canvas does not automatically change a circuit that is already running.

If you change the design:

1. Stop the project.
2. Make your changes.
3. Validate.
4. Apply.
5. Run again.

### Run

Press **Run** to start the applied project.

In hardware mode, GPIO operations are sent to the Raspberry Pi.

### Stop

Press **Stop** to stop the current project.

### E-STOP

The **E-STOP** button is the emergency stop.

Use it if hardware begins behaving unexpectedly.

MoonPi releases its outputs when the emergency stop is activated.

---

# Your first project: blinking an LED

A simple LED is a good way to test MoonPi.

## What you need

- Raspberry Pi 3 B+
- Standard LED
- 220 Ω to 330 Ω resistor
- Breadboard
- Jumper wires

## Example wiring

One example is:

```text
Raspberry Pi Pin 11 / GPIO17
        |
      resistor
        |
       LED
        |
Raspberry Pi Pin 6 / GND
```

The resistor is required to limit current through the LED.

### Important

LEDs have polarity.

The longer leg is normally the **anode (+)**.

The shorter leg is normally the **cathode (-)**.

Connect the GPIO side through the resistor to the LED anode, and connect the LED cathode to ground.

Never connect an LED directly between a GPIO pin and ground without a suitable resistor.

---

## Build the project in MoonPi

Open:

```text
Starters
```

and look for an LED or blinking-output starter project.

A typical blinking project uses:

```text
GPIO17 / Physical Pin 11
GND / Physical Pin 6
```

with an **Interval** node controlling the output.

After opening or building the project:

1. Check the wiring.
2. Click **Validate design**.
3. Click **Apply**.
4. Click **Run**.

The LED should begin changing state according to the interval.

Press **Stop** when finished.

---

# Simulation mode

MoonPi also has a simulation mode.

Simulation mode lets you experiment with the editor and logic without controlling real GPIO hardware.

Start it with:

```bash
./build-pi/moonpi --simulation --web frontend/dist
```

The interface should display:

```text
SIMULATION MODE
```

Simulation mode is useful for:

- learning how MoonPi works
- creating projects before wiring hardware
- testing logic
- experimenting without activating GPIO pins

Simulation does **not** control real Raspberry Pi pins.

---

# Useful commands

Show MoonPi's command-line options:

```bash
./build-pi/moonpi --help
```

MoonPi 0.8.0 supports options including:

```text
--simulation
--hardware
--gpio-chip
--bind
--port
--resources
--schemas
--web
--data
--config
--dry-run
--doctor
--validate-components
```

Run hardware mode:

```bash
./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist
```

Run simulation mode:

```bash
./build-pi/moonpi --simulation --web frontend/dist
```

Run on a different port:

```bash
./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist --port 8081
```

---

# Troubleshooting

## The browser says `Asset not found; build the frontend`

Make sure you started MoonPi with:

```bash
--web frontend/dist
```

The full hardware command is:

```bash
./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist
```

Also check that this file exists:

```bash
ls frontend/dist/index.html
```

---

## The interface says `SIMULATION MODE`

Make sure you used:

```bash
--hardware
```

not:

```bash
--simulation
```

Use:

```bash
./build-pi/moonpi --hardware --gpio-chip /dev/gpiochip0 --web frontend/dist
```

---

## MoonPi cannot access GPIO

Check:

```bash
ls -l /dev/gpiochip*
```

and:

```bash
groups
```

Your user should have access to the `gpio` group.

---

## The web page does not open

Make sure MoonPi is still running in the terminal.

Then try:

```text
http://localhost:8080
```

If connecting from another computer, find the Pi's IP address:

```bash
hostname -I
```

and use:

```text
http://PI-IP-ADDRESS:8080
```

---

## GPIO pin does not respond

Check all of the following:

- MoonPi says **HARDWARE MODE**
- the project has been validated
- the design has been applied
- the project is running
- the correct physical GPIO pin is connected
- the component has a ground connection
- the GPIO pin is not already being used by another program
- your wiring is correct
- the component is compatible with Raspberry Pi 3.3 V GPIO

---

# Electrical safety

The Raspberry Pi GPIO header uses **3.3 V logic**.

Do not connect 5 V signals directly to GPIO input pins.

Do not power motors, relays, large LEDs, solenoids, or other high-current devices directly from a GPIO pin.

Use the correct driver circuit, transistor, MOSFET, relay module, resistor, or other interface hardware for the device you are controlling.

When unsure, disconnect power and verify the circuit before running it.

MoonPi includes software safety checks, but software cannot protect the Raspberry Pi from every incorrectly wired circuit.

---

# Project files

MoonPi projects can be saved, loaded, imported, and exported from the interface.

When experimenting with an important project, export a copy before making major changes.

---

# Beta software

MoonPi 0.8.0 is a **beta release**.

That means:

- features may change
- bugs may still exist
- hardware support is still being expanded
- project formats may change before version 1.0
- additional testing is needed

If you find a bug, please report it through the project's GitHub Issues page and include:

- what you were trying to do
- what you expected to happen
- what actually happened
- your Raspberry Pi model
- your operating system
- any error message shown by MoonPi

---

# Contributing

Contributions, bug reports, documentation improvements, testing, and feature ideas are welcome.

If you modify MoonPi, please test your changes carefully before using them with physical hardware.

---

# Author

MoonPi was originally created and is maintained by:

**17ofSeptember**

Website:  
https://www.awrynetwork.com/links

Email:  
awrynetwork@gmail.com

---

# License

MoonPi is licensed under the **BSD 3-Clause License**.

You may use, modify, redistribute, and build upon MoonPi under the terms of that license.

See the [`LICENSE`](LICENSE) file for the complete license text.

---

## Disclaimer

MoonPi controls physical electronic hardware. Incorrect wiring or unsuitable components can damage a Raspberry Pi, connected equipment, or other electronics.

The software is provided without warranty. Always verify your wiring and component specifications before applying power.
</div>
