# Custom pin scripts and expanded components (0.6)

Add **Custom pin script** from the Scripting category. Connect `signal` to an available GPIO and `ground` to GND. Select the node and edit **Lua script**. Validate checks syntax without executing it; Apply and Run activate the edited version. Projects save and export the source with the node.

The script executes once on Start and whenever its `run` Trigger or `input` Boolean receives an event. The inspector also has a **Trigger run** button. Each node has an independent Lua state; global variables persist between events and reset on Stop/Run. `event` is `start`, `run`, or `input`; `value` is false for Start, true for a Trigger, or the incoming Boolean.

```lua
-- Connect Interval.tick to this node's run input to blink.
if event == "start" then
  pin.write(false)
elseif event == "input" then
  pin.write(value)
else
  pin.write(not pin.read())
end
```

`pin.write(boolean)` stages the node's output; `pin.read()` returns that level (initially the current output). Only the last staged value is applied after successful execution. A sequence of writes inside one event does not create physical pulses. Use Interval, Delay or Sequence nodes for timing. The `value` output publishes the committed Boolean and `done` emits a Trigger after each successful event, including Start. See `examples/scripted-pin.moonpi.json` for a 500 ms blink.

Scripts control one digital output through Moon Pi's existing exclusive pin lease. They cannot select another pin, change pin mode, produce hardware PWM, or control arbitrary OS devices. For sensor-driven behavior, wire a digital sensor's `value` into `input`. For numeric sensor thresholds, use Compare first.

Lua 5.4.9 language expressions, functions, loops and tables are available. The exposed base functions are `assert`, `error`, `ipairs`, `pairs`, `next`, `select`, `tonumber`, `tostring`, and `type`. There are no imports, filesystem, network, shell, debug, coroutine, metatable, protected-call or standard-library APIs. There is no sleep function. Each event is limited to 50,000 VM instructions and each Lua state to 512 KiB; source is limited to 16,384 bytes. The runtime yields its lock between bounded event slices. These are application bounds, not a hard real-time guarantee.

A syntax error blocks Apply. An execution error (including infinite-loop or memory limits) stops the graph, discards staged writes, releases GPIO to LOW, and leaves a node diagnostic. Stop and Emergency Stop discard all Lua states. Scripts imported in a project execute only when you Apply and Run it.

## New component profiles

| Components | Supported behavior and wiring |
| --- | --- |
| Reed, limit, tilt, float and toggle switches | Passive dry contacts between signal and GND; pull-up, active LOW and debounce defaults. |
| PIR, IR obstacle, light threshold and capacitive touch | Digital detection from separately powered modules whose output has been verified to stay within 3.3 V. Configure active polarity to match the module. |
| Relay, active buzzer and MOSFET modules | On/off output into an externally powered, active-HIGH driver with 3.3 V input drawing at most 1 mA. LOW must mean off. |

Input ports `pressed` and `released` mean activated and deactivated for all these profiles. The generic module profiles do not identify a specific vendor model or validate external power wiring. Analog measurements, buzzer pitch, motor speed and active-LOW output modules are not supported. Never wire a bare load directly to GPIO. See the [Raspberry Pi GPIO documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio).

## Board reference

The canvas uses the user's `piIMAGES/3b+.jpg`, bundled as `web/images/raspberry-pi-reference.jpg`, with all 40 labeled interactive connections beneath it. The photo's silkscreen identifies Raspberry Pi 2 Model B V1.1; the configured hardware profile remains Pi 3 B+ and is visibly named above the image. Appearance does not change physical/BCM pin numbering or supported hardware.

## Dependency provenance

Lua source: [official 5.4.9 release](https://www.lua.org/ftp/), SHA-256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`, vendored under `third_party/lua-5.4.9`. The release includes its license in `licenses/lua.html`. Sandbox uses the [Lua allocator, protected execution and instruction hook APIs](https://www.lua.org/manual/5.4/manual.html). No additional interpreter installation is needed.
