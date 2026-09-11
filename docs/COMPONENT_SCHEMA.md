# Component Definition Standard v1

Canonical JSON Schema: `schemas/component.schema.json` (Draft 7). Every file is
validated by the C++ JSON Schema validator before registration. Invalid files are
skipped with filename and schema-path diagnostics. Duplicate IDs are rejected.

Required fields: `schema: moonpi-component`, `schema_version: 1`, positive
`revision`, namespaced `id`, `name`, `category`, `description`, `aliases`,
`documentation`, `driver`, `connections`, `ports`, `properties`, `electrical`.
`extensions` is an optional object for vendor metadata. Definition changes are
explicit; startup never downloads updates. Schema versions are separate from
definition revisions and driver API versions.

`driver` specifies a namespaced ID and `api_version: 1`. Compiled driver resolution
is exposed as `driver_available`. Missing drivers remain visible and cannot run.
Current drivers are `moonpi.driver.gpio_output` and `moonpi.driver.interval`.
The LED and digital-output profiles reuse the output driver. Adding a definition
does not require a React change. No public shared-library driver ABI is offered.

`connections` describe hardware requirements: stable port ID, role, required flag,
voltage. A component never specifies Raspberry Pi physical pin numbers. Ground
and power are wiring roles, never software output leases. Bus role names are
reserved by the schema; bus execution is unavailable in this first slice.

`ports` declare ID, input/output direction and type. Defined types include Boolean,
Integer, Float, String, Bytes, Event, Trigger, Timestamp, GPIO, PWM, I2C, SPI, UART,
JSON. Connections require identical types. Declaring a type alone does not add a
runtime implementation.

`properties` is a map of inspector fields with `type`, `title`, `default`, optional
numeric bounds, enum and description. The same constraints validate effective
backend settings. Unknown properties are rejected. Defaults are validated at
registry load. Runtime uses merged defaults and project overrides.

`electrical` requires logic voltage, conservative maximum current, level-shifter
and resistor requirements, safe state, and notes. The initial driver permits only
LOW safe state. Unsupported level shifting fails validation. LED resistor checks
conservatively assume zero diode forward drop. Wiring metadata cannot establish
what is physically connected; real circuits still need verification.

## Annotated LED example

The executable example is `resources/components/basic/led.json`. JSON comments
are not valid canonical JSON; annotations below explain the corresponding fields:

```jsonc
{
  "schema": "moonpi-component", // identifies the document type
  "schema_version": 1,          // schema compatibility
  "revision": 1,                // metadata revision
  "id": "moonpi.led",           // stable identifier; never the display name
  "driver": {"id": "moonpi.driver.gpio_output", "api_version": 1},
  "connections": [
    {"id":"signal","role":"gpio_output","required":true,"voltage":3.3},
    {"id":"ground","role":"ground","required":true,"voltage":0}
  ],
  "ports": [{"id":"toggle","type":"Trigger","direction":"input"}]
  // See the complete canonical file for identity, properties and safety fields.
}
```

## User definitions and validation

Put custom definitions in `user-data/components/` with `user.` IDs. They never
silently override built-ins and are marked user-supplied. Search includes metadata
and aliases. Run `moonpi --validate-components` from a directory containing
resources/schemas, or pass explicit `--resources` and `--schemas` directories.

For a single file, use `moonpi component validate FILE` or
`moonpi component inspect FILE --schemas schemas`. Both validate schema and the
compiled driver's contract; inspect also prints the interpreted definition.

The extensive representative component pack and protocol constraints requested by
the specification remain on the implementation checklist. Unsupported physical
products are not advertised as working components.
