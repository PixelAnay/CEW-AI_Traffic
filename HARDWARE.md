# SmartFlow AI Traffic: Hardware & Wiring Specification

This document provides complete instructions for wiring, configuring, and flashing the **ESP8266 NodeMCU** hardware client for SmartFlow AI Traffic.

---

## 1. Bill of Materials (BOM)

| Component | Quantity | Specification / Description |
| :--- | :---: | :--- |
| **Microcontroller** | 1 | NodeMCU ESP8266 (ESP-12E / ESP-12F module) |
| **Ultrasonic Sensors** | 3 | HC-SR04 or HC-SR04P (North, South, East lanes) |
| **Display** | 1 | 0.96-inch OLED Display (I2C, SSD1306, 128x64 pixels) |
| **Temperature Sensor** | 1 | DHT11 Temperature and Humidity Sensor module |
| **Buzzer** | 1 | Active or Passive 5V/3.3V Buzzer (Audible congestion alarm) |
| **Resistors** | 6 | 1 kΩ and 2 kΩ (for 5V $\to$ 3.3V logic level voltage dividers on Echo pins) |
| **Breadboard & Wires** | 1+ | Full-size breadboard and Male-to-Male / Male-to-Female jumper wires |
| **Power Supply** | 1 | 5V 2A Micro-USB adapter or external regulated 5V power supply |

---

## 2. Complete Pinout & Wiring Table

| Component | Component Pin | NodeMCU Pin | GPIO | Description / Wiring Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Lane 1: North (HC-SR04)** | VCC | Vin / 5V | - | 5V Power rail |
| | GND | GND | - | Common Ground |
| | TRIG | **D5** | GPIO 14 | Direct connection |
| | ECHO | **D6** | GPIO 12 | **Must use Voltage Divider** (5V $\to$ 3.3V) |
| **Lane 2: South (HC-SR04)** | VCC | Vin / 5V | - | 5V Power rail |
| | GND | GND | - | Common Ground |
| | TRIG | **D7** | GPIO 13 | Direct connection |
| | ECHO | **D2** | GPIO 4 | **Must use Voltage Divider** (5V $\to$ 3.3V) |
| **Lane 3: East (HC-SR04)** | VCC | Vin / 5V | - | 5V Power rail |
| | GND | GND | - | Common Ground |
| | TRIG | **D1** | GPIO 5 | Direct connection |
| | ECHO | **D0** | GPIO 16 | **Must use Voltage Divider** (5V $\to$ 3.3V) |
| **OLED Display (SSD1306)** | VCC | 3V3 | - | 3.3V Power rail |
| | GND | GND | - | Common Ground |
| | SDA | **D3** | GPIO 0 | I2C Data (Moved from D2 to avoid sensor conflict) |
| | SCL | **D4** | GPIO 2 | I2C Clock (Moved from D1 to avoid sensor conflict) |
| **DHT11 Sensor** | VCC | 3V3 | - | 3.3V Power rail |
| | GND | GND | - | Common Ground |
| | DATA | **RX** | GPIO 3 | Data pin (Keep `DEBUG_SERIAL 0` to prevent UART contention) |
| **Buzzer** | Positive (+) | **D8** | GPIO 15 | Active high / Tone signal output |
| | Negative (-) | GND | - | Common Ground |

---

## 3. Important Electrical & Safety Considerations

### ⚠️ 1. 5V Logic Protection on HC-SR04 Echo Pins
HC-SR04 sensors output a **5V logic pulse** on their `ECHO` pin. The ESP8266 GPIO inputs are rated for **3.3V maximum**. Feeding 5V into the ESP8266 can permanently damage the GPIO pins.

Connect a simple two-resistor voltage divider to each ECHO pin:
```
HC-SR04 ECHO (5V) ───[ 1 kΩ ]───┬─── ESP8266 ECHO Pin (3.3V)
                                │
                             [ 2 kΩ ]
                                │
                               GND
```
*Formula: $V_{out} = 5\text{V} \times \frac{2000}{1000 + 2000} \approx 3.33\text{V}$*

### ⚠️ 2. OLED Relocation (D3 & D4)
Standard ESP8266 default I2C pins are D1 (`SCL`) and D2 (`SDA`). In this 3-lane project, D1 and D2 are allocated to ultrasonic sensor triggers and echoes. The SSD1306 I2C bus is initialized in code using:
```cpp
Wire.begin(D3, D4); // SDA = D3 (GPIO0), SCL = D4 (GPIO2)
```

### ⚠️ 3. DHT11 on GPIO 3 (RX)
Because all digital GPIO pins are populated, the DHT11 sensor is connected to **GPIO 3 (NodeMCU RX pin)**. 
- In `config.h`, keep `#define DEBUG_SERIAL 0`.
- If `DEBUG_SERIAL` is set to `1`, the Hardware UART Serial receiver will clash with the DHT11 digital signal, causing read errors or boot failure.
- When uploading new firmware via the Arduino IDE or PlatformIO, ensure sensors on RX do not pull the pin LOW at bootloader entry.

---

## 4. Software Setup & Flashing Guide

1. Open the **Arduino IDE** (v1.8+ or v2.x).
2. Install the **ESP8266 Board Package**:
   - Go to **File > Preferences** and add: `http://arduino.esp8266.com/stable/package_esp8266com_index.json` to **Additional Board Manager URLs**.
   - Go to **Tools > Board > Boards Manager**, search for `esp8266`, and click **Install**.
3. Install the required libraries via **Sketch > Include Library > Manage Libraries...**:
   - `Adafruit SSD1306`
   - `Adafruit GFX Library`
   - `DHT sensor library` (Adafruit)
   - `Adafruit Unified Sensor`
4. Configure your credentials:
   - Copy `config.example.h` to `config.h`:
     ```bash
     cp config.example.h config.h
     ```
   - Open `config.h` and enter your Wi-Fi SSID, password, and the IP of the machine running `server.js`:
     ```cpp
     static const char* WIFI_SSID = "MyHomeWiFi";
     static const char* WIFI_PASSWORD = "MyPassword123";
     static const char* SERVER_URL = "http://192.168.1.50:8080";
     ```
5. Select Board settings:
   - **Board**: `NodeMCU 1.0 (ESP-12E Module)`
   - **Upload Speed**: `115200` or `921600`
   - **CPU Frequency**: `80 MHz` (or `160 MHz`)
   - **Port**: Choose your COM port (e.g., `COM3`, `/dev/ttyUSB0`)
6. Click **Upload**.
