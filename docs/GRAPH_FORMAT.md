# Portable project and API v1

`schemas/project.schema.json` defines `moonpi-project`, version 1. Projects contain
a UUID, name, board definition/node UUID/position, component nodes and edges.
Every node and edge uses a lowercase UUID. Nodes reference stable component IDs
and properties; unknown extension fields survive save/load. Resource identifiers
are board-local stable strings, not display labels.

Hardware edge: board UUID + `header.11` → component UUID + `signal`.
Logic edge: interval UUID + `tick` → component UUID + `toggle`.
Edges retain distinct `kind` values. The maximum graph is 128 components and 512
edges. Required hardware ports must be connected and exclusive GPIO use is checked.

REST routes: `GET /api/v1/state`, `/catalogue`, `/resources`, `/health` and
`POST /api/v1/commands`. Command schema is `schemas/command.schema.json`:

```json
{"version":1,"command":"Apply","expected_revision":2}
```

Commands: SetDesign (with project), Validate, Apply, Start, Stop, EmergencyStop,
ClearEmergency, Save, Load, InjectInput (node and Boolean value, simulation only),
and Trigger (node and Trigger-input port). Stale revisions return HTTP 409; Stop and EmergencyStop
remain effective from stale clients. Semantic validation errors are returned in
state with severity, stable code, message and affected subject UUID. Successful
commands return a complete state snapshot.

Authentication uses optional bearer credentials or browser sessions. See
SECURITY.md for `POST /api/v1/session`, `DELETE /api/v1/session`, cookie lifetime,
rate limits and the expanded health response. State also includes the latest
audit-write status under `audit`.

`/api/v1/events` is WebSocket telemetry. It sends version-1 StateSnapshot events
with sequence, timestamp_ms and full state. Client may send only
`{"version":1,"type":"Resync"}`; mutations use the REST command contract.
Reconnect by fetching state/catalogue and subscribing again. The browser preserves
unsent design edits; stale edits are rejected on submission, not silently merged.

Migration policy: v1 is the first persisted format. Unsupported versions are
rejected before load/save, leaving original data intact. No fabricated pre-v1
migration is provided. Add explicit adjacent-version migrations and fixtures when
v2 is introduced. This remains an extension point rather than completed migration
support across multiple released formats.
