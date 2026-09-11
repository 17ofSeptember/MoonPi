# Automation and calendar schedules

## Timed sequences and manual testing

Import `examples/led-sequence.moonpi.json`, Apply, Run, select Sequence, then click
**Trigger start**. Four one-second steps set and reset a latch to blink the LED
twice. **Trigger cancel** cancels pending steps and resets the example's latch.
The canvas displays the active step, completion or cancellation.

Sequence supports one to four steps with individually configured durations from
1 ms to 24 hours. Starting emits step_1 immediately; each step's duration is the
wait before the next step, and done fires after the last duration. The step
output carries a one-based Integer; active is Boolean. Connect done to another
sequence's start to build longer routines. Several distinct Trigger outputs can
feed one Trigger input. Value inputs still accept only one writer, and duplicate
connections are rejected.

Start while active either does nothing (IGNORE, the default) or restarts step 1
(RESTART). Restart replaces the pending timer. Cancel emits cancelled only when
active; completed and cancelled sequences can start again. Scheduling is
monotonic: a delayed scheduler waits the next full duration, without a burst of
catch-up steps. This is software timing, not a hard real-time guarantee.

Cancel cannot undo effects already performed by earlier steps. Wire cancelled
to explicit reset actions, as the example does. Stop/E-STOP cancel all pending
steps and release GPIO outputs. Sequences start idle after a service restart or
a new Run; they do not resume midway through a routine.

All supported Trigger input ports have **Manual triggers** buttons in the
inspector. They require a running, applied design and enqueue the same typed
events as graph connections. Buttons are disabled while local edits are pending.
The service also rejects stale designs, unknown nodes and non-Trigger ports.
Manual trigger buttons can operate hardware in hardware mode just like the
corresponding graph event; they are not limited to simulation.

Apply validates a design snapshot. Run starts that snapshot explicitly; opening a
browser, importing a project and restarting the service never start automation.
Stop cancels pending timers and events. Emergency stop also latches until cleared.

Interval and Delay use monotonic time. GPIO inputs are polled every 10 ms and
accept a change only after the configured debounce time. This is suitable for
buttons and slow digital inputs; it does not capture high-frequency pulses.
In simulation, select an applied input while running and use Press / activate or
Release / deactivate. The commands respect the component's active-low setting.

Logic components include Boolean and numeric constants, comparisons, arithmetic,
Boolean operations, counters, integer-to-float conversion, numeric displays,
delays, latches and conditional triggers. Values travel through typed ports in a
bounded native event queue. Runtime arithmetic or queue failures stop execution
and release outputs. Counters and latches reset on Run.

## Calendar schedule

The expression has five numeric UTC fields: `minute hour day month weekday`.
Supported syntax includes `*`, comma lists, inclusive ranges and steps such as
`*/15`. Weekdays are 0 (Sunday) through 6 (Saturday). When both day-of-month and
weekday are restricted, either matching field satisfies the day condition.
Example: `*/15 9-17 * * 1-5` triggers every quarter hour during UTC business hours.
Names, seconds fields and local time zones are not supported.

Run skips its starting minute. Missed minutes are not replayed. After a forward
clock jump, at most the current matching minute fires; backward changes cannot
repeat an already checkpointed minute.

## One-time schedule

Enter an exact UTC timestamp, for example `2030-01-01T12:00:00Z` (supported years
1970–2200). `SKIP_MISSED` skips a time that passed before Run.
`RUN_ONCE_AFTER_START` explicitly allows one catch-up event on Run.

The service atomically writes `user-data/scheduler.json` before releasing calendar
events. Checkpoints contain node identity, settings and the last time slot. The
same node with unchanged settings cannot replay its event after Stop/Run or a
service restart. Changing settings or pasting a new node creates a new schedule.
Preserve scheduler.json when moving an existing installation if this protection
must continue; the exported project alone does not include execution history.

This is at-most-once delivery: a crash after checkpointing but before downstream
execution can lose an action. It does not guarantee exactly-once actuation.
Corrupt checkpoints block scheduled graphs while allowing manual graphs. Storage
failures stop the runtime before emitting the affected event. Do not delete a
checkpoint to recover without reviewing which schedules could fire again.

Checkpoint history is limited to 1,024 node identities. Reaching the limit stops
new schedule recording rather than silently discarding replay protection.
