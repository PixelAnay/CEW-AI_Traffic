# SmartFlow AI Traffic

> **Adaptive Real-Time Urban Traffic Management powered by ESP8266 IoT Hardware & Local Large Language Models.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Node.js](https://img.shields.io/badge/Node.js-18%2B-green.svg)](https://nodejs.org/)
[![Hardware](https://img.shields.io/badge/Hardware-ESP8266%20NodeMCU-blue.svg)](HARDWARE.md)
[![AI Providers](https://img.shields.io/badge/AI-LM%20Studio%20%7C%20Ollama%20%7C%20OpenAI-purple.svg)](https://github.com/PixelAnay/CEW-AI_Traffic)

SmartFlow is an intelligent, edge-connected traffic signal control system. It dynamically calculates optimal green-light cycles by feeding vehicle densities from physical ultrasonic sensors (or an integrated real-time simulator) into local Large Language Models (such as Gemma 3, Llama 3, Mistral, Qwen, or Phi).

---

## 🌟 Key Features

- **Autonomous Dual Operating Modes**:
  - **Live Hardware Sync**: Interacts seamlessly with physical ESP8266 NodeMCU hardware, 3-lane ultrasonic distance sensors, DHT11 environmental sensor, buzzer alarm, and an SSD1306 OLED display.
  - **Standalone Interactive Simulator**: Run the complete interactive simulation in any modern web browser without needing any physical hardware connected!
- **Universal LLM Compatibility**:
  - Works with **LM Studio**, **Ollama**, **LocalAI**, **vLLM**, or any **OpenAI-compatible** API endpoint.
  - Auto-detects currently loaded models—no manual configuration required when changing models.
- **Fail-Safe Deterministic Fallback**:
  - If your LLM is offline or busy, the system automatically falls back to an internal mathematical optimization algorithm, guaranteeing continuous, uninterrupted intersection operations.
- **Telemetry & Live Dashboard**:
  - Real-time animated traffic intersection with vehicle density meters.
  - Incident injection controls (simulate gridlocks and bottlenecks).
  - Environmental monitoring (temperature and humidity telemetry).
  - Historical run logs, time-saved metrics, and efficiency scoring.
- **Zero Heavy Dependencies**:
  - Built with native Node.js HTTP/fetch and zero external npm packages for maximum portability, speed, and security.

---

## 🏗️ Architecture

```text
  ┌─────────────────────────────────────────────────────────┐
  │                 Physical Intersection                   │
  │  [3x HC-SR04 Sensors] ──> [NodeMCU ESP8266] <── [DHT11] │
  │          │                       │                      │
  │     (Ultrasonic)            (SSD1306 OLED & Buzzer)     │
  └──────────┼───────────────────────▲──────────────────────┘
             │ HTTP POST             │ HTTP GET
             │ /sensor-data          │ /command
             ▼                       │
  ┌──────────────────────────────────┴──────────────────────┐
  │               SmartFlow Node.js Server                  │
  │             (Reverse Proxy & Coordinator)               │
  │                  http://localhost:8080                  │
  └──────────┬────────────────────────────────▲─────────────┘
             │                                │
             ▼ Reverse Proxy / Internal Fetch │ Inference Response
  ┌───────────────────────────────────────────┴─────────────┐
  │                 Local / Cloud LLM Engine                │
  │         (LM Studio / Ollama / OpenAI / Groq)            │
  │         "Prompt: Optimize N, S, E green cycles"         │
  └─────────────────────────────────────────────────────────┘
```

---

## 🚀 Quickstart Guide

### 1. Prerequisites
- **Node.js** 18.0.0 or higher ([Download Node.js](https://nodejs.org/)).
- *(Optional)* An OpenAI-compatible LLM host, such as:
  - [LM Studio](https://lmstudio.ai/)
  - [Ollama](https://ollama.com/)
  - [OpenAI API](https://platform.openai.com/)

### 2. Installation
Clone the repository:
```bash
git clone https://github.com/PixelAnay/CEW-AI_Traffic.git
cd CEW-AI_Traffic
```

### 3. Configure Server Environment (Optional)
Copy the example environment file:
```bash
cp .env.example .env
```
Default `.env` settings:
```env
PORT=8080
HOST=0.0.0.0
AI_PROVIDER_URL=http://127.0.0.1:1234
AI_MODEL=google/gemma-3-4b
AI_MIN_GAP_MS=6000
```

> **Note**: If you don't create a `.env` file, the server will automatically default to `http://127.0.0.1:1234` on port `8080`.

### 4. Start the Server
```bash
npm start
```
*(Or directly with `node server.js`)*

The console will display the local dashboard URL and all available network IP addresses:
```text
======================================================
  SmartFlow Traffic Optimization Engine
======================================================
  Dashboard URL      : http://localhost:8080
  AI Provider Base   : http://127.0.0.1:1234
  Configured Model   : google/gemma-3-4b
  Network IP list (use in config.h for ESP8266):
    • 192.168.1.50    (Wi-Fi) -> http://192.168.1.50:8080
```

Open **`http://localhost:8080`** in your browser to launch the dashboard!

---

## 🧠 Connecting Your LLM

### LM Studio
1. Launch **LM Studio** and download any model (e.g., `google/gemma-3-4b`, `llama-3.2-3b-instruct`, or `qwen2.5-coder-7b-instruct`).
2. Go to the **Developer tab** (↔ icon) and click **Start Server** on port `1234`.
3. Open the SmartFlow dashboard; the header chip will turn green (**AI READY**).

### Ollama
1. Start Ollama:
   ```bash
   ollama run llama3.2
   ```
2. In your `.env` file:
   ```env
   AI_PROVIDER_URL=http://127.0.0.1:11434
   AI_MODEL=llama3.2
   ```
3. Restart `server.js`.

---

## 🔌 Hardware Setup (ESP8266)

If you are deploying to physical hardware:

1. Follow the comprehensive wiring diagram and bill of materials in **[HARDWARE.md](HARDWARE.md)**.
2. Copy `config.example.h` to `config.h`:
   ```bash
   cp config.example.h config.h
   ```
3. Set your Wi-Fi credentials and PC's local IP address inside `config.h`:
   ```cpp
   static const char* WIFI_SSID = "Your_WiFi_Name";
   static const char* WIFI_PASSWORD = "Your_WiFi_Password";
   static const char* SERVER_URL = "http://192.168.1.50:8080";
   ```
4. Flash `esp8266.ino` to your NodeMCU using the Arduino IDE.

---

## 📡 REST API Reference

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/` | Web Dashboard UI |
| `GET` | `/api/config` | Returns runtime server configuration (model, provider URL, gap limit) |
| `POST` | `/sensor-data` | Ingests lane densities from ESP8266 (`{"north":40,"south":12,"east":80,"tempC":24.5,"humidity":55}`) |
| `GET` | `/sensor-data` | Retrieves live telemetry and vehicle counts |
| `GET` | `/command` | Polled by ESP8266 to retrieve active signal timings and priority lane |
| `POST` | `/optimize` | Manually triggers an AI optimization cycle |
| `ALL` | `/v1/*` | Direct reverse proxy to the configured AI provider |

---

## 📄 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

---

## 🤝 Contributing

Contributions, issues, and feature requests are welcome! Feel free to check the [issues page](https://github.com/PixelAnay/CEW-AI_Traffic/issues).
