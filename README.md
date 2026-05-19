# HeatWatch: IoT Heat Stress & Safety Wearable

![HeatWatch Banner](https://img.shields.io/badge/Status-Development-orange)
![License](https://img.shields.io/badge/License-GPLv3-blue)
![Platform](https://img.shields.io/badge/Platform-ESP32-green)
![Connectivity](https://img.shields.io/badge/Connectivity-LoRaWAN-blueviolet)

## 📌 Project Introduction

**HeatWatch** is a proactive IoT-based safety wearable designed to protect workers in extreme industrial and outdoor environments, specifically tailored for the mining industry in Western Australia. Heat stress and falls represent significant occupational hazards that not only endanger lives but also lead to substantial economic losses due to downtime and healthcare costs.

Current solutions in the market are often prohibitively expensive, rely on high-cost SaaS subscriptions, or require robust cellular/Wi-Fi infrastructure which is often unavailable in remote mine sites. HeatWatch addresses these gaps by providing:

*   **Real-time Physiological Monitoring:** Continuous tracking of heart rate and skin temperature.
*   **Predictive Heat Stress Assessment:** Edge-computed Heat Stress Index using a proxy Wet-Bulb Globe Temperature (WBGT) model.
*   **Automated Fall Detection:** Instant alerts triggered by sudden impacts using IMU sensors.
*   **Localized Connectivity:** Reliable long-range communication via LoRaWAN, independent of cellular networks.
*   **Cost-Effective Design:** Built on open-source hardware (ESP32) to eliminate recurring subscription fees.

---

## 🏗 System Architecture

The HeatWatch system follows a multi-layered architecture designed for reliability in remote environments.

### 1. Edge Layer (Wearable Device)
The core of the system is the **ESP32-TTGO** development board, which performs localized computation (Edge Computing) to ensure immediate responsiveness.
*   **Data Acquisition:** Samples data from high-precision sensors (MAX30102, MLX90614, BME280, LSM6DS3).
*   **Edge Processing:** 
    *   Calculates the natural wet-bulb temperature (Tw) and proxy WBGT using the Stull empirical formula.
    *   Executes a 6-axis motion analysis for real-time fall detection.
*   **Haptic Alerts:** Triggers a built-in ERM vibration motor to provide immediate tactile warnings to the worker.

### 2. Communication Layer
*   **LoRaWAN:** Data is compressed and transmitted to a **LoRaWAN Gateway** over long distances (up to 10+ km), ensuring connectivity in deep-pit or underground sites.
*   **The Things Network (TTN):** Acts as the network server to handle data traffic and routing.

### 3. Application Layer (Supervisor Dashboard)
*   **MQTT Protocol:** Securely transports data from the network server to the backend.
*   **ThingsBoard.io:** A professional IoT dashboard that visualizes real-time metrics for multiple workers, manages alerts, and stores historical safety data for compliance reporting.

---

## 🛠 Hardware Stack

| Component | Function | Model |
| :--- | :--- | :--- |
| **Microprocessor** | Core Logic & LoRa Transceiver | ESP32-TTGO (T-Beam) |
| **Heart Rate** | PPG Signal Monitoring | MAX30102 |
| **Skin Temp** | Non-contact Infrared Monitoring | MLX90614 |
| **Environment** | Ambient Temp, Humidity, Pressure | BME280 |
| **IMU** | 6-Axis Motion & Fall Detection | LSM6DS3 |
| **Haptics** | Tactile User Alerts | ERM Vibration Motor |

---

## 🚀 Key Features

*   **Multi-Tier Risk Assessment:** Classifies safety status into Low, Medium, High, and Critical based on combined physiological and environmental data.
*   **Low Power Consumption:** Designed to last for a standard 12-hour mining shift on a single charge.
*   **High Reliability:** Local alerts work even if network connectivity is temporarily lost.
*   **Scalability:** The dashboard can monitor clusters of devices simultaneously.

---

## 📂 Project Structure

```text
HeatWatch/
├── edge/               # ESP32 firmware (Arduino framework)
├── backend/            # ThingsBoard configurations & MQTT scripts
├── frontend/           # Next.js Supervisor Control Console (Dashboard UI)
├── simulator/          # Python-based device simulator & test tools
├── docs/               # Technical specifications and research
├── pyproject.toml      # Poetry dependency management
└── README.md           # Project master documentation
```

---

## 💻 Development & Simulation

To facilitate rapid testing without requiring physical hardware, a **Device Simulator** is provided in the `simulator/` directory. It uses **MQTT** to push telemetry data that mimics the behavior of the real wearable.

### ⚙️ Prerequisites
The project uses **Poetry** for dependency management. Ensure you have the virtual environment set up:
```bash
# Install dependencies
poetry install
```

### 📡 Running the Simulator
1.  **Configure environment**: Create or edit `simulator/.env` to set your MQTT broker details and simulation flags.
    *   `SIMULATE_HEAT_STRESS=true`: Triggers abnormal physiological readings.
    *   `SIMULATE_FALL=true`: Triggers a fall detection alert.
2.  **Start the Simulator**:
    ```bash
    poetry run python simulator/simulator.py
    ```
3.  **Verify Data (Optional)**: If you don't have a backend ready, you can run the receiver to see incoming messages:
    ```bash
    poetry run python simulator/receiver.py
    ```

### 📊 Simulation Logic
The simulator implements the same edge-computing logic as the firmware:
- **WBGT Proxy**: Calculated using Stull's formula from ambient temperature and humidity.
- **Risk Assessment**: Logic-based scoring (0-3) derived from physiological and environmental stressors.

---

## 🏛 ThingsBoard Cloud Setup (Production & Recommended)

The production stack utilizes the official **ThingsBoard Cloud** (SaaS) platform to host device streams, LoRaWAN decoders, and dashboard telemetry.

### 🌐 ThingsBoard Cloud Configuration
1.  **Platform URL**: Access the official cloud instance at [https://thingsboard.cloud](https://thingsboard.cloud).
2.  **Authentication**: Use your tenant administration credentials (configured in `frontend/.env.local` to enable secure browser-to-server Next.js API proxy communication).
3.  **Device Provisioning**:
    *   Under the **Devices** section, register your wearables with the target type `heatstress-group10`.
    *   Retrieve the **Device Access Token** from the device details view to configure your physical hardware nodes or the Python-based simulator.

---

## 🏛 Legacy Backend Setup (Local ThingsBoard CE via Docker)

For developers wanting to host a local sandbox environment instead of using the online cloud SaaS, the local **ThingsBoard Community Edition** is containerized using Docker.

### 🚀 Starting Local Sandbox (CE)
1.  **Navigate to the backend directory**:
    ```bash
    cd backend
    ```
2.  **Start the container**:
    ```bash
    docker-compose up -d
    ```
3.  **Access the Local Dashboard**:
    *   URL: `http://localhost:9090`
    *   Default Credentials:
        *   System Admin: `sysadmin@thingsboard.org` / `sysadmin`
        *   Tenant Admin: `tenant@thingsboard.org` / `tenant`
        *   Customer User: `customer@thingsboard.org` / `customer`

### 🎨 Custom Pages & Dashboards
ThingsBoard allows for deep customization:
- **Dashboards**: Use the built-in drag-and-drop editor to create real-time monitoring views.
- **Custom Widgets**: For unique aesthetic or functional requirements, you can develop widgets using HTML5, CSS3, and JavaScript directly within the ThingsBoard "Widget Library".
- **External Integration**: If a completely separate web application is needed, you can consume data via the ThingsBoard [Websocket API](https://thingsboard.io/docs/user-guide/telemetry/#websocket-api) or REST API.

---

## 🖥️ Next.js Supervisor Control Console

A state-of-the-art, high-end web dashboard built using **Next.js** and **React** is located in the `frontend/` directory. It interfaces with the ThingsBoard Cloud API to provide supervisors with a real-time, premium monitoring console.

### ✨ Key Dashboard Features
- **Pilbara Sandstone Aesthetic**: Designed with a high-fidelity light-mode color palette inspired by the Western Australian mining region (Outback Sandstone Sand and Clay HSL gradients).
- **Full Bilingual Localization**: Interactive language selector supporting English (EN) and Simplified Chinese (ZH) across all telemetry grids, action buttons, and physiological ratings.
- **Dynamic Wearable Status Cards**:
  - Displays body/ambient temperature split (e.g. `36.5 / 24.5 °C`), dynamic risk indicators, and edge Wet-Bulb Globe Temperature (WBGT).
  - Built-in hardware-accelerated **double flashing fall warning indicator (⚠️ 摔倒感叹号)** that pulses red with audio-visual cues during emergency fall events.
  - Smart battery level normalizer handling both integer and fractional telemetry.
- **Telemetry Inspector Detail Drawer**:
  - Interactive SVG historical trend line graph rendering the last 30 minutes of telemetry for selected metrics (Heat Stress WBGT, Skin Temp, Ambient Temp, Discomfort Index, Heart Rate).
  - Hover-based crosshairs displaying exact data points and UTC-local synced timestamps.
  - **Uplink Network Diagnostic Card**: Shows the absolute received local timestamp and relative freshness indicator (e.g. `(3 mins ago)`) of the latest packet pushed to the gateway, powered by a custom ThingsBoard parser that ignores empty placeholders and captures true hardware transmission times.
  - **Biometric Calibration & Hydration Advisor**: Dynamically adjusts personal safe WBGT thresholds based on age and BMI, featuring interactive haptic downlink RPC command relays (Mute, Hydrate vibration).

### ⚙️ Getting Started (Frontend)
1. **Navigate to the frontend directory**:
   ```bash
   cd frontend
   ```
2. **Configure environment variables**: Copy the template and fill in your ThingsBoard Tenant credentials:
   ```bash
   cp .env.example .env.local
   ```
3. **Install dependencies**:
   ```bash
   npm install
   ```
4. **Launch development server**:
   ```bash
   npm run dev
   ```
5. **Access the Console**: Open [http://localhost:3000](http://localhost:3000) in your web browser.

