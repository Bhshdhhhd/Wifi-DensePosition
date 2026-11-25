# WiFi-DensePosition

### Single-Node Wi-Fi CSI Human Sensing using ESP32-S3

WiFi-DensePosition is an academic Wi-Fi sensing project that uses
Channel State Information (CSI) collected by an ESP32-S3 to study
changes in the wireless environment.

The initial implementation includes Wi-Fi CSI acquisition,
signal processing, empty-environment baseline calibration,
and experimental presence-change detection.

No additional sensing hardware is required.

---

## 1. Project Overview

### Objective

Explore how human activity affects the propagation of Wi-Fi signals
and use CSI measurements to detect changes in the environment.

### Hardware

- ESP32-S3 development board
- 2.4 GHz Wi-Fi router or mobile hotspot
- USB data cable
- Computer running macOS, Linux, or Windows

### Software

- ESP-IDF 5.4
- FreeRTOS
- C
- Python 3
- Docker
- esptool
- pyserial

### Current Features

- ESP32-S3 Wi-Fi station connection
- Wi-Fi CSI callback registration
- CSI sample collection
- FreeRTOS queue-based processing
- CSI I/Q magnitude extraction
- Per-subcarrier empty-environment baseline
- Statistical CSI deviation scoring
- Experimental presence-change detection
- Serial telemetry

---

## 2. Architecture

    2.4 GHz Wi-Fi Access Point
              |
              v
        ESP32-S3 Wi-Fi
              |
              v
          CSI Capture
              |
              v
        FreeRTOS Queue
              |
              v
       CSI Signal Processing
              |
              v
      Magnitude Extraction
              |
              v
       Baseline Comparison
              |
              v
       Presence Detector
              |
              v
        Serial Output

### How It Works

1. The ESP32-S3 connects to a 2.4 GHz Wi-Fi access point.
2. The Wi-Fi driver provides CSI information from received packets.
3. A callback copies CSI samples into a FreeRTOS queue.
4. A separate task processes the samples.
5. Complex CSI I/Q values are converted into magnitudes.
6. The first 300 CSI frames establish a reference environment.
7. Subsequent samples are compared against that reference.
8. A statistical score is used to identify significant changes.

---

## 3. Repository Structure

    Wifi-DensePosition/
    |
    |-- README.md
    |
    |-- firmware/
    |   |
    |   |-- s3-csi-sensing/
    |       |
    |       |-- main/
    |       |   |-- main.c
    |       |   |-- presence_detector.c
    |       |   |-- presence_detector.h
    |       |   |-- CMakeLists.txt
    |       |   |-- Kconfig.projbuild
    |       |
    |       |-- CMakeLists.txt
    |       |-- sdkconfig.defaults
    |       |-- .gitignore
    |
    |-- tools/
    |
    |-- data/

---

## 4. Installation and Setup

### Step 1: Clone the Repository

Install Git and Docker Desktop.

Open a terminal:

    git clone https://github.com/Bhshdhhhd/Wifi-DensePosition.git

    cd Wifi-DensePosition

### Step 2: Identify Your ESP32-S3 Serial Port

Connect the ESP32-S3 using a USB data cable.

On macOS:

    ls /dev/cu.usb*

On Linux:

    ls /dev/ttyUSB* /dev/ttyACM*

On Windows, check Device Manager for the COM port.

For macOS, set the detected port:

    export PORT=/dev/cu.usbmodem101

Replace this value with the actual port on your computer.

### Step 3: Prepare the Firmware Environment

The project uses ESP-IDF 5.4 through Docker.

The Docker image is:

    espressif/idf:v5.4

Pull it:

    docker pull espressif/idf:v5.4

This avoids requiring a separate local ESP-IDF installation.

### Step 4: Configure the Firmware

Run from the repository root:

    docker run --rm -it \
      -v "$PWD/firmware/s3-csi-sensing:/project" \
      -w /project \
      espressif/idf:v5.4 \
      bash -c "idf.py set-target esp32s3 && idf.py menuconfig"

Inside the configuration menu:

1. Open WiFi DensePosition Configuration.
2. Set Wi-Fi SSID.
3. Set Wi-Fi password.
4. Save and exit.

Use a 2.4 GHz access point. ESP32-S3 does not support 5 GHz Wi-Fi.

The local `sdkconfig` stores the credentials and is excluded from Git.

The default configuration enables CSI and selects the USB Serial/JTAG console.

---

## 5. Build the Firmware

Run from the repository root:

    docker run --rm \
      -v "$PWD/firmware/s3-csi-sensing:/project" \
      -w /project \
      espressif/idf:v5.4 \
      bash -c "idf.py build"

A successful build ends with:

    Project build complete.

The generated application binary is:

    firmware/s3-csi-sensing/build/wifi_denseposition_s3.bin

---

## 6. Flash the ESP32-S3

Install the host-side Python tools if required:

    python3 -m pip install esptool pyserial

### First-Time Flash

For a new or freshly configured board, run from the repository root:

    python3 -m esptool \
      --chip esp32s3 \
      --port "$PORT" \
      --baud 115200 \
      write-flash \
      --flash-mode dio \
      --flash-freq 80m \
      --flash-size 8MB \
      0x0 firmware/s3-csi-sensing/build/bootloader/bootloader.bin \
      0x8000 firmware/s3-csi-sensing/build/partition_table/partition-table.bin \
      0x10000 firmware/s3-csi-sensing/build/wifi_denseposition_s3.bin

Expected output:

    Hash of data verified.
    Hard resetting via RTS pin...

### Subsequent Firmware Updates

When the bootloader and partition table are already configured,
only the application needs to be flashed:

    python3 -m esptool \
      --chip esp32s3 \
      --port "$PORT" \
      --baud 115200 \
      write-flash \
      0x10000 firmware/s3-csi-sensing/build/wifi_denseposition_s3.bin

Do not erase the complete flash for ordinary application updates.

---

## 7. Start the Serial Monitor

Run:

    python3 -m serial.tools.miniterm "$PORT" 115200

The firmware should display messages similar to:

    DENSEPOSITION: WiFi DensePosition - ESP32-S3
    DENSEPOSITION: Starting single-node CSI sensing firmware
    DENSEPOSITION: Wi-Fi station started
    DENSEPOSITION: Connecting to Wi-Fi...
    DENSEPOSITION: Connected. IP address: ...
    DENSEPOSITION: CSI enabled.
    DENSEPOSITION: Gateway ping started: ...

Press Ctrl+] to exit the serial monitor.

If the serial device is busy, close any other serial monitor before flashing.

---

## 8. Baseline Calibration

The firmware automatically performs baseline calibration after
booting and establishing Wi-Fi CSI reception.

It collects the first 300 valid CSI frames.

At approximately 10 CSI frames per second, calibration takes
around 30 seconds.

### Calibration Procedure

1. Keep the ESP32-S3 and Wi-Fi access point stationary.
2. Keep the monitored sensing area unoccupied.
3. Avoid movement during calibration.
4. Power on or reset the ESP32-S3.
5. Wait until the baseline reaches 300 frames.

Expected output:

    CSI ... | baseline=100/300 frames
    CSI ... | baseline=200/300 frames
    CSI ... | baseline=300/300 frames

After calibration, the firmware starts reporting its presence score.

Important:

The baseline represents the environment during calibration.
If a person is already present, that person can become part of
the reference environment.

The baseline is currently stored in RAM. Rebooting the board
starts a new calibration.

---

## 9. Understanding the Detection Output

Example:

    CSI 10.0 fps |
    PRESENCE=NOT_DETECTED |
    score=0.40 |
    valid_bins=109 |
    power=131.83 |
    RSSI=-13 dBm |
    channel=6 |
    drops=0

### Field Description

| Field | Meaning |
|---|---|
| CSI fps | Measured CSI processing rate |
| PRESENCE | Experimental detection result |
| score | Average normalized CSI deviation |
| valid_bins | Number of usable CSI subcarriers |
| power | Mean CSI subcarrier power |
| RSSI | Received signal strength |
| channel | Wi-Fi channel |
| drops | CSI samples dropped by the software queue |

### Current Detection Parameters

The initial detector uses:

- Calibration frames: 300
- Entry threshold: 2.2
- Entry confirmation: 10 consecutive frames
- Exit threshold: 1.4
- Exit confirmation: 50 consecutive frames

These are experimental thresholds and may require tuning.

### Interpretation

A high deviation score indicates a change in CSI relative
to the learned baseline.

A low score indicates that the signal is relatively similar
to the reference environment.

NOT_DETECTED does not prove that a room is empty.

---

## 10. Demonstration Procedure

### Experiment A: Empty Reference Environment

1. Position the ESP32-S3 and Wi-Fi access point.
2. Keep the monitored sensing area unoccupied.
3. Reset the ESP32-S3.
4. Wait for 300 baseline frames.
5. Record the empty-environment score.

### Experiment B: Stationary Occupant

1. Do not reset the board.
2. Have one person enter the monitored sensing area.
3. Ask the person to remain relatively still.
4. Observe the CSI score and detection result.

### Experiment C: Moving Occupant

1. Keep the ESP32-S3 and access point stationary.
2. Ask one person to move through the RF sensing area.
3. Observe how the CSI score changes.

### Experiment D: Person Exits

1. Have the person leave the monitored area.
2. Observe whether the score returns toward the baseline.
3. Record the detection response.

For meaningful experiments, repeat the tests and record
the actual results.

---

## 11. Troubleshooting

### A. Serial Port Is Busy

Exit the existing serial monitor using Ctrl+].

Check the process:

    lsof -nP "$PORT"

Terminate only the confirmed stale serial-monitor process:

    kill -TERM PID

Replace PID with the process ID reported by lsof.

### B. Wi-Fi Does Not Connect

Check:

- The SSID is correct.
- The password is correct.
- The access point is operating at 2.4 GHz.
- WPA2-Personal compatibility is enabled.
- The ESP32-S3 is within Wi-Fi range.

### C. No CSI Samples

Check:

- Wi-Fi connection succeeded.
- CSI was enabled.
- Gateway ping is running.
- The access point is transmitting traffic.
- The CSI callback is receiving packets.

### D. Stack Overflow

The processing task uses an 8192-byte stack.
The large detector state and magnitude scratch buffer are
allocated statically to reduce task stack usage.

Build and flash the latest repository version.

### E. No Presence Detection

Check whether:

- The baseline was collected in the intended empty condition.
- The person is affecting the RF sensing path.
- The ESP32-S3 and access point remained stationary.
- The score crosses the configured detection thresholds.

Do not interpret NOT_DETECTED as proof of an empty room.

---

## 12. Current Implementation Limitations

This is an experimental single-node Wi-Fi CSI sensing prototype.

Currently implemented:

- CSI collection
- CSI magnitude extraction
- Empty-environment statistical baseline
- Relative CSI deviation scoring
- Experimental presence-change detection

Not yet implemented:

- Raw CSI dataset export
- Machine-learning model training
- Trained model deployment
- Breathing-rate estimation
- Heart-rate estimation
- Reliable multi-person detection
- Spatial localization
- Fall detection

The current presence detector is based on statistical thresholds.
It is not a trained machine-learning classifier.

The system is intended for academic experimentation,
not medical diagnosis or safety-critical monitoring.

---

## 13. Development Roadmap

### Phase 1: CSI Acquisition
Completed.

### Phase 2: Statistical Presence Detection
Implemented; further hardware validation required.

### Phase 3: Dataset Collection
Planned.

### Phase 4: Feature Engineering and ML Training
Planned.

### Phase 5: Respiration Signal Processing
Planned.

### Phase 6: Model Deployment and Final Evaluation
Planned.

---

## 14. Reference Material

Espressif ESP-IDF 5.4 documentation:
https://docs.espressif.com/projects/esp-idf/en/v5.4/

RuView reference project:
https://github.com/ruvnet/RuView

This project uses Wi-Fi CSI sensing concepts for an independent,
single-node ESP32-S3 implementation.

---

## 15. Project Information

Repository:
https://github.com/Bhshdhhhd/Wifi-DensePosition

Hardware:
ESP32-S3

Framework:
ESP-IDF 5.4

Language:
C

Project type:
Academic / Experimental Wi-Fi Sensing

