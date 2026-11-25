# WiFi DensePosition

A single-node Wi-Fi sensing project using the ESP32-S3 and Channel State
Information (CSI).

## Current implementation

- ESP32-S3 Wi-Fi station connection
- Wi-Fi CSI callback and sample collection
- FreeRTOS queue for CSI data
- CSI magnitude and signal-power extraction
- Empty-room baseline calibration
- Experimental presence detection using normalized CSI deviations
- Serial telemetry for CSI rate, RSSI, channel and detection score

## Architecture

Wi-Fi Access Point
       |
       v
ESP32-S3 CSI Receiver
       |
       v
FreeRTOS CSI Queue
       |
       v
CSI Signal Processing
       |
       v
Presence Detection
       |
       v
Serial Telemetry

## Hardware

- ESP32-S3 development board
- 2.4 GHz Wi-Fi access point
- Computer for firmware development and serial monitoring

## Software

- ESP-IDF 5.4
- FreeRTOS
- C

## Build

From the firmware directory:

    cd firmware/s3-csi-sensing

Configure the target and Wi-Fi credentials:

    idf.py set-target esp32s3
    idf.py menuconfig

Open WiFi DensePosition Configuration and enter the Wi-Fi SSID
and password.

Build:

    idf.py build

The local sdkconfig file contains Wi-Fi credentials and is intentionally
excluded from version control.

## Current limitations

Presence detection is experimental and uses an initial empty-room
baseline and provisional thresholds. Its performance must be tested
under different room conditions and RF environments.

Respiration-rate estimation, heart-rate estimation, fall detection,
and multi-person localization are not implemented yet.

This project is intended for academic experimentation, not medical use.
