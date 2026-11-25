# WiFi-DensePosition Dashboard

Local browser dashboard for the ESP32-S3 serial telemetry. It runs independently from the Rust RuView server to avoid competing for port 3000. Default URL: http://localhost:8090.

## Run

From the repository root:

```bash
python3 -m pip install -r dashboard/requirements.txt
python3 dashboard/server.py --port /dev/cu.usbmodem101 --baud 115200 --http-port 8090
```

Open http://localhost:8090.

Close `miniterm` before starting this dashboard because only one process can own the serial port. Stop the dashboard with Ctrl+C.

## Supported serial formats

1. Existing summary telemetry, e.g. `CSI 10.0 fps | PRESENCE=NOT_DETECTED | score=0.40 | valid_bins=109 | power=131.83 | RSSI=-13 dBm | channel=6 | length=256 | drops=0`.
2. Optional 32-band raw CSI record: `CSI,timestamp_us,rssi,channel,pair_count,band_00,...,band_31`.

The firmware exporter for format 2 must be implemented and flashed separately. Until then, charts and detection telemetry can work from summary records, while raw CSI heatmap and dataset collection remain unavailable.

## Data collection

Press Start recording, choose EMPTY / OCCUPIED_STATIC / OCCUPIED_MOVING as the experiment changes, then Stop recording. Raw rows are written to `data/dashboard/<session timestamp>/raw_csi.csv`. The dashboard does not train a classifier. ML feature extraction, training, evaluation and ESP32 C inference are future implementation work.

## Engine status

The current embedded detector is statistical baseline + thresholds. There is no trained neural model or deployed neural inference engine at this stage.
