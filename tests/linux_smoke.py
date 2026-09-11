"""Linux simulation lifecycle and packaged-web smoke; no hardware access."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
import urllib.request

root = Path(__file__).resolve().parent.parent
executable = Path(sys.argv[1] if len(sys.argv) > 1 else root / "build-linux/moonpi").resolve()
package = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
runtime_root = package or root
data = root / "build-linux" / f"smoke-{time.time_ns()}"
data.mkdir(parents=True)
with socket.socket() as listener:
    listener.bind(("127.0.0.1", 0))
    port = listener.getsockname()[1]
base = f"http://127.0.0.1:{port}"
process = None


def get(route):
    with urllib.request.urlopen(base + "/api/v1/" + route, timeout=3) as response:
        return json.load(response)


def command(name, **fields):
    state = get("state")
    request = urllib.request.Request(
        base + "/api/v1/commands",
        data=json.dumps(dict(version=1, command=name,
                            expected_revision=state["design_revision"], **fields)).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=3) as response:
        return json.load(response)


def start(log):
    global process
    process = subprocess.Popen([str(executable), "--simulation", "--bind", "127.0.0.1",
                                "--port", str(port), "--web", "web" if package else "frontend/dist", "--data", str(data)],
                               cwd=runtime_root, stdout=log, stderr=log)
    for _ in range(100):
        if process.poll() is not None:
            raise RuntimeError("Service exited during startup")
        try:
            return get("state")
        except OSError:
            time.sleep(.05)
    raise RuntimeError("Service startup timed out")


def stop():
    global process
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        assert process.wait(timeout=5) == 0
    process = None


try:
    with (data / "service.log").open("w") as log:
        state = start(log)
        assert state["runtime"]["mode"] == "simulation"
        assert not state["runtime"]["running"]
        with urllib.request.urlopen(base, timeout=3) as response:
            assert b'<div id="root">' in response.read()
        project = json.loads((runtime_root / "examples/bme280-alarm.moonpi.json").read_text())
        command("SetDesign", project=project)
        fd_baseline = len(os.listdir(f"/proc/{process.pid}/fd"))
        for cycle in range(20):
            command("Apply")
            command("Start")
            for _ in range(100):
                state = get("state")
                sensor = state["runtime"]["nodes"].get(project["nodes"][0]["id"], {})
                if "temperature" in sensor and state["runtime"]["hardware"][0]["actual"]:
                    break
                time.sleep(.02)
            else:
                raise AssertionError("BME280 alarm did not execute")
            assert abs(sensor["temperature"] - 25.08) < .02
            state = command("EmergencyStop" if cycle % 2 else "Stop")
            assert not state["runtime"]["running"]
            assert not state["runtime"]["hardware"]
            if state["runtime"]["emergency"]:
                command("ClearEmergency")
        assert len(os.listdir(f"/proc/{process.pid}/fd")) <= fd_baseline + 2
        command("Save")
        stop()
        state = start(log)
        assert state["project"]["name"] == project["name"]
        assert not state["runtime"]["running"] and not state["runtime"]["hardware"]
        assert not state["recovery"]
        stop()
    print("PASS Linux sensor automation, 20 Stop/E-STOP lifecycles, descriptor bound, save and clean SIGTERM restart")
    print("Artifacts:", data)
finally:
    if process and process.poll() is None:
        process.kill()
        process.wait(timeout=5)
