# Pressure Monitor  
A real-time inhale pressure monitoring device that measures, classifies, and displays inhale strength on a 3.5" LCD screen.

---

## Table of Contents
- [About The Project](#about-the-project)  
- [Features](#features)  
- [Tech Stack](#tech-stack)  
- [Getting Started](#getting-started)  
  - [Prerequisites](#prerequisites)  
  - [Hardware Wiring](#hardware-wiring)  
  - [Board Settings](#board-settings)  
  - [Installation](#installation)  
- [Usage](#usage)  
- [Configuration](#configuration)  
- [Directory Structure](#directory-structure)  
- [Contact](#contact)  
- [Acknowledgments](#acknowledgments)  

---

## About The Project
This project uses an **ESP32-S3** microcontroller with a built-in 3.5" capacitive touch display and an **Adafruit MPRLS** ported pressure sensor (0–25 PSI) to monitor inhale strength.  
The user inhales through a tube connected to the sensor, and the device reads the pressure drop, classifies the inhale as weak, moderate, or strong, and displays the result on screen.

If the inhale is **weak or moderate**, the user is prompted to try again immediately.  
If the inhale is **strong**, the device shows a success screen and waits 60 seconds before the next reading.

Additionally, this project is coded in **C++** using the **Arduino** framework and libraries.

---

## Features
- **Real-Time Pressure Reading**  
  Reads inhale pressure directly via I2C from the MPRLS sensor with no third-party sensor library required.

- **Inhale Classification**  
  Classifies each inhale into three levels based on configurable PSI drop thresholds:
  - **Weak** (≥ 0.3 PSI) — Red — Try again  
  - **Moderate** (≥ 0.8 PSI) — Orange — Try again  
  - **Strong** (≥ 1.5 PSI) — Green — Wait 60 seconds  

- **Peak Tracking**  
  Monitors pressure over a 10-second measurement window and records the peak inhale strength.

- **Automatic Calibration**  
  On startup, reads ambient pressure 20 times and averages them to establish a stable baseline.

- **Standalone Operation**  
  Once uploaded, the device runs on any USB power source or lithium battery — no computer needed.

---

## Tech Stack
- **Language**: C++  
- **Framework**: Arduino  
- **Microcontroller**: ESP32-S3 (Xtensa LX7 dual-core, 240MHz)  
- **Display**: 3.5" IPS LCD, 320×480, AXS15231B controller (QSPI)  
- **Sensor**: Adafruit MPRLS, 0–25 PSI (I2C address 0x18)  
- **I/O Expander**: TCA9554 (handles LCD reset)  
- **Libraries**:  
  - Arduino_GFX_Library (display driver)  
  - TCA9554 (I/O expander driver from Waveshare)  

---

## Getting Started

### Prerequisites
- Node.js is **not** required — this is an embedded C++ project  
- Arduino IDE 2.x  
- ESP32 board package installed (esp32 by Espressif Systems)  
- USB-C cable  

### Hardware Wiring
Connect the MPRLS breakout board to the ESP32 GPIO header:

- **Red** (VIN) → Pin 32 — 3V3  
- **Black** (GND) → Pin 30 — GND  
- **Blue** (SDA) → Pin 28 — GPIO 8  
- **Green** (SCL) → Pin 26 — GPIO 7  

> **Note:** The sensor uses I2C bus 1 (`Wire1`) on GPIO 8/7.  
> The display uses I2C bus 0 (`Wire`) on GPIO 21/22 internally.

### Board Settings
Configure the following in Arduino IDE under **Tools**:
- **Board**: ESP32S3 Dev Module  
- **Flash Size**: 16MB (128Mb)  
- **PSRAM**: OPI PSRAM  
- **Partition Scheme**: 16M Flash (3MB APP/9.9MB FATFS)  
- **USB CDC On Boot**: Enabled  
- **Upload Speed**: 921600  

### Installation
1. **Clone the repo**  
   ```bash
   git clone https://github.com/ahmadb123/pressure-monitor.git
   cd pressure-monitor
   ```

2. **Install Arduino libraries**  
   - Open Arduino IDE → **Tools → Manage Libraries**  
   - Search and install **Arduino_GFX_Library** (by moononournation)  
   - Copy the `TCA9554` library folder into `~/Documents/Arduino/libraries/`  
     - This library is included in the [Waveshare Demo ZIP](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5B)  

3. **Upload**  
   - Open `pressure-monitor.ino` in Arduino IDE  
   - Select **ESP32S3 Dev Module** and configure board settings above  
   - Select the correct port (`/dev/cu.usbmodem101` on Mac)  
   - Click Upload  
   - If upload fails, hold **BOOT**, press **RESET**, release **RESET**, release **BOOT**, then upload  

---

## Usage
- **Power on** the device via USB or lithium battery.  
- **Wait** for the calibration screen to finish (~1 second).  
- **Inhale** through the tube connected to the MPRLS sensor port.  
- **View results** on screen — weak (red), moderate (orange), or strong (green).  
- If **strong** — device waits 60 seconds, then prompts for next inhale.  
- If **not strong** — device prompts to try again after 3 seconds.  

---

## Configuration
Adjust thresholds and timing in the code:
```cpp
float weakThreshold = 0.3;        // minimum PSI drop to detect inhale
float moderateThreshold = 0.8;    // moderate inhale threshold
float strongThreshold = 1.5;      // strong inhale threshold (success)
unsigned long waitTime = 60000;    // cooldown after success (ms)
unsigned long readingTime = 10000; // measurement window (ms)
```

---

## Directory Structure
- ## pressure-monitor/
    ├── pressure-monitor.ino    # Main Arduino sketch  
    ├── README.md               # This file  
    └── libraries/  
        └── TCA9554/            # I/O expander library (from Waveshare)  

---

## Contact
- **Authors**: Ahmad Bishara, Allison Barreto-Portiilo
- **Email**: [bishara.a@northeastern.edu](mailto:bishara.a@northeastern.edu)
[barreto-portillo.a@northeastern.edu] (mailto:barreto-portillo.a@northeastern.edu)
- **GitHub**: [ahmadb123](https://github.com/ahmadb123)  

---

## Acknowledgments
- [Waveshare ESP32-S3-Touch-LCD-3.5B](https://www.waveshare.com/esp32-s3-touch-lcd-3.5b.htm)  
- [Adafruit MPRLS Pressure Sensor](https://www.adafruit.com/product/3965)  
- [Arduino_GFX Library](https://github.com/moononournation/Arduino_GFX)  
- [Arduino](https://www.arduino.cc/)  
- [Espressif ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3)
