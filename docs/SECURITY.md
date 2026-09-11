# Access, sessions and health

Moon Pi remains a trusted-LAN engineering service. Authentication is optional:
set `MOONPI_TOKEN` before launching to require it. The token must contain 24–128
ASCII letters, digits, underscores or hyphens. Use a randomly generated token.
Keep it outside project files and source control. Without a configured token,
the service remains open to clients that can reach its listening address.

The browser exchanges the access token for an opaque HttpOnly, SameSite=Strict
session cookie. The access token is not retained in localStorage or sessionStorage;
older sessionStorage credentials are removed. Browser reload uses the cookie.
Cookies last at most eight hours on the server, and reconnects do not extend that
lifetime. Closing the browser normally discards the session cookie, though browser
session restoration may preserve it. Restarting Moon Pi invalidates all sessions.
At most 16 sessions are retained; expired entries are reclaimed on login.

Sign out revokes the session and closes its telemetry feed. It does not stop native
automation. Local editor state stays in the tab during sign-out; saving still
requires authentication. A service access token can create new sessions until it
is changed and the service restarted. This is a single shared credential model,
not separate user accounts or role-based access.

REST clients may continue using `Authorization: Bearer <token>`. Browser login is
`POST /api/v1/session` with that header; logout is `DELETE /api/v1/session` with
the cookie. Tokens and session IDs are not returned in JSON or recorded in logs.
The Set-Cookie response is the only delivery of the session credential. Unrelated
cookies are supported; duplicate Moon Pi cookie names are rejected.

The current server uses HTTP, without TLS. Credentials and traffic are therefore
not protected from network observers. HttpOnly cookies prevent JavaScript from
reading the cookie, but do not make arbitrary script execution safe. The cookie
does not use Secure because the bundled service is HTTP. Restrict access to a
trusted network or bind to 127.0.0.1; do not expose this service to the internet.

Session IDs use 32 bytes from the OS random generator:
[Windows BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom)
or [Linux getrandom](https://www.man7.org/linux/man-pages/man2/getrandom.2.html).
Generation failures reject login; Linux uses nonblocking entropy acquisition.

## Limits and emergency commands

The service permits 30 ordinary commands per second and 10 session requests per
second. These are separate service-wide limits. Stop and EmergencyStop bypass the
command counter, but still require authentication when configured. They act before
attempting to log the action, so an audit-disk failure cannot prevent output release.

There are four WebSocket slots, eight HTTP workers and a bounded connection queue.
Request bodies, frames and graph event queues also have bounds. These limits are
not a guarantee against network denial of service or a replacement for a firewall.

## Health and audit history

The Health button shows the authenticated `/api/v1/health` report. It includes
uptime, access mode, HTTP, WebSocket count, executor state, timer/queue counts,
degraded devices, project recovery and audit health. Unconfigured AI is reported
as unavailable while manual operation can remain healthy.

Persistence health reports known project-load failures and the last audit-write
result. It does not continuously probe storage or promise that the next write will
succeed. A successful audit write clears its previous failure. The state endpoint
contains the recorded audit error; hardware/runtime errors are also exposed there.

Audit logs rotate after exceeding 1 MiB, retaining `audit.jsonl.1`, `.2` and `.3`
alongside the current file. A single final record can cross the threshold. Rotation
and write failures are surfaced as degraded persistence. Recovery history and
actual physical-device behavior still require operator review.
