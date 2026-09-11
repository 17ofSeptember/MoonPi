import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { createRequire } from 'node:module';
const require = createRequire(new URL('../frontend/package.json', import.meta.url));
const { chromium } = require('@playwright/test');
const root = path.resolve(import.meta.dirname, '..');
const packaged = process.env.MOONPI_PACKAGE && path.resolve(process.env.MOONPI_PACKAGE);
const executable = packaged
  ? path.join(packaged, 'moonpi.exe')
  : process.env.MOONPI_EXE || path.join(root, 'build-native/Release/moonpi.exe');
const data = path.join(root, 'build-win', 'security-' + Date.now());
fs.mkdirSync(data, { recursive: true });
const token = randomBytes(24).toString('hex');
const base = 'http://127.0.0.1:18082';
const child = spawn(
  executable,
  [
    '--simulation',
    '--bind',
    '127.0.0.1',
    '--port',
    '18082',
    '--web',
    packaged ? 'web' : 'frontend/dist',
    '--data',
    data,
  ],
  { cwd: packaged || root, windowsHide: true, env: { ...process.env, MOONPI_TOKEN: token } },
);
let logs = '',
  browser;
child.stdout.on('data', (chunk) => (logs += chunk));
child.stderr.on('data', (chunk) => (logs += chunk));
child.on('error', (error) => (logs += error.message));
const bearer = { Authorization: `Bearer ${token}` };
const request = (route, options = {}) => fetch(base + '/api/v1/' + route, options);
try {
  let ready = false;
  for (let i = 0; i < 100; ++i) {
    try {
      ready = (await request('state')).status === 403;
    } catch {}
    if (ready) break;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  assert.ok(ready, 'Protected server is listening');
  assert.equal(
    (await request('state', { headers: { Authorization: 'Bearer incorrect' } })).status,
    403,
  );
  assert.equal(
    (await request('state', { headers: { Cookie: `moonpi_session=${token}` } })).status,
    403,
  );
  assert.equal(
    (await request('state', { headers: { ...bearer, Origin: 'http://untrusted.invalid' } })).status,
    403,
  );
  const login = await request('session', { method: 'POST', headers: bearer });
  assert.equal(login.status, 200);
  const header = login.headers.get('set-cookie');
  assert.ok(header.includes('HttpOnly') && header.includes('SameSite=Strict'));
  assert.ok(!header.includes(token));
  const cookie = header.split(';')[0];
  assert.equal(
    (await request('state', { headers: { Cookie: `theme=dark; ${cookie}; other=a` } })).status,
    200,
  );
  assert.equal(
    (await request('state', { headers: { Cookie: `${cookie}; ${cookie}` } })).status,
    403,
  );
  const health = await (await request('health', { headers: bearer })).json();
  assert.equal(health.authentication, 'session_or_bearer');
  assert.equal(health.subsystems.executor.status, 'healthy');
  assert.equal(health.subsystems.persistence.status, 'healthy');
  const command = (name) =>
    request('commands', {
      method: 'POST',
      headers: { ...bearer, 'Content-Type': 'application/json' },
      body: JSON.stringify({
        version: 1,
        command: name,
        expected_revision: name === 'Validate' ? 1 : 0,
      }),
    });
  const flood = await Promise.all(Array.from({ length: 45 }, () => command('Validate')));
  assert.ok(flood.some((response) => response.status === 429));
  assert.equal((await command('Stop')).status, 200);
  const emergency = await command('EmergencyStop');
  assert.equal(emergency.status, 200);
  assert.equal((await emergency.json()).runtime.emergency, true);
  assert.equal(
    (await request('session', { method: 'DELETE', headers: { Cookie: cookie } })).status,
    200,
  );
  assert.equal((await request('state', { headers: { Cookie: cookie } })).status, 403);
  console.log(
    'PASS session exchange, cookie parsing/revocation, origin checks, health and Stop/E-STOP under rate limit',
  );
  const auditPath = path.join(data, 'logs', 'audit.jsonl');
  fs.renameSync(auditPath, auditPath + '.saved');
  fs.mkdirSync(auditPath);
  try {
    assert.equal((await command('EmergencyStop')).status, 200);
    assert.equal(
      (await (await request('health', { headers: bearer })).json()).subsystems.persistence.status,
      'degraded',
    );
  } finally {
    fs.rmdirSync(auditPath);
    fs.renameSync(auditPath + '.saved', auditPath);
  }
  assert.equal((await command('Stop')).status, 200);
  assert.equal(
    (await (await request('health', { headers: bearer })).json()).subsystems.persistence.status,
    'healthy',
  );
  console.log(
    'PASS health reflects audit-disk failure and recovery without preventing emergency stop',
  );
  browser = await chromium.launch({
    channel: process.env.MOONPI_BROWSER || 'msedge',
    headless: true,
  });
  const context = await browser.newContext({ viewport: { width: 1700, height: 1100 } });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto(base);
  await page.getByLabel('Session token', { exact: true }).fill(token);
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await page.getByText('Connected', { exact: false }).first().waitFor();
  const session = (await context.cookies()).find((c) => c.name === 'moonpi_session');
  assert.ok(session?.httpOnly && session.sameSite === 'Strict');
  assert.notEqual(session.value, token);
  assert.equal(await page.evaluate(() => sessionStorage.getItem('moonpi-token')), null);
  assert.ok(!(await page.evaluate(() => document.cookie)).includes('moonpi_session'));
  await page.reload();
  await page.getByText('Connected', { exact: false }).first().waitFor();
  await page.getByRole('button', { name: 'Health', exact: true }).click();
  await page.getByText('Service health: healthy', { exact: true }).waitFor();
  await page.screenshot({ path: path.join(data, 'authenticated.png'), fullPage: true });
  await page.getByRole('button', { name: 'Sign out', exact: true }).click();
  await page.getByLabel('Session token', { exact: true }).waitFor();
  assert.equal(
    (await request('state', { headers: { Cookie: `moonpi_session=${session.value}` } })).status,
    403,
  );
  await page.waitForFunction(
    () => !document.querySelector('.connection')?.textContent?.includes('Connected'),
  );
  for (let i = 0; i < 30; ++i) {
    const current = await (await request('health', { headers: bearer })).json();
    if (current.subsystems.websocket.clients === 0) break;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  assert.equal(
    (await (await request('health', { headers: bearer })).json()).subsystems.websocket.clients,
    0,
  );
  const rejectedLogins = await Promise.all(
    Array.from({ length: 16 }, () => request('session', { method: 'POST' })),
  );
  assert.ok(rejectedLogins.some((response) => response.status === 429));
  assert.deepEqual(errors, []);
  console.log('PASS browser login, HttpOnly credential isolation, reload and sign-out');
  console.log('Artifacts: ' + data);
} finally {
  if (browser) await browser.close();
  if (child.exitCode === null) {
    child.kill();
    await new Promise((resolve) => child.once('exit', resolve));
  }
  assert.ok(!logs.includes(token), 'Access token is absent from server logs');
  fs.writeFileSync(path.join(data, 'server.log'), logs);
}
