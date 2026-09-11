// Maintainer tool: regenerates the initial version-1 schemas and data.
import fs from 'node:fs';
const write = (p, v) => {
  fs.mkdirSync(p.slice(0, p.lastIndexOf('/')), { recursive: true });
  fs.writeFileSync(p, JSON.stringify(v, null, 2) + '\n');
};
const str = { type: 'string', minLength: 1, maxLength: 256 };
const uuid = {
  type: 'string',
  pattern: '^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$',
};
const obj = (properties, required = Object.keys(properties), additionalProperties = false) => ({
  type: 'object',
  properties,
  required,
  additionalProperties,
});
const arr = (items, maxItems = 512) => ({ type: 'array', items, maxItems });
const num = (minimum, maximum) => ({ type: 'number', minimum, maximum });
const en = (...values) => ({ enum: values });
const version = { const: 1 };
const port = obj({
  id: str,
  type: en(
    'Boolean',
    'Integer',
    'Float',
    'String',
    'Bytes',
    'Event',
    'Trigger',
    'Timestamp',
    'GPIO',
    'PWM',
    'I2C',
    'SPI',
    'UART',
    'JSON',
  ),
  direction: en('input', 'output'),
});
const connection = obj({
  id: str,
  role: en(
    'gpio_output',
    'gpio_input',
    'ground',
    'power',
    'i2c_sda',
    'i2c_scl',
    'spi_mosi',
    'spi_miso',
    'spi_clock',
    'spi_cs',
    'uart_tx',
    'uart_rx',
    'pwm',
    'onewire',
  ),
  required: { type: 'boolean' },
  voltage: num(0, 48),
});
const property = obj(
  {
    type: en('integer', 'number', 'boolean', 'string'),
    default: {},
    title: str,
    minimum: { type: 'number' },
    maximum: { type: 'number' },
    enum: { type: 'array' },
    description: { type: 'string' },
  },
  ['type', 'default', 'title'],
);
const component = obj(
  {
    schema: { const: 'moonpi-component' },
    schema_version: version,
    revision: { type: 'integer', minimum: 1 },
    id: str,
    name: str,
    category: str,
    description: str,
    aliases: arr(str),
    driver: obj({ id: str, api_version: version }),
    connections: arr(connection),
    ports: arr(port),
    properties: { type: 'object', additionalProperties: property },
    electrical: obj({
      logic_voltage: num(0, 48),
      max_current_ma: num(0, 5000),
      requires_level_shifter: { type: 'boolean' },
      requires_resistor: { type: 'boolean' },
      safe_state: { type: 'boolean' },
      notes: { type: 'string' },
    }),
    documentation: str,
    extensions: { type: 'object' },
  },
  [
    'schema',
    'schema_version',
    'revision',
    'id',
    'name',
    'category',
    'description',
    'aliases',
    'driver',
    'connections',
    'ports',
    'properties',
    'electrical',
    'documentation',
  ],
);
const pin = obj({
  id: str,
  physical: { type: 'integer', minimum: 1, maximum: 256 },
  bcm: { type: ['integer', 'null'], minimum: 0, maximum: 255 },
  label: str,
  kind: en('gpio', 'ground', 'power', 'reserved'),
  voltage: num(0, 48),
  capabilities: arr(str),
  reserved: { type: 'boolean' },
});
const board = obj({
  schema: { const: 'moonpi-board' },
  schema_version: version,
  id: str,
  name: str,
  logic_voltage: num(0, 48),
  max_gpio_current_ma: num(0, 100),
  max_total_current_ma: num(0, 1000),
  pins: arr(pin, 256),
  documentation: str,
});
const node = obj(
  {
    id: uuid,
    component: str,
    position: obj({ x: num(-100000, 100000), y: num(-100000, 100000) }),
    properties: { type: 'object' },
    extensions: { type: 'object' },
  },
  ['id', 'component', 'position', 'properties'],
  true,
);
const edge = obj(
  {
    id: uuid,
    kind: en('hardware', 'logic'),
    source: uuid,
    source_port: str,
    target: uuid,
    target_port: str,
  },
  undefined,
  true,
);
const project = obj(
  {
    format: { const: 'moonpi-project' },
    version,
    id: uuid,
    name: str,
    board: obj({
      definition: str,
      node_id: uuid,
      position: obj({ x: num(-100000, 100000), y: num(-100000, 100000) }),
    }),
    nodes: arr(node, 128),
    edges: arr(edge, 512),
    extensions: { type: 'object' },
  },
  ['format', 'version', 'id', 'name', 'board', 'nodes', 'edges'],
  true,
);
const command = obj(
  {
    version,
    command: en(
      'SetDesign',
      'Validate',
      'Apply',
      'Start',
      'Stop',
      'EmergencyStop',
      'ClearEmergency',
      'Save',
      'Load',
    ),
    expected_revision: { type: 'integer', minimum: 0 },
    project,
  },
  ['version', 'command', 'expected_revision'],
);
for (const [name, schema] of Object.entries({ component, board, project, command }))
  write(`schemas/${name}.schema.json`, {
    $schema: 'http://json-schema.org/draft-07/schema#',
    ...schema,
  });
write('schemas/config.schema.json', {
  $schema: 'http://json-schema.org/draft-07/schema#',
  ...obj(
    {
      version,
      bind: str,
      port: { type: 'integer', minimum: 1, maximum: 65535 },
      resources: str,
      schemas: str,
      web: str,
      data: str,
      mode: { const: 'simulation' },
    },
    ['version'],
  ),
});
write('schemas/event.schema.json', {
  $schema: 'http://json-schema.org/draft-07/schema#',
  ...obj({
    version,
    type: en('StateSnapshot'),
    sequence: { type: 'integer', minimum: 0 },
    timestamp_ms: { type: 'integer' },
    state: { type: 'object' },
  }),
});
const mapping = [
  null,
  null,
  2,
  null,
  3,
  null,
  4,
  14,
  null,
  15,
  17,
  18,
  27,
  null,
  22,
  23,
  null,
  24,
  10,
  null,
  9,
  25,
  11,
  8,
  null,
  7,
  0,
  1,
  5,
  null,
  6,
  12,
  13,
  null,
  19,
  16,
  26,
  20,
  null,
  21,
];
const grounds = [6, 9, 14, 20, 25, 30, 34, 39];
const powers = { 1: 3.3, 2: 5, 4: 5, 17: 3.3 };
const functions = {
  2: ['i2c1.sda'],
  3: ['i2c1.scl'],
  4: ['onewire'],
  7: ['spi0.cs1'],
  8: ['spi0.cs0'],
  9: ['spi0.miso'],
  10: ['spi0.mosi'],
  11: ['spi0.clock'],
  12: ['pwm0'],
  13: ['pwm1'],
  14: ['uart0.tx'],
  15: ['uart0.rx'],
  18: ['pwm0'],
  19: ['pwm1'],
};
write('resources/boards/raspberry-pi-3b-plus.json', {
  schema: 'moonpi-board',
  schema_version: 1,
  id: 'moonpi.board.rpi3b-plus',
  name: 'Raspberry Pi 3 B+',
  logic_voltage: 3.3,
  max_gpio_current_ma: 8,
  max_total_current_ma: 40,
  documentation: 'https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio',
  pins: mapping.map((bcm, i) => {
    const physical = i + 1;
    const kind = grounds.includes(physical)
      ? 'ground'
      : powers[physical]
        ? 'power'
        : physical === 27 || physical === 28
          ? 'reserved'
          : 'gpio';
    return {
      id: `header.${physical}`,
      physical,
      bcm,
      label:
        kind === 'ground'
          ? 'GND'
          : kind === 'power'
            ? `${powers[physical]}V`
            : kind === 'reserved'
              ? `ID ${bcm === 0 ? 'SDA' : 'SCL'}`
              : `GPIO${bcm}`,
      kind,
      voltage: kind === 'ground' ? 0 : powers[physical] || 3.3,
      capabilities: kind === 'gpio' ? ['gpio_input', 'gpio_output', ...(functions[bcm] || [])] : [],
      reserved: kind === 'reserved',
    };
  }),
});
const electrical = {
  logic_voltage: 3.3,
  max_current_ma: 0,
  requires_level_shifter: false,
  requires_resistor: false,
  safe_state: false,
  notes: 'Simulation first. Verify the exact physical circuit before hardware use.',
};
const base = (id, name, driver, category) => ({
  schema: 'moonpi-component',
  schema_version: 1,
  revision: 1,
  id: `moonpi.${id}`,
  name,
  category,
  description: name,
  aliases: [],
  driver: { id: `moonpi.driver.${driver}`, api_version: 1 },
  connections: [],
  ports: [],
  properties: {},
  electrical: { ...electrical },
  documentation: 'https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio',
});
const led = base('led', 'LED', 'gpio_output', 'Basic electronics');
led.description = 'Current-limited LED with explicit GPIO and ground wiring.';
led.connections = [
  { id: 'signal', role: 'gpio_output', required: true, voltage: 3.3 },
  { id: 'ground', role: 'ground', required: true, voltage: 0 },
];
led.ports = [
  { id: 'toggle', type: 'Trigger', direction: 'input' },
  { id: 'set', type: 'Boolean', direction: 'input' },
];
led.electrical = {
  ...electrical,
  max_current_ma: 5,
  requires_resistor: true,
  notes: 'Use a series resistor. Current validation conservatively assumes zero LED forward drop.',
};
led.properties = {
  resistor_ohms: {
    type: 'integer',
    title: 'Series resistor (ohms)',
    default: 1000,
    minimum: 680,
    maximum: 100000,
  },
};
write('resources/components/basic/led.json', led);
const out = structuredClone(led);
out.id = 'moonpi.digital-output';
out.name = 'Digital output';
out.description = '3.3 V logic output for a low-current input stage; never directly drive a motor.';
out.electrical.requires_resistor = false;
out.electrical.max_current_ma = 1;
out.properties = {};
write('resources/components/basic/digital-output.json', out);
const timer = base('interval', 'Interval', 'interval', 'Timing');
timer.description = 'Native monotonic interval; skips missed ticks and starts only on Run.';
timer.ports = [{ id: 'tick', type: 'Trigger', direction: 'output' }];
timer.properties = {
  interval_ms: {
    type: 'integer',
    title: 'Interval (ms)',
    default: 500,
    minimum: 50,
    maximum: 86400000,
  },
};
write('resources/components/virtual/interval.json', timer);
const projectExample = {
  format: 'moonpi-project',
  version: 1,
  id: '10000000-0000-4000-8000-000000000001',
  name: 'LED Blink',
  board: {
    definition: 'moonpi.board.rpi3b-plus',
    node_id: '10000000-0000-4000-8000-000000000002',
    position: { x: 80, y: 60 },
  },
  nodes: [
    {
      id: '10000000-0000-4000-8000-000000000003',
      component: led.id,
      position: { x: 640, y: 180 },
      properties: { resistor_ohms: 1000 },
    },
    {
      id: '10000000-0000-4000-8000-000000000004',
      component: timer.id,
      position: { x: 640, y: 420 },
      properties: { interval_ms: 150 },
    },
  ],
  edges: [],
};
const ed = (n, kind, source, sp, target, tp) => ({
  id: `20000000-0000-4000-8000-${String(n).padStart(12, '0')}`,
  kind,
  source: projectExample.nodes[source]?.id || projectExample.board.node_id,
  source_port: sp,
  target: projectExample.nodes[target]?.id || projectExample.board.node_id,
  target_port: tp,
});
projectExample.edges = [
  ed(1, 'hardware', 2, 'header.11', 0, 'signal'),
  ed(2, 'hardware', 2, 'header.6', 0, 'ground'),
  ed(3, 'logic', 1, 'tick', 0, 'toggle'),
];
write('examples/led-blink.moonpi.json', projectExample);
