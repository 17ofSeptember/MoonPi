# Linux deployment and installation check (0.8)

## Check a build without starting it

From an extracted release directory:

```sh
./moonpi --doctor
./moonpi --doctor --hardware --gpio-chip /dev/gpiochip0
```

On Windows use `.\moonpi.exe --doctor`. A source build needs `--web frontend/dist`.
The command prints JSON and returns 0 when its checks pass, 1 on failure. Warnings
describe checks that were not performed. It validates schemas and the component
catalogue, checks frontend paths and examines the nearest data-directory ancestor.
Linux checks effective directory access for the invoking user. Hardware mode also
checks the Pi 3 B+ device tree and GPIO character-device access.

The check does not open devices, request pins, create data directories, acquire the
application lock, load a project, start HTTP, or test physical wiring. It can run
alongside the application. It does not prove free GPIO ownership, I2C access, token
validity, a free HTTP port, or future disk-write success. Run it as the intended
service user when testing permissions.

## Build a Linux bundle

Build the frontend first, then build on the target architecture:

```sh
sh scripts/build-pi.sh
python3 scripts/package-linux.py --build build-pi
```

Packaging needs Python 3.11+ and CMake. It installs a fresh bundle containing the
native executable, frontend, resources, schemas, examples, docs, licenses and
deployment tools. It creates `release/MoonPi-linux-arm64.tar.gz` on a 64-bit Pi or
`MoonPi-linux-x64.tar.gz` for x86-64, plus a SHA-256 sidecar. The archive contains a
per-file integrity manifest and `release.json` identifying architecture and build
platform. Existing user-data directories are not copied into the archive.

The x64 bundle built in WSL is **not a Raspberry Pi binary**. ARM64 execution has
not been verified here. Build on your Pi's OS to use compatible glibc/libstdc++
versions; bundles are not fully static and do not include system libraries.
No compiler, Node.js, npm or internet connection is needed to run an already
compatible bundle. Installation tooling needs Python 3.11+, systemd and useradd.

## Verify and preview installation

After transferring the archive and checksum, verify before extracting:

```sh
sha256sum -c MoonPi-linux-arm64.tar.gz.sha256
tar -xzf MoonPi-linux-arm64.tar.gz
cd MoonPi
python3 tools/deploy.py verify
python3 tools/deploy.py install --root /tmp/moonpi-preview
```

Use the x64 filename instead on x86-64 Linux. File hashes detect corruption, not
publisher identity: obtain the bundle and checksum from a trusted source. The
installer rejects symlinks, traversal, unlisted files and hash mismatches. Verify
a fresh extraction; running the app with its default `user-data` inside the bundle
adds unlisted files. Keep writable data outside the release.

The preview writes a filesystem tree under the explicit root. It does not create
accounts or call systemctl. Review its config and unit before a live installation.

## Install on the target

```sh
sudo python3 tools/deploy.py install --root /
sudo systemctl daemon-reload
sudo systemctl enable --now moonpi
systemctl status moonpi
```

Installation creates a non-root `moonpi` account if necessary and copies code into
`/opt/moonpi/releases/VERSION-HASH`. It creates `/etc/moonpi/config.json` only when
missing. Defaults are simulation, loopback (`127.0.0.1`) and port 8080. Persistent
projects, scheduler state and audit logs live under `/var/lib/moonpi`.

The installer creates `/etc/moonpi/environment` only when missing, with a randomly
generated `MOONPI_TOKEN` and mode 0600. View that file locally with administrator
access to obtain the login token; do not put it into URLs. The installer never
prints credentials. Existing config, token and project files are preserved.

Access the loopback service locally, or forward it from your workstation:

```sh
ssh -L 8081:127.0.0.1:8080 YOUR_PI_USER@YOUR_PI_HOST
```

Then open `http://127.0.0.1:8081` and enter the token. To intentionally expose the
HTTP service on a trusted LAN, change `bind` in `/etc/moonpi/config.json`, then
restart the service. See SECURITY.md for the HTTP/session limitations.

For hardware, follow LINUX_GPIO.md and I2C.md first, then set `mode` to `hardware`.
The installer includes existing `gpio` and `i2c` groups in the service unit; it
does not create those device groups, change device permissions, enable interfaces
or alter boot configuration. Missing groups/devices need target-specific setup.
After any restart, Moon Pi starts with automation stopped; Apply and Run remain
explicit actions.

## Service lifecycle and upgrades

```sh
sudo systemctl stop moonpi
sudo systemctl restart moonpi
journalctl -u moonpi --since today
```

The unit requests normal SIGTERM shutdown and allows 15 seconds before systemd's
forced termination. LOW output on an abrupt crash, power loss or forced kill is
not guaranteed. `Restart=on-failure` restarts the process with rate limits; it does
not resume the graph. `StateDirectory` keeps persistent data separate from code.
The service uses NoNewPrivileges, read-only system paths and restricted home
access. `PrivateDevices=no` retains GPIO/I2C access through Unix groups. These
choices follow the [systemd execution documentation](https://github.com/systemd/systemd/blob/main/man/systemd.exec.xml)
and [service lifecycle documentation](https://github.com/systemd/systemd/blob/main/man/systemd.service.xml).

To upgrade, verify and install a fresh compatible bundle. The installer writes the
new unit last, retains previous code directories and saves the previous unit as
`moonpi.service.previous-*`. It does not start, stop, restart or enable anything.
Review changes, run daemon-reload and explicitly restart. Back up `/var/lib/moonpi`
while stopped before a version change. To roll back, stop the service, restore the
appropriate previous unit and compatible data backup, daemon-reload and restart.
No uninstall or cleanup command deletes previous releases or user projects.

The generated unit's syntax and staged install behavior are tested in Linux x64.
Live installation, reboot behavior, device-group access and hardware behavior on
an actual Pi remain unverified.
