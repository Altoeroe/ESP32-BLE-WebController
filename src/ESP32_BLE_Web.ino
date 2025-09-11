#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>

// ==== WiFi AP Settings ====
const char* ssid = "ESP32_AP";
const char* password = "12345678";

WebServer server(80);
Preferences preferences;

// ==== BLE Settings ====
BLEScan* pBLEScan;
BLEClient* pClient;
static String savedDeviceAddr = "";
bool autoReconnect = false;

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
    max-width: 400px;
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
  }
  .device-btn:hover {
    background: #008e76;
    transform: scale(1.08);
  }

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
<p>Saved Device: <strong><span id="savedDevice">)rawliteral";

  page += savedDeviceAddr.length() ? savedDeviceAddr : "None";
  page += R"rawliteral(</span></strong></p>
<button onclick="scanDevices()">🔍 Scan Devices</button>
<button onclick="clearDevice()">♻ Re-Pair</button>
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
      html += `<button class="device-btn" onclick="selectDevice('${d.addr}')">${d.name || 'Unknown'} (${d.addr})</button>`;
    });
    if(devices.length === 0) html = '<p>No BLE devices found.</p>';
    document.getElementById('devices').innerHTML = html;
  }).catch(err=>{
    document.getElementById('loading').style.display = 'none';
    document.getElementById('devices').innerHTML = '<p style="color:red;">Scan failed.</p>';
  });
}
function selectDevice(addr) {
  fetch('/select?addr='+addr).then(r=>r.text()).then(res=>{
    alert(res);
    location.reload();
  });
}
function clearDevice() {
  fetch('/clear').then(r=>r.text()).then(res=>{
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

// ==== Web Handlers ====
void handleRoot() {
  server.send(200, "text/html", getHTMLPage());
}

void handleScan() {
  BLEScanResults* foundDevices = pBLEScan->start(3, false);  // pointer in core 3.x
  String json = "[";
  for (int i = 0; i < foundDevices->getCount(); i++) {
    BLEAdvertisedDevice dev = foundDevices->getDevice(i);
    json += "{\"name\":\"" + String(dev.getName().c_str()) +
            "\",\"addr\":\"" + String(dev.getAddress().toString().c_str()) + "\"}";
    if (i < foundDevices->getCount()-1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleSelect() {
  if (!server.hasArg("addr")) {
    server.send(400, "text/plain", "Missing addr");
    return;
  }
  savedDeviceAddr = server.arg("addr");
  preferences.putString("ble_addr", savedDeviceAddr);
  server.send(200, "text/plain", "Device saved: " + savedDeviceAddr);
  autoReconnect = true;
}

void handleClear() {
  // 🛠 Disconnect before clearing to fix re-pair bug
  if (pClient->isConnected()) {
    pClient->disconnect();
  }
  preferences.remove("ble_addr");
  savedDeviceAddr = "";
  server.send(200, "text/plain", "Device cleared. Please scan again.");
}

// ==== BLE Connection ====
void connectToSavedDevice() {
  if (savedDeviceAddr == "") return;
  Serial.println("Trying to connect to saved device: " + savedDeviceAddr);
  BLEAddress address(savedDeviceAddr.c_str());
  if (pClient->connect(address)) {
    Serial.println("✅ Connected to " + savedDeviceAddr);
    // GATT operations can be added here if needed
  } else {
    Serial.println("❌ Failed to connect.");
  }
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);

  // WiFi AP
  WiFi.softAP(ssid, password);
  Serial.println("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // NVS
  preferences.begin("ble", false);
  savedDeviceAddr = preferences.getString("ble_addr", "");

  // BLE
  BLEDevice::init("ESP32-BLE-Web");
  pClient = BLEDevice::createClient();
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);

  // Web Routes
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/select", handleSelect);
  server.on("/clear", handleClear);
  server.begin();

  // Auto reconnect if we have saved device
  if (savedDeviceAddr.length()) {
    autoReconnect = true;
  }
}

void loop() {
  server.handleClient();
  if (autoReconnect) {
    connectToSavedDevice();
    autoReconnect = false;
  }
}
