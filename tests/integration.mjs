import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
const require = createRequire(new URL('../frontend/package.json', import.meta.url));
const { chromium } = require('@playwright/test');
const root = path.resolve(import.meta.dirname, '..');
const packageRoot = process.env.MOONPI_PACKAGE && path.resolve(process.env.MOONPI_PACKAGE);
const executable = packageRoot
  ? path.join(packageRoot, 'moonpi.exe')
  : process.env.MOONPI_EXE || path.join(root, 'build-native', 'Debug', 'moonpi.exe');
if (packageRoot) {
  const manifest = JSON.parse(
    fs.readFileSync(path.join(packageRoot, 'manifest.json'), 'utf8').replace(/^\uFEFF/, ''),
  );
  const names = new Set();
  for (const entry of manifest) {
    const target = path.resolve(packageRoot, entry.path);
    assert.ok(target.startsWith(packageRoot + path.sep));
    assert.ok(!names.has(entry.path));
    names.add(entry.path);
    const contents = fs.readFileSync(target);
    assert.equal(contents.length, entry.bytes);
    assert.equal(createHash('sha256').update(contents).digest('hex'), entry.sha256);
  }
  const bundled = fs
    .readdirSync(packageRoot, { recursive: true, withFileTypes: true })
    .filter((e) => e.isFile())
    .map((e) => path.relative(packageRoot, path.join(e.parentPath, e.name)).replaceAll('\\', '/'));
  assert.deepEqual(new Set(bundled), new Set([...names, 'manifest.json']));
  assert.ok(!bundled.some((name) => name.startsWith('user-data/')));
  console.log('PASS package manifest, SHA-256 file integrity and clean contents');
}
const data = path.join(root, 'build-win', 'integration-' + Date.now());
fs.mkdirSync(data, { recursive: true });
const doctorData = path.join(data, 'doctor-must-not-create');
const doctorArgs = [
  '--doctor',
  '--simulation',
  '--web',
  packageRoot ? 'web' : 'frontend/dist',
  '--data',
  doctorData,
];
const doctor = spawnSync(executable, doctorArgs, {
  cwd: packageRoot || root,
  encoding: 'utf8',
  windowsHide: true,
  timeout: 10000,
});
assert.equal(doctor.status, 0, doctor.stderr);
assert.equal(JSON.parse(doctor.stdout).read_only, true);
assert.equal(JSON.parse(doctor.stdout).ready, true);
assert.equal(fs.existsSync(doctorData), false);
const missingWebArgs = [...doctorArgs];
missingWebArgs[3] = path.join(data, 'missing-web');
const missingWeb = spawnSync(executable, missingWebArgs, {
  cwd: packageRoot || root,
  encoding: 'utf8',
  windowsHide: true,
  timeout: 10000,
});
assert.equal(missingWeb.status, 1);
assert.equal(JSON.parse(missingWeb.stdout).ready, false);
assert.equal(fs.existsSync(doctorData), false);
console.log('PASS read-only installation CLI and missing-frontend diagnostics');
const base = 'http://127.0.0.1:18080';
let child;
let logs = '';
async function start() {
  child = spawn(
    executable,
    [
      '--simulation',
      '--bind',
      '127.0.0.1',
      '--port',
      '18080',
      '--web',
      packageRoot ? 'web' : 'frontend/dist',
      '--data',
      data,
    ],
    { cwd: packageRoot || root, windowsHide: true },
  );
  child.stdout.on('data', (x) => (logs += x));
  child.stderr.on('data', (x) => (logs += x));
  child.on('error', (e) => (logs += e));
  for (let i = 0; i < 100; i++) {
    try {
      const r = await fetch(base + '/api/v1/state');
      if (r.ok) return await r.json();
    } catch {}
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error('Server failed to start: ' + logs);
}
async function stop() {
  if (child && child.exitCode === null) {
    child.kill();
    await new Promise((r) => child.once('exit', r));
  }
}
let browser;
try {
  let state = await start();
  assert.equal(state.runtime.running, false);
  const api = async (command, project, revision = state.design_revision) => {
    const r = await fetch(base + '/api/v1/commands', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        version: 1,
        command,
        expected_revision: revision,
        ...(project ? { project } : {}),
      }),
    });
    const result = await r.json();
    if (r.ok) state = result;
    return { r, result };
  };
  const sample = JSON.parse(fs.readFileSync(path.join(root, 'examples/led-blink.moonpi.json')));
  assert.equal((await api('SetDesign', sample)).r.status, 200);
  assert.equal(state.validation.valid, true);
  assert.equal((await api('Apply')).r.status, 200);
  assert.equal((await api('Start')).r.status, 200);
  await new Promise((r) => setTimeout(r, 450));
  state = await (await fetch(base + '/api/v1/state')).json();
  assert.ok(state.runtime.event_count >= 2);
  assert.equal((await api('Save')).r.status, 200);
  assert.equal((await api('Apply', undefined, 1)).r.status, 409);
  const badOrigin = await fetch(base + '/api/v1/commands', {
    method: 'POST',
    headers: { Origin: 'http://evil.invalid', 'Content-Type': 'application/json' },
    body: JSON.stringify({ version: 1, command: 'Stop', expected_revision: 0 }),
  });
  assert.equal(badOrigin.status, 403);
  assert.ok([403, 404].includes((await fetch(base + '/assets/%2e%2e%2fpackage.json')).status));
  await api('EmergencyStop', undefined, 0);
  assert.equal(state.runtime.emergency, true);
  assert.equal(state.runtime.hardware.length, 0);
  assert.equal((await api('Start')).r.status, 400);
  await api('ClearEmergency');
  await api('Stop');
  console.log('PASS REST execution, validation, revision conflicts, safety and persistence');
  // Crash/restart deliberately tests recovery. No real hardware is used.
  await stop();
  state = await start();
  assert.equal(state.project.id, sample.id);
  assert.equal(state.runtime.running, false);
  assert.equal(state.runtime.hardware.length, 0);
  assert.equal(state.recovery, true);
  console.log('PASS process restart preserves graph and starts safely');
  browser = await chromium.launch({
    headless: true,
    channel: process.env.MOONPI_BROWSER || 'msedge',
  });
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto(base);
  await page.getByText('Connected', { exact: false }).first().waitFor();
  await page.locator('.board').waitFor();
  assert.equal(await page.locator('.pin').count(), 40);
  await page.locator('.board-photo img').waitFor();
  await page.waitForFunction(
    () => document.querySelector('.board-photo img')?.naturalWidth === 960,
  );
  await page.getByRole('button', { name: 'Apply', exact: true }).click();
  await page.getByRole('button', { name: '▶ Run', exact: true }).click();
  await page.locator('.component-node.on').first().waitFor({ timeout: 5000 });
  assert.ok((await page.locator('.canvas-status').innerText()).includes('RUNNING'));
  await page.screenshot({ path: path.join(data, 'running.png'), fullPage: true });
  await page.getByRole('button', { name: 'E-STOP', exact: true }).click();
  await page.getByText('Emergency stop latched.', { exact: false }).waitFor();
  assert.equal(errors.length, 0, errors.join('\n'));
  console.log('PASS browser board rendering, WebSocket live GPIO/LED and emergency stop');
  // Create the exact slice through the visual editor starting from an empty design.
  await page.getByRole('button', { name: 'Clear emergency stop', exact: true }).click();
  state = await (await fetch(base + '/api/v1/state')).json();
  const blank = structuredClone(sample);
  blank.nodes = [];
  blank.edges = [];
  blank.name = 'Browser-built blink';
  await api('SetDesign', blank);
  await page.reload();
  await page.locator('.board').waitFor();
  await page.getByText('Connected', { exact: false }).first().waitFor();
  await page
    .getByRole('button', { name: 'LED Drag or click to add +', exact: false })
    .dragTo(page.locator('.canvas'), { targetPosition: { x: 850, y: 150 } });
  await page.getByRole('button', { name: 'Interval Drag or click to add +', exact: false }).click();
  const connect = async (source, target) => {
    const a = await source.boundingBox(),
      b = await target.boundingBox();
    assert.ok(a && b);
    await page.mouse.move(a.x + a.width / 2, a.y + a.height / 2);
    await page.mouse.down();
    await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2, { steps: 15 });
    await page.mouse.up();
  };
  const gpio = page.locator('.board [data-handleid="header.11"]'),
    ground = page.locator('.board [data-handleid="header.6"]');
  await connect(gpio, page.locator('.component-node [data-handleid="signal"]'));
  await connect(ground, page.locator('.component-node [data-handleid="ground"]'));
  await connect(
    page.locator('.component-node [data-handleid="tick"]'),
    page.locator('.component-node [data-handleid="toggle"]'),
  );
  await page.getByRole('button', { name: 'Apply', exact: true }).click();
  await page.waitForFunction(() =>
    document.querySelector('.canvas-status')?.textContent?.includes('Current design applied'),
  );
  await page.getByRole('button', { name: '▶ Run', exact: true }).click();
  await page.locator('.component-node.on').waitFor();
  await page.getByRole('button', { name: 'Save', exact: true }).click();
  await page.waitForTimeout(250);
  state = await (await fetch(base + '/api/v1/state')).json();
  assert.equal(state.project.nodes.length, 2);
  assert.equal(state.project.edges.length, 3);
  assert.equal(state.validation.valid, true);
  assert.ok(state.runtime.event_count > 0);
  await page.close();
  await new Promise((r) => setTimeout(r, 600));
  const detached = await (await fetch(base + '/api/v1/state')).json();
  assert.ok(detached.runtime.event_count > state.runtime.event_count);
  await stop();
  state = await start();
  assert.equal(state.project.name, 'Browser-built blink');
  assert.equal(state.project.edges.length, 3);
  assert.equal(state.runtime.running, false);
  console.log('PASS visual wiring, native execution without browser, save and second restart');
  const editor = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  editor.setDefaultTimeout(10000);
  editor.on('pageerror', (e) => errors.push(e.message));
  await editor.goto(base);
  await editor.getByText('Connected', { exact: false }).first().waitFor();
  const led = () =>
    editor
      .locator('.component-node')
      .filter({ has: editor.locator('strong', { hasText: /^LED$/ }) });
  const interval = () =>
    editor
      .locator('.component-node')
      .filter({ has: editor.locator('strong', { hasText: /^Interval$/ }) });
  const counts = async (nodes, edges) => {
    await editor.waitForFunction(
      ([n, e]) =>
        document.querySelectorAll('.component-node').length === n &&
        document.querySelectorAll('.react-flow__edge').length === e,
      [nodes, edges],
    );
  };
  await counts(2, 3);
  // Separate the nodes so selection elevation cannot cover the other title.
  const intervalTitle = await interval().locator('strong').boundingBox();
  assert.ok(intervalTitle);
  await editor.mouse.move(intervalTitle.x + 20, intervalTitle.y + 5);
  await editor.mouse.down();
  await editor.mouse.move(intervalTitle.x + 20, intervalTitle.y + 225, { steps: 15 });
  await editor.mouse.up();
  await editor.getByRole('button', { name: 'Apply', exact: true }).click();
  await editor.getByRole('button', { name: '▶ Run', exact: true }).click();
  await editor.locator('.component-node.on').waitFor();
  await led().locator('strong').click();
  await editor.screenshot({ path: path.join(data, 'delete-controls.png'), fullPage: true });
  await editor.getByRole('button', { name: 'Delete component', exact: true }).click();
  await counts(1, 0);
  const runningDesign = await (await fetch(base + '/api/v1/state')).json();
  assert.equal(runningDesign.runtime.running, true);
  assert.equal(runningDesign.project.nodes.length, 2);
  assert.equal(runningDesign.runtime.hardware.length, 1);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  await editor.getByRole('button', { name: '■ Stop', exact: true }).click();
  await led().locator('strong').click();
  await editor
    .getByRole('button', { name: /^Unlink / })
    .first()
    .click();
  await counts(2, 2);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  // Click the actual wire hit area, without relying on an undocumented keyboard shortcut.
  const wirePath = editor
    .locator('.react-flow__edge.hardware .react-flow__edge-interaction')
    .first();
  const wirePoint = await wirePath.evaluate((wire) => {
    for (let fraction = 0.1; fraction < 0.95; fraction += 0.05) {
      const p = wire.getPointAtLength(wire.getTotalLength() * fraction);
      const screen = new DOMPoint(p.x, p.y).matrixTransform(wire.getScreenCTM());
      if (
        document.elementFromPoint(screen.x, screen.y)?.closest('.react-flow__edge') ===
        wire.closest('.react-flow__edge')
      )
        return { x: screen.x, y: screen.y };
    }
    return null;
  });
  assert.ok(wirePoint, 'Wire has an exposed clickable segment');
  await editor.mouse.click(wirePoint.x, wirePoint.y);
  await editor.getByRole('button', { name: 'Delete wire', exact: true }).waitFor();
  const oldSource = await editor.getByLabel('Wire source', { exact: true }).inputValue();
  const alternativeSource = await editor
    .getByLabel('Wire source', { exact: true })
    .locator('option')
    .evaluateAll(
      (options, current) => options.find((option) => option.value !== current).value,
      oldSource,
    );
  await editor.getByLabel('Wire source', { exact: true }).selectOption(alternativeSource);
  await editor.getByRole('button', { name: /Undo/ }).click();
  assert.equal(await editor.getByLabel('Wire source', { exact: true }).inputValue(), oldSource);
  const reconnectHandle = await editor
    .locator('.react-flow__edge.selected .react-flow__edgeupdater-source')
    .boundingBox();
  const nextPin = await editor.locator('.board [data-handleid="header.13"]').boundingBox();
  assert.ok(reconnectHandle && nextPin);
  await editor.mouse.move(
    reconnectHandle.x + reconnectHandle.width / 2,
    reconnectHandle.y + reconnectHandle.height / 2,
  );
  await editor.mouse.down();
  await editor.mouse.move(nextPin.x + nextPin.width / 2, nextPin.y + nextPin.height / 2, {
    steps: 12,
  });
  await editor.mouse.up();
  await editor.waitForFunction(() =>
    document.querySelector('[aria-label="Wire source"]')?.value.includes('header.13'),
  );
  await editor.getByRole('button', { name: /Undo/ }).click();
  assert.equal(await editor.getByLabel('Wire source', { exact: true }).inputValue(), oldSource);
  await editor.screenshot({ path: path.join(data, 'wire-inspector.png'), fullPage: true });
  await editor.getByRole('button', { name: 'Delete wire', exact: true }).click();
  await counts(2, 2);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  console.log(
    'PASS visible component Unlink, direct wire selection, endpoint editing and Delete wire with undo',
  );
  await led().locator('strong').click();
  await editor.keyboard.press('Delete');
  await counts(1, 0);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  await editor.getByRole('button', { name: /Redo/ }).click();
  await counts(1, 0);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  await led().locator('strong').click();
  await editor.keyboard.press('Backspace');
  await counts(1, 0);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  await editor.locator('.board-title strong').click();
  await editor.keyboard.press('Delete');
  assert.equal(await editor.locator('.board').count(), 1);
  await counts(2, 3);
  const projectName = editor.getByLabel('Project name', { exact: true });
  await projectName.focus();
  await editor.keyboard.press('End');
  await editor.keyboard.press('Backspace');
  await counts(2, 3);
  await editor.locator('.react-flow__edge').first().focus();
  await editor.keyboard.press('Enter');
  await editor.keyboard.press('Delete');
  await counts(2, 2);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  await led().locator('strong').click();
  await interval()
    .locator('strong')
    .click({ modifiers: ['Control'] });
  await editor.getByRole('button', { name: 'Delete selected', exact: true }).click();
  await counts(0, 0);
  assert.equal(await editor.locator('.board').count(), 1);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  await led().locator('strong').click();
  await interval()
    .locator('strong')
    .click({ modifiers: ['Control'] });
  await editor.keyboard.press('Control+c');
  await editor.keyboard.press('Control+v');
  await counts(4, 4);
  await editor.keyboard.press('Control+z');
  await counts(2, 3);
  await editor.keyboard.press('Control+Shift+z');
  await counts(4, 4);
  const copiedDownloadEvent = editor.waitForEvent('download');
  await editor.getByRole('button', { name: 'Export', exact: true }).click();
  await (await copiedDownloadEvent).saveAs(path.join(data, 'copied.moonpi.json'));
  const copied = JSON.parse(fs.readFileSync(path.join(data, 'copied.moonpi.json')));
  assert.equal(new Set(copied.nodes.map((n) => n.id)).size, 4);
  assert.equal(new Set(copied.edges.map((e) => e.id)).size, 4);
  assert.equal(copied.edges.filter((e) => e.kind === 'hardware').length, 2);
  const copiedIds = new Set(copied.nodes.slice(2).map((n) => n.id));
  assert.ok(copiedIds.has(copied.edges[3].source) && copiedIds.has(copied.edges[3].target));
  await projectName.focus();
  await editor.keyboard.press('Control+v');
  await counts(4, 4);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  console.log(
    'PASS subgraph copy/paste, fresh IDs, internal wires, atomic undo/redo and text focus',
  );
  const exportEditing = async (name) => {
    const event = editor.waitForEvent('download');
    await editor.getByRole('button', { name: 'Export', exact: true }).click();
    const filename = path.join(data, name + '.moonpi.json');
    await (await event).saveAs(filename);
    return JSON.parse(fs.readFileSync(filename));
  };
  await led().locator('strong').click();
  await editor.getByLabel('Component name', { exact: true }).fill('Status light');
  await editor
    .locator('.component-node strong')
    .filter({ hasText: /^Status light$/ })
    .waitFor();
  assert.ok((await exportEditing('renamed')).nodes.some((n) => n.label === 'Status light'));
  await editor.getByRole('button', { name: /Undo/ }).click();
  await led().locator('strong').click();
  await editor.keyboard.press('Control+a');
  await editor.keyboard.press('Control+x');
  await counts(0, 0);
  await editor.keyboard.press('Control+z');
  await counts(2, 3);
  await led().locator('strong').click();
  await interval()
    .locator('strong')
    .click({ modifiers: ['Control'] });
  const beforeMove = await exportEditing('before-group-move');
  await interval().locator('strong').click();
  await editor.keyboard.press('ArrowRight');
  const nudged = await exportEditing('keyboard-move');
  const intervalBefore = beforeMove.nodes.find((n) => n.component === 'moonpi.interval');
  assert.equal(
    nudged.nodes.find((n) => n.id === intervalBefore.id).position.x,
    intervalBefore.position.x + 16,
  );
  await editor.getByRole('button', { name: /Undo/ }).click();
  await led().locator('strong').click();
  await interval()
    .locator('strong')
    .click({ modifiers: ['Control'] });
  const groupTitle = await interval().locator('strong').boundingBox();
  await editor.mouse.move(groupTitle.x + 15, groupTitle.y + 5);
  await editor.mouse.down();
  await editor.mouse.move(groupTitle.x + 79, groupTitle.y + 69, { steps: 10 });
  await editor.mouse.up();
  const afterMove = await exportEditing('after-group-move');
  for (const node of beforeMove.nodes) {
    const moved = afterMove.nodes.find((n) => n.id === node.id);
    assert.notDeepEqual(moved.position, node.position, 'Every selected node movement is persisted');
  }
  await editor.getByRole('button', { name: /Undo/ }).click();
  assert.deepEqual((await exportEditing('undo-group-move')).nodes, beforeMove.nodes);
  await led().locator('strong').click();
  await interval()
    .locator('strong')
    .click({ modifiers: ['Control'] });
  await editor.locator('.edit-menu summary').click();
  await editor.getByRole('button', { name: 'Align top', exact: true }).click();
  const aligned = await exportEditing('aligned');
  assert.equal(aligned.nodes[0].position.y, aligned.nodes[1].position.y);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await led().locator('strong').click();
  await interval()
    .locator('strong')
    .click({ modifiers: ['Control'] });
  await editor.locator('.edit-menu summary').click();
  await editor.getByRole('button', { name: 'Duplicate selected', exact: true }).click();
  await counts(4, 4);
  await editor.getByRole('button', { name: /Undo/ }).click();
  await counts(2, 3);
  console.log(
    'PASS component names, select-all/cut, group drag persistence, alignment and group duplication',
  );
  await led().locator('strong').click();
  await editor.getByRole('button', { name: 'Delete component', exact: true }).click();
  await counts(1, 0);
  await editor.getByRole('button', { name: 'Apply', exact: true }).click();
  await editor.waitForFunction(() =>
    document.querySelector('.canvas-status')?.textContent?.includes('Current design applied'),
  );
  await editor.getByRole('button', { name: 'Save', exact: true }).click();
  await editor.waitForFunction(() => {
    const save = [...document.querySelectorAll('button')].find((b) => b.textContent === 'Save');
    return save && !save.disabled;
  });
  await editor.close();
  await stop();
  state = await start();
  assert.equal(state.project.nodes.length, 1);
  assert.equal(state.project.edges.length, 0);
  assert.equal(state.runtime.hardware.length, 0);
  assert.equal(errors.length, 0, errors.join('\n'));
  console.log(
    'PASS node deletion, attached wires, undo/redo, multi-selection, keyboard focus, board protection and persistence',
  );
  const advanced = await browser.newPage({ viewport: { width: 1700, height: 1100 } });
  advanced.setDefaultTimeout(15000);
  advanced.on('pageerror', (e) => errors.push(e.message));
  await advanced.goto(base);
  await advanced.getByText('Connected', { exact: false }).first().waitFor();
  await advanced
    .getByLabel('Import project', { exact: true })
    .setInputFiles(path.join(root, 'examples/bme280-alarm.moonpi.json'));
  await advanced
    .locator('.component-node strong')
    .filter({ hasText: /^BME280$/ })
    .waitFor();
  await advanced.getByRole('button', { name: 'Apply', exact: true }).click();
  await advanced.getByRole('button', { name: '▶ Run', exact: true }).click();
  await advanced.locator('.sensor-reading').filter({ hasText: '°C' }).waitFor();
  await advanced
    .locator('.component-node.on')
    .filter({ has: advanced.locator('strong', { hasText: /^LED$/ }) })
    .waitFor();
  await advanced.screenshot({ path: path.join(data, 'sensor-alarm.png'), fullPage: true });
  await advanced.getByRole('button', { name: '■ Stop', exact: true }).click();
  const downloadEvent = advanced.waitForEvent('download');
  await advanced.getByRole('button', { name: 'Export', exact: true }).click();
  const download = await downloadEvent;
  await download.saveAs(path.join(data, 'exported.moonpi.json'));
  assert.equal(
    JSON.parse(fs.readFileSync(path.join(data, 'exported.moonpi.json'))).name,
    'BME280 temperature alarm',
  );
  await advanced
    .getByLabel('Import project', { exact: true })
    .setInputFiles(path.join(root, 'examples/button-led.moonpi.json'));
  const buttonNode = advanced
    .locator('.component-node')
    .filter({ has: advanced.locator('strong', { hasText: /^Push button$/ }) });
  await buttonNode.waitFor();
  await advanced.getByRole('button', { name: 'Apply', exact: true }).click();
  await advanced.getByRole('button', { name: '▶ Run', exact: true }).click();
  await buttonNode.locator('strong').click();
  await advanced.getByRole('button', { name: 'Press / activate', exact: true }).click();
  await advanced
    .locator('.component-node.on')
    .filter({ has: advanced.locator('strong', { hasText: /^LED$/ }) })
    .waitFor();
  await advanced.getByRole('button', { name: 'Release / deactivate', exact: true }).click();
  await advanced.waitForTimeout(80);
  await advanced.getByRole('button', { name: 'Press / activate', exact: true }).click();
  await advanced.waitForFunction(() =>
    [...document.querySelectorAll('.component-node')].some(
      (n) => n.querySelector('strong')?.textContent === 'LED' && !n.classList.contains('on'),
    ),
  );
  await advanced.getByRole('button', { name: 'E-STOP', exact: true }).click();
  await advanced.getByRole('button', { name: 'Clear emergency stop', exact: true }).click();
  await advanced
    .getByLabel('Import project', { exact: true })
    .setInputFiles(path.join(root, 'examples/led-sequence.moonpi.json'));
  const sequenceNode = advanced
    .locator('.component-node')
    .filter({ has: advanced.locator('strong', { hasText: /^Sequence$/ }) });
  await sequenceNode.locator('strong').click();
  assert.equal(
    await advanced.getByRole('button', { name: 'Trigger start', exact: true }).isDisabled(),
    true,
  );
  await advanced.getByRole('button', { name: 'Apply', exact: true }).click();
  await advanced.getByRole('button', { name: '▶ Run', exact: true }).click();
  await advanced.getByRole('button', { name: 'Trigger start', exact: true }).click();
  await sequenceNode.getByText('Step 1', { exact: true }).waitFor();
  await advanced
    .locator('.component-node.on')
    .filter({ has: advanced.locator('strong', { hasText: /^LED$/ }) })
    .waitFor();
  await advanced.screenshot({ path: path.join(data, 'sequence-running.png'), fullPage: true });
  await advanced.getByRole('button', { name: 'Trigger cancel', exact: true }).click();
  await sequenceNode.getByText('CANCELLED', { exact: true }).waitFor();
  await advanced.waitForFunction(() =>
    [...document.querySelectorAll('.component-node')].some(
      (n) => n.querySelector('strong')?.textContent === 'LED' && !n.classList.contains('on'),
    ),
  );
  await advanced.getByRole('button', { name: 'Trigger start', exact: true }).click();
  await sequenceNode.getByText('COMPLETED', { exact: true }).waitFor();
  await advanced.getByRole('button', { name: '■ Stop', exact: true }).click();
  assert.equal(
    await advanced.getByRole('button', { name: 'Trigger start', exact: true }).isDisabled(),
    true,
  );
  console.log(
    'PASS manual sequence trigger, visible progress, cancel/reset, restart and completion',
  );
  await advanced
    .getByLabel('Import project', { exact: true })
    .setInputFiles(path.join(root, 'examples/scripted-pin.moonpi.json'));
  const scriptNode = advanced
    .locator('.component-node')
    .filter({ has: advanced.locator('strong', { hasText: /^Custom pin script$/ }) });
  await scriptNode.locator('strong').click();
  const scriptEditor = advanced.getByLabel('Lua script', { exact: true });
  assert.ok((await scriptEditor.inputValue()).includes('pin.write'));
  await scriptEditor.fill('if then');
  await advanced.getByRole('button', { name: 'Validate design', exact: true }).click();
  await advanced.getByText('script.invalid', { exact: false }).first().waitFor();
  const scriptSource =
    'count = (count or 0) + 1\nif event == "start" then pin.write(false) else pin.write(count % 2 == 0) end';
  await scriptEditor.fill(scriptSource);
  await advanced.getByRole('button', { name: 'Apply', exact: true }).click();
  await advanced.getByRole('button', { name: '▶ Run', exact: true }).click();
  await advanced.waitForFunction(() =>
    [...document.querySelectorAll('.component-node.on')].some(
      (n) => n.querySelector('strong')?.textContent === 'Custom pin script',
    ),
  );
  await advanced.getByRole('button', { name: 'Trigger run', exact: true }).click();
  await advanced.screenshot({ path: path.join(data, 'pin-script-board.png'), fullPage: true });
  await advanced.getByRole('button', { name: '■ Stop', exact: true }).click();
  assert.equal(
    await advanced.getByRole('button', { name: 'Trigger run', exact: true }).isDisabled(),
    true,
  );
  await advanced.waitForFunction(
    () =>
      ![...document.querySelectorAll('.component-node.on')].some(
        (n) => n.querySelector('strong')?.textContent === 'Custom pin script',
      ),
  );
  const scriptDownload = advanced.waitForEvent('download');
  await advanced.getByRole('button', { name: 'Export', exact: true }).click();
  const downloadedScript = await scriptDownload;
  const exportedScript = JSON.parse(fs.readFileSync(await downloadedScript.path(), 'utf8'));
  assert.equal(
    exportedScript.nodes.find((n) => n.component === 'moonpi.pin-script').properties.script,
    scriptSource,
  );
  console.log(
    'PASS reference board image, Lua editor, syntax validation, live scripted GPIO, manual trigger, Stop and source export',
  );
  await advanced.getByRole('button', { name: 'Starters', exact: true }).click();
  const gallery = advanced.getByRole('dialog', { name: 'Starter projects', exact: true });
  await gallery.getByLabel('Search starters').fill('temperature');
  await gallery.getByRole('button', { name: /Temperature alarm/ }).click();
  assert.equal(await gallery.locator('tbody tr').count(), 6);
  await gallery.getByLabel('Search starters').fill('');
  await gallery.getByRole('button', { name: /Button-controlled LED/ }).click();
  await gallery.getByRole('button', { name: 'Open starter', exact: true }).click();
  await gallery.getByRole('button', { name: 'Keep current canvas', exact: true }).click();
  assert.equal(await scriptEditor.inputValue(), scriptSource);
  await gallery.getByRole('button', { name: 'Open starter', exact: true }).click();
  await gallery.getByRole('button', { name: 'Replace canvas', exact: true }).click();
  await advanced.getByLabel('Starter guide', { exact: true }).waitFor();
  await advanced.getByRole('button', { name: /Undo/ }).click();
  await scriptNode.locator('strong').click();
  assert.equal(await scriptEditor.inputValue(), scriptSource);
  await advanced.getByRole('button', { name: /Redo/ }).click();
  await advanced.getByRole('button', { name: 'Apply', exact: true }).click();
  let starterState = await (await fetch(base + '/api/v1/state')).json();
  assert.equal(starterState.validation.valid, true);
  assert.equal(starterState.project_saved, false);
  const originalStarter = JSON.parse(
    fs.readFileSync(path.join(root, 'examples/button-led.moonpi.json'), 'utf8'),
  );
  const originalIds = new Set([
    originalStarter.id,
    originalStarter.board.node_id,
    ...originalStarter.nodes.map((n) => n.id),
    ...originalStarter.edges.map((e) => e.id),
  ]);
  for (const id of [
    starterState.project.id,
    starterState.project.board.node_id,
    ...starterState.project.nodes.map((n) => n.id),
    ...starterState.project.edges.map((e) => e.id),
  ])
    assert.ok(!originalIds.has(id));
  await advanced.getByRole('button', { name: 'Save', exact: true }).click();
  await advanced.getByText('Saved to disk', { exact: true }).waitFor();
  await advanced.reload();
  await advanced.getByLabel('Starter guide', { exact: true }).waitFor();
  await advanced.getByRole('button', { name: '▶ Run', exact: true }).click();
  await advanced.getByRole('button', { name: 'Starters', exact: true }).click();
  await gallery
    .getByText('Stop the running graph before opening a starter.', { exact: false })
    .waitFor();
  assert.equal(await gallery.getByRole('button', { name: 'Open starter', exact: true }).count(), 0);
  await gallery.screenshot({ path: path.join(data, 'starter-gallery.png') });
  await gallery.getByRole('button', { name: 'Close starters', exact: true }).click();
  await advanced.getByRole('button', { name: '■ Stop', exact: true }).click();
  console.log(
    'PASS starter search, wiring preview, replacement cancellation, Undo/Redo, fresh IDs, saved-state tracking, persistent guide and running-graph protection',
  );
  await advanced.close();
  assert.equal(errors.length, 0, errors.join('\n'));
  console.log(
    'PASS imported sensor automation, live readings, export and debounced simulated button',
  );
  console.log('Artifacts: ' + data);
} catch (error) {
  if (browser) {
    let index = 0;
    for (const context of browser.contexts())
      for (const page of context.pages())
        await page
          .screenshot({ path: path.join(data, `failure-${index++}.png`), fullPage: true })
          .catch(() => {});
  }
  throw error;
} finally {
  if (browser) await browser.close();
  await stop();
  fs.writeFileSync(path.join(data, 'server.log'), logs);
}
