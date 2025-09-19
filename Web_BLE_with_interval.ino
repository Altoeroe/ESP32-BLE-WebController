#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoBLE.h>
#include <ArduinoJson.h>

// ==== WiFi AP Settings ====
const char* ssid = "ESP32_AP";
const char* password = "12345678";

WebServer server(80);
Preferences preferences;

// ==== JSON Buffer ====
char jsonBuffer[512];
StaticJsonDocument<512> doc;

// ==== BLE Globals ====
BLEDevice connectedPeripheral;
BLECharacteristic hrChar;
BLECharacteristic customChar3;

unsigned long lastPollTime = 0;
const unsigned long pollInterval = 100; // ms

// ==== Serial Send Interval ====
unsigned long lastSendTime = 0;
uint32_t sendInterval = 5; // default in seconds

// Globals to hold last known values
uint16_t heartRateValue = 0;
String customChar3Value = "0";
String customChar1Value = "0";
String customChar2Value = "0";
uint8_t batteryLevelValue = 0;

bool hrReceived = false;
bool customChar3Received = false;

String savedDeviceAddr = "";
String savedDeviceName = "";
String savedDeviceNick = "";
bool autoReconnect = false;

// Auto-restart/watchdog if no service data received
unsigned long lastServiceReceived = 0;
const unsigned long serviceTimeout = 10000; // 10 seconds without service data -> restart

// ==== Forward Declarations ====
bool connectAndSetup(BLEDevice peripheral);
void pollCharacteristics();
void setECGStreaming(bool enable, BLEDevice peripheral);
void onCustomChar3Notify(BLEDevice central, BLECharacteristic characteristic);
void onHeartRateNotify(BLEDevice central, BLECharacteristic characteristic);
String toHexString(const unsigned char* data, int length);

// ==== Modern Web UI ====
String getHTMLPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 BLE Control</title>
<style>
  /* Animated Gradient Background */
  body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    margin: 0;
    min-height: 100vh;
    display: flex;
    justify-content: center;
    align-items: flex-start;
    padding: 20px;
    background: linear-gradient(-45deg, #1e1e1e, #121212, #1e1e1e, #292929);
    background-size: 400% 400%;
    animation: gradientBG 15s ease infinite;
    color: #eee;
  }
  @keyframes gradientBG {
    0% {background-position:0% 50%;}
    50% {background-position:100% 50%;}
    100% {background-position:0% 50%;}
  }

  /* Card Style with Smooth Entrance */
  .card {
    background: rgba(30,30,30,0.95);
    padding: 25px;
    border-radius: 20px;
    box-shadow: 0 8px 25px rgba(0,0,0,0.7);
    max-width: 480px;
    width: 100%;
    text-align: center;
    animation: cardFade 0.8s ease forwards;
    transform: translateY(20px);
    opacity: 0;
  }
  @keyframes cardFade {
    to { transform: translateY(0); opacity: 1; }
  }

  h2 { color: #4fc3f7; margin-bottom: 12px; }
  
  button {
    background: #4fc3f7;
    color: #121212;
    border: none;
    padding: 14px 22px;
    margin: 10px 5px;
    border-radius: 14px;
    font-size: 16px;
    cursor: pointer;
    transition: all 0.3s ease;
    box-shadow: 0 5px 15px rgba(0,0,0,0.5);
  }
  button:hover {
    background: #0288d1;
    transform: translateY(-3px) scale(1.05);
  }

  #devices {
    margin-top: 20px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    animation: fadeIn 1s ease;
  }
  .device-btn {
    background: #00bfa5;
    color: #fff;
    border-radius: 14px;
    padding: 14px;
    font-size: 14px;
    transition: all 0.3s ease;
    box-shadow: 0 3px 12px rgba(0,0,0,0.4);
    text-align: left;
  }
  .device-btn:hover {
    background: #008e76;
    transform: scale(1.02);
  }

  .saved-line { display:flex; justify-content:center; align-items:center; gap:8px; flex-wrap:wrap; }
  .pen { cursor:pointer; font-size:18px; color:#fff; padding:6px; border-radius:6px; background:#2b2b2b; box-shadow:0 2px 8px rgba(0,0,0,0.4); }
  .pen:hover { background:#3a3a3a; transform:translateY(-2px); }

  @keyframes fadeIn {
    from { opacity: 0; transform: translateY(10px);}
    to { opacity: 1; transform: translateY(0);}
  }

  #loading {
    display: none;
    margin-top: 15px;
    color: #fff;
  }
  .spinner {
    border: 4px solid #f3f3f3;
    border-top: 4px solid #4fc3f7;
    border-radius: 50%;
    width: 35px;
    height: 35px;
    animation: spin 1s linear infinite;
    margin: 0 auto 8px;
  }
  @keyframes spin {
    0% { transform: rotate(0deg);}
    100% { transform: rotate(360deg);}
  }

  /* Responsive */
  @media (max-width: 500px) {
    .card { padding: 20px; }
    button { padding: 12px 18px; font-size: 14px; }
    .device-btn { padding: 12px; font-size: 13px; }
  }
</style>
</head>
<body>
<div class="card">
<h2>ESP32 BLE Control</h2>
<p>Saved Device:</p>
<div class="saved-line">
<strong><span id="savedDevice">)rawliteral";

  // Compose displayed saved device (nick or name + addr)
  String displaySaved = "None";
  if (savedDeviceAddr.length()) {
    if (savedDeviceNick.length()) {
      displaySaved = savedDeviceNick + " (" + (savedDeviceName.length() ? savedDeviceName : "Unknown") + " / " + savedDeviceAddr + ")";
    } else {
      displaySaved = (savedDeviceName.length() ? savedDeviceName : "Unknown") + " (" + savedDeviceAddr + ")";
    }
  }
  page += displaySaved;
  page += R"rawliteral(</span></strong>
<span class="pen" title="Edit nickname" onclick="editNick()">✏️</span>
</div>
<button onclick="scanDevices()">🔍 Scan Devices</button>
<button onclick="clearDevice()">♻ Re-Pair</button>
<button onclick="fetch('/reboot').then(()=>alert('Rebooting...'))">🔄 Reset</button>
<p id="intervalText">Time interval set at )rawliteral";
page += String(sendInterval);
page += R"rawliteral( seconds</p>
<input type="number" id="intervalInput" value=")rawliteral";
page += String(sendInterval);
page += R"rawliteral(" min="1" style="padding:8px; border-radius:8px; width:80px; text-align:center;">
<button onclick="setInterval()">💾 Save Interval</button>

<div id="loading"><div class="spinner"></div><p>Scanning...</p></div>
<div id="devices"></div>
</div>
<script>
function scanDevices() {
  document.getElementById('loading').style.display = 'block';
  document.getElementById('devices').innerHTML = '';
  fetch('/scan').then(r=>r.json()).then(devices=>{
    document.getElementById('loading').style.display = 'none';
    let html = '';
    devices.forEach(d=>{
      // d.name and d.addr
      const safeName = d.name || 'Unknown';
      html += `<button class="device-btn" onclick="selectDevice('${d.addr}','${safeName.replace(/'/g,'\\\'').replace(/"/g,'\\\"')}')">${safeName} — ${d.addr}</button>`;
    });
    if(devices.length === 0) html = '<p>No Frontier BLE devices found.</p>';
    document.getElementById('devices').innerHTML = html;
  }).catch(err=>{
    document.getElementById('loading').style.display = 'none';
    document.getElementById('devices').innerHTML = '<p style="color:red;">Scan failed.</p>';
  });
}
function selectDevice(addr, name) {
  // include name when saving
  fetch('/select?addr='+encodeURIComponent(addr)+'&name='+encodeURIComponent(name)).then(r=>r.text()).then(res=>{
    alert(res);
    location.reload();
  });
}
function setInterval() {
  const sec = document.getElementById('intervalInput').value;
  fetch('/setInterval?sec='+sec).then(r=>r.text()).then(res=>{
    alert(res);
    document.getElementById('intervalText').innerText = "Time interval set at " + sec + " seconds";
  });
}
function clearDevice() {
  fetch('/clear').then(r=>r.text()).then(res=>{
    alert(res);
    location.reload();
  });
}
function editNick() {
  // If no saved device, ask to pair first
  const saved = document.getElementById('savedDevice').innerText;
  if (!saved || saved === 'None') {
    alert('No saved device to nickname. Please pair first.');
    return;
  }
  const nick = prompt('Enter a short nickname for this device:');
  if (nick === null) return; // cancelled
  // send to server
  const addrMatch = saved.match(/([0-9A-Fa-f:]{17})/);
  if (!addrMatch) {
    alert('Could not determine saved device address. Re-pair and try again.');
    return;
  }
  const addr = addrMatch[0];
  fetch('/setnick?addr='+encodeURIComponent(addr)+'&nick='+encodeURIComponent(nick)).then(r=>r.text()).then(res=>{
    alert(res);
    location.reload();
  });
}
</script>
</body>
</html>
)rawliteral";
  return page;
}

// ==== Web Routes ====
void handleRoot() { server.send(200, "text/html", getHTMLPage()); }

// ==== Reboot Handler ====
void handleReboot() {
  server.send(200, "text/plain", "ESP32 is rebooting...");
  delay(500);
  ESP.restart();
}

// ==== Scan: return only devices whose name starts with "Frontier" ====
void handleScan() {
  BLE.scan();
  delay(3000);
  String json = "[";
  while (BLE.available()) {
    BLEDevice d = BLE.available();
    String nm = String(d.localName().c_str());
    if (nm.length() && nm.startsWith("Frontier")) {
      json += "{\"name\":\"" + nm +
              "\",\"addr\":\"" + String(d.address().c_str()) + "\"},";
    }
  }
  if (json.endsWith(",")) json.remove(json.length()-1);
  json += "]";
  BLE.stopScan();
  server.send(200, "application/json", json);
}

void handleSelect() {
  if (!server.hasArg("addr")) {
    server.send(400, "text/plain", "Missing addr"); return;
  }
  String addr = server.arg("addr");
  String name = "";
  if (server.hasArg("name")) name = server.arg("name");

  savedDeviceAddr = addr;
  savedDeviceName = name;
  preferences.putString("ble_addr", savedDeviceAddr);
  preferences.putString("ble_name", savedDeviceName);
  // Clear any existing nickname for previous device - keep only one saved device nickname for simplicity
  // (If you want per-device nicknames, change storage strategy)
  preferences.putString("ble_nick", preferences.getString("ble_nick", "")); // keep existing nick if any
  server.send(200, "text/plain", "Device saved: " + savedDeviceName + " (" + savedDeviceAddr + ")");
  autoReconnect = true;
  delay(500);
  ESP.restart();   // 🔄 restart right after selecting a device
}

void handleClear() {
  if (connectedPeripheral) connectedPeripheral.disconnect();
  preferences.remove("ble_addr");
  preferences.remove("ble_name");
  preferences.remove("ble_nick");
  savedDeviceAddr = "";
  savedDeviceName = "";
  savedDeviceNick = "";
  server.send(200, "text/plain", "Device cleared. Please scan again.");
}

void handleSetInterval() {
  if (!server.hasArg("sec")) {
    server.send(400, "text/plain", "Missing sec"); 
    return;
  }
  sendInterval = server.arg("sec").toInt();
  if (sendInterval < 1) sendInterval = 1; // minimum 1 second
  preferences.putUInt("send_interval", sendInterval);
  server.send(200, "text/plain", "Interval updated: " + String(sendInterval) + " seconds");
}

void handleSetNick() {
  if (!server.hasArg("addr") || !server.hasArg("nick")) {
    server.send(400, "text/plain", "Missing addr or nick"); return;
  }
  String addr = server.arg("addr");
  String nick = server.arg("nick");
  // Only allow setting nick for currently saved device addr
  if (addr != savedDeviceAddr) {
    server.send(400, "text/plain", "Address mismatch with saved device.");
    return;
  }
  savedDeviceNick = nick;
  preferences.putString("ble_nick", savedDeviceNick);
  server.send(200, "text/plain", "Nickname saved: " + savedDeviceNick);
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  while (!Serial);

  // WiFi AP
  WiFi.softAP(ssid, password);
  Serial.println(WiFi.softAPIP());

  // NVS
  preferences.begin("ble", false);
  savedDeviceAddr = preferences.getString("ble_addr", "");
  savedDeviceName = preferences.getString("ble_name", "");
  savedDeviceNick = preferences.getString("ble_nick", "");

  sendInterval = preferences.getUInt("send_interval", 5); // default 5s

  // BLE
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!"); while (1);
  }
  Serial.println("BLE Central ready");

  // Web Routes
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/select", handleSelect);
  server.on("/clear", handleClear);
  server.on("/reboot", handleReboot);
  server.on("/setnick", handleSetNick);
  server.on("/setInterval", handleSetInterval);

  server.begin();

  if (savedDeviceAddr.length()) autoReconnect = true;

  // initialize watchdog baseline
  lastServiceReceived = millis();
}

// ==== Loop ====
void loop() {
  server.handleClient();
  BLE.poll();

  // Auto-reconnect to saved device
  if (!connectedPeripheral && autoReconnect && savedDeviceAddr.length()) {
    Serial.println("Scanning for saved device: " + savedDeviceAddr);
    BLE.scan();
    delay(3000);
    while (BLE.available()) {
      BLEDevice d = BLE.available();
      if (String(d.address().c_str()) == savedDeviceAddr) {
        BLE.stopScan();
        if (connectAndSetup(d)) {
          connectedPeripheral = d;
          lastPollTime = millis();
          autoReconnect = false;
          lastServiceReceived = millis(); // reset watchdog on connect
        }
      }
    }
    BLE.stopScan();
  }

  if (connectedPeripheral && connectedPeripheral.connected()) {
    unsigned long now = millis();

    // Watchdog: restart if we haven't received service data recently
    if (now - lastServiceReceived > serviceTimeout) {
      Serial.println("No service data received for " + String(serviceTimeout) + " ms. Restarting...");
      delay(200);
      ESP.restart();
    }

    if (now - lastPollTime >= pollInterval) {
      lastPollTime = now;
      pollCharacteristics();

      doc["HR"] = heartRateValue;
      doc["B"] = batteryLevelValue;
      doc["CC1"] = customChar1Value;
      doc["CC2"] = customChar2Value;
      doc["CC3"] = customChar3Value;
      serializeJson(doc, jsonBuffer);

      // Only send at defined interval
      unsigned long now = millis();
      if (now - lastSendTime >= sendInterval * 1000UL) {
        Serial.println(jsonBuffer);
        lastSendTime = now;
      }

      if (!hrReceived) heartRateValue = 0;
      if (!customChar3Received) customChar3Value = "0";
      hrReceived = false;
      customChar3Received = false;
    }
  }
}

// ==== BLE Service Logic ====
bool connectAndSetup(BLEDevice peripheral) {
  if (!peripheral.connect()) return false;
  if (!peripheral.discoverAttributes()) return false;

  customChar3 = peripheral.characteristic("9f154f03-2020-11e6-8749-0002a5d5c51b");
  if (customChar3 && customChar3.subscribe()) {
    customChar3.setEventHandler(BLECharacteristicEvent::BLEUpdated, onCustomChar3Notify);
  } else {
    Serial.println("Failed to subscribe to customChar3");
    return false;
  }

  hrChar = peripheral.characteristic("2a37");
  if (hrChar && hrChar.subscribe()) {
    hrChar.setEventHandler(BLECharacteristicEvent::BLEUpdated, onHeartRateNotify);
  } else {
    Serial.println("HR char not available or failed to subscribe (continuing if optional).");
    // Not returning false here because HR might be optional for some devices
  }

  delay(500);
  setECGStreaming(true, peripheral);

  // Reset watchdog baseline when connection established
  lastServiceReceived = millis();
  return true;
}

void pollCharacteristics() {
  if (!connectedPeripheral) return;

  BLECharacteristic customChar1 = connectedPeripheral.characteristic("9f154f01-2020-11e6-8749-0002a5d5c51b");
  BLECharacteristic batteryChar = connectedPeripheral.characteristic("2a19");

  if (customChar1 && customChar1.canRead() && customChar1.read())
    customChar1Value = "0x" + toHexString(customChar1.value(), customChar1.valueLength());
  else customChar1Value = "0";

  customChar2Value = "N/A";

  if (batteryChar && batteryChar.canRead() && batteryChar.read())
    batteryLevelValue = batteryChar.value()[0];
  else batteryLevelValue = 0;
}

void onCustomChar3Notify(BLEDevice central, BLECharacteristic characteristic) {
  customChar3Value = "0x" + toHexString(characteristic.value(), characteristic.valueLength());
  customChar3Received = true;
  lastServiceReceived = millis(); // reset watchdog
}

void onHeartRateNotify(BLEDevice central, BLECharacteristic characteristic) {
  const unsigned char* data = characteristic.value();
  int len = characteristic.valueLength();
  if (len < 2) return;
  uint8_t flags = data[0];
  heartRateValue = (flags & 0x01 && len >= 3) ? (data[1] | (data[2] << 8)) : data[1];
  hrReceived = true;
  lastServiceReceived = millis(); // reset watchdog
}

String toHexString(const unsigned char* data, int length) {
  String hexStr = "";
  for (int i = 0; i < length; i++) {
    if (data[i] < 16) hexStr += "0";
    hexStr += String(data[i], HEX);
  }
  hexStr.toUpperCase();
  return hexStr;
}

void setECGStreaming(bool enable, BLEDevice peripheral) {
  BLECharacteristic commandChar = peripheral.characteristic("9f154f02-2020-11e6-8749-0002a5d5c51b");
  if (!commandChar) return;
  uint8_t cmd[2] = {0x30, 0x80};
  commandChar.writeValue(cmd, 2, false);
}
