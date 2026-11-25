#!/usr/bin/env python3
"""Local serial-to-browser dashboard for WiFi-DensePosition."""
from __future__ import annotations

import argparse
import csv
import json
import re
import threading
import time
from collections import deque
from datetime import datetime, timezone
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("pyserial is required. Install with: python3 -m pip install pyserial") from exc

ROOT = Path(__file__).resolve().parents[1]
DASHBOARD_DIR = Path(__file__).resolve().parent
DATA_DIR = ROOT / "data" / "dashboard"
DATA_DIR.mkdir(parents=True, exist_ok=True)

lock = threading.RLock()
state = {
    "connected": False,
    "port": None,
    "baud": 115200,
    "started_at": time.time(),
    "last_seen": None,
    "last_line": "Waiting for ESP32 serial data…",
    "logs": deque(maxlen=80),
    "history": deque(maxlen=180),
    "raw_bins": [],
    "raw_frames": 0,
    "telemetry_frames": 0,
    "telemetry": {
        "csi_fps": None,
        "presence": "UNKNOWN",
        "score": None,
        "valid_bins": None,
        "power": None,
        "rssi": None,
        "channel": None,
        "length": None,
        "drops": None,
    },
    "recording": False,
    "current_label": None,
    "session_file": None,
    "label_counts": {"EMPTY": 0, "OCCUPIED_STATIC": 0, "OCCUPIED_MOVING": 0},
    "raw_stream_available": False,
    "error": None,
}
CSV_FIELDS = ["host_timestamp", "esp_timestamp_us", "label", "rssi_dbm", "channel", "pair_count"] + [f"band_{i:02d}" for i in range(32)]


def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


def add_log(line: str, kind: str = "serial") -> None:
    with lock:
        state["last_line"] = line
        state["last_seen"] = time.time()
        state["logs"].appendleft({"time": now_iso(), "kind": kind, "line": line[:500]})


def append_raw_row(parts: list[str]) -> None:
    # Firmware exporter format: CSI,timestamp_us,rssi,channel,pair_count,32 pooled magnitudes
    if len(parts) < 5 + 32:
        add_log(f"Raw CSI row has {len(parts)} fields; expected at least 38", "warning")
        return
    try:
        values = {
            "host_timestamp": now_iso(),
            "esp_timestamp_us": int(parts[1]),
            "label": state["current_label"] or "UNLABELLED",
            "rssi_dbm": int(parts[2]),
            "channel": int(parts[3]),
            "pair_count": int(parts[4]),
        }
        for i in range(32):
            values[f"band_{i:02d}"] = float(parts[5 + i])
    except (ValueError, IndexError):
        add_log("Could not parse CSI dataset row", "warning")
        return

    with lock:
        state["raw_stream_available"] = True
        state["raw_frames"] += 1
        state["raw_bins"] = [values[f"band_{i:02d}"] for i in range(32)]
        state["history"].append({"time": time.time(), "type": "raw", "value": sum(state["raw_bins"]) / 32.0})
        if state["recording"] and state["session_file"]:
            path = Path(state["session_file"])
            write_header = not path.exists() or path.stat().st_size == 0
            with path.open("a", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
                if write_header:
                    writer.writeheader()
                writer.writerow(values)
            label = values["label"]
            if label in state["label_counts"]:
                state["label_counts"][label] += 1


def parse_telemetry(line: str) -> bool:
    # Example: CSI 10.0 fps | PRESENCE=NOT_DETECTED | score=0.40 | valid_bins=109 | power=131.83 | RSSI=-13 dBm | channel=6 | length=256 | drops=0
    if "PRESENCE=" not in line or "score=" not in line:
        return False
    patterns = {
        "csi_fps": r"CSI\s+([\d.]+)\s+fps",
        "presence": r"PRESENCE=([A-Z_]+)",
        "score": r"score=([\d.]+)",
        "valid_bins": r"valid_bins=(\d+)",
        "power": r"power=([\d.]+)",
        "rssi": r"RSSI=(-?\d+)\s*dBm",
        "channel": r"channel=(\d+)",
        "length": r"length=(\d+)",
        "drops": r"drops=(\d+)",
    }
    values = {}
    for key, pattern in patterns.items():
        match = re.search(pattern, line)
        if match:
            raw = match.group(1)
            if key == "presence":
                values[key] = raw
            elif key in ("valid_bins", "channel", "length", "drops"):
                values[key] = int(raw)
            else:
                values[key] = float(raw)
    if "presence" not in values:
        return False
    with lock:
        state["telemetry"].update(values)
        state["telemetry_frames"] += 1
        state["history"].append({"time": time.time(), "type": "score", "value": values.get("score")})
    return True


def serial_reader(port: str, baud: int) -> None:
    while True:
        try:
            with serial.Serial(port, baudrate=baud, timeout=1) as ser:
                with lock:
                    state["connected"] = True
                    state["port"] = port
                    state["error"] = None
                add_log(f"Serial connected: {port} @ {baud}", "system")
                while True:
                    raw = ser.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    if line.startswith("CSI,"):
                        append_raw_row(line.split(","))
                        add_log("Raw CSI frame received", "csi")
                    else:
                        parsed = parse_telemetry(line)
                        if not parsed:
                            kind = "system" if any(x in line for x in ("DENSEPOSITION", "wifi:", "boot:", "ERROR", "Connected")) else "serial"
                            add_log(line, kind)
        except Exception as exc:  # reconnect after unplug/reset/port busy
            with lock:
                state["connected"] = False
                state["error"] = str(exc)
                state["port"] = port
            add_log(f"Serial disconnected / retrying: {exc}", "error")
            time.sleep(2)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DASHBOARD_DIR), **kwargs)

    def log_message(self, fmt, *args):
        # Keep terminal output compact.
        print("[dashboard] " + fmt % args)

    def _json(self, obj, status=200):
        payload = json.dumps(obj, default=list).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/state":
            with lock:
                snapshot = {k: v for k, v in state.items() if k not in ("history", "logs")}
                snapshot["history"] = list(state["history"])
                snapshot["logs"] = list(state["logs"])
                snapshot["uptime_s"] = int(time.time() - state["started_at"])
                snapshot["last_seen_age_s"] = round(time.time() - state["last_seen"], 1) if state["last_seen"] else None
            self._json(snapshot)
            return
        if path == "/api/health":
            self._json({"ok": True, "service": "wifi-denseposition-dashboard"})
            return
        if path == "/":
            self.path = "/index.html"
        return super().do_GET()

    def do_POST(self):
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length", "0"))
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self._json({"ok": False, "error": "Invalid JSON"}, 400)
            return

        if path == "/api/label":
            label = str(body.get("label", "")).upper()
            allowed = {"EMPTY", "OCCUPIED_STATIC", "OCCUPIED_MOVING", "UNLABELLED"}
            if label not in allowed:
                self._json({"ok": False, "error": f"Label must be one of {sorted(allowed)}"}, 400)
                return
            with lock:
                state["current_label"] = label
            add_log(f"Experiment label changed: {label}", "label")
            self._json({"ok": True, "label": label})
            return

        if path == "/api/session":
            action = body.get("action")
            with lock:
                if action == "start":
                    if state["recording"]:
                        self._json({"ok": False, "error": "A capture session is already running"}, 409)
                        return
                    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                    session_dir = DATA_DIR / stamp
                    session_dir.mkdir(parents=True, exist_ok=True)
                    session_file = session_dir / "raw_csi.csv"
                    state["recording"] = True
                    state["current_label"] = "UNLABELLED"
                    state["session_file"] = str(session_file)
                    state["label_counts"] = {"EMPTY": 0, "OCCUPIED_STATIC": 0, "OCCUPIED_MOVING": 0}
                elif action == "stop":
                    state["recording"] = False
                    session_file = state["session_file"]
                    state["current_label"] = None
                else:
                    self._json({"ok": False, "error": "action must be start or stop"}, 400)
                    return
            add_log(f"Capture session {action}: {session_file}", "session")
            self._json({"ok": True, "recording": state["recording"], "session_file": session_file})
            return

        if path == "/api/train":
            self._json({"ok": False, "error": "Training module is not implemented yet. This dashboard currently captures raw CSI and displays the existing statistical detector."}, 501)
            return

        self._json({"ok": False, "error": "Not found"}, 404)


def main():
    parser = argparse.ArgumentParser(description="WiFi-DensePosition serial dashboard")
    parser.add_argument("--port", default="/dev/cu.usbmodem101", help="ESP32 serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--http-port", type=int, default=8090)
    args = parser.parse_args()

    reader = threading.Thread(target=serial_reader, args=(args.port, args.baud), daemon=True)
    reader.start()
    server = ThreadingHTTPServer(("127.0.0.1", args.http_port), Handler)
    print("\nWiFi-DensePosition dashboard")
    print(f"  Dashboard: http://localhost:{args.http_port}")
    print(f"  Serial:    {args.port} @ {args.baud}")
    print(f"  Dataset:   {DATA_DIR}")
    print("Press Ctrl+C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down dashboard...")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
