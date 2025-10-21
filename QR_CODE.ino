// Full sketch: provisioning -> check API key -> show "Pay X to dispense" QR on TFT
// Requires: TFT_eSPI, qrcode_espi, ArduinoJson
// Uses: WebServer, DNSServer, Preferences, WiFiClientSecure, HTTPClient

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include <SPI.h>
#include <TFT_eSPI.h>
#include <qrcode_espi.h>

// Display + QR
TFT_eSPI display = TFT_eSPI();
QRcode_eSPI qrcode(&display);

// Web + DNS + prefs
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

// AP config
const char* ap_ssid = "ESP32-Setup";
const char* ap_password = "setup1234"; // set to "" for open AP
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

// provisioning state
String prov_ssid = "";
String prov_pass = "";
bool credsReceived = false;
unsigned long credsMillis = 0;
bool qrShown = false;
const unsigned long SHOW_QR_AFTER = 30000UL; // 30 seconds

bool captiveRunning = false;
String availableNetworks = "";
bool setupComplete = false;

// product server state
bool productServerRunning = false;

// stored last product
String lastProductName = "";
String lastProductApiKey = "";
double lastProductPrice = 0.0;
String lastDeviceId = "";

// WebSocket
WebSocketsClient webSocket;
bool wsConnected = false;
int joinRef = 1;
int messageRef = 1;

// ---------- HTML pages ----------

String generateProvisioningPage() {
  String html = R"rawliteral(
<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>ESP32 WiFi Provisioning</title>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>
      body{font-family:Arial,Helvetica,sans-serif;padding:20px;}
      input,select{padding:8px;margin:6px 0;width:100%;}
      button{padding:10px;width:100%;}
      .card{max-width:420px;margin:auto;}
    </style>
  </head>
  <body>
    <div class="card">
      <h2>Provision Wi-Fi</h2>
      <form method="POST" action="/save">
        <label>Select Network</label><br>
        <select name="ssid" required>
          <option value="">Choose a network...</option>
)rawliteral";
  
  html += availableNetworks;
  
  html += R"rawliteral(
        </select><br>
        <label>Password</label><br>
        <input name="pass" placeholder="Password (leave empty for open)" type="password"><br>
        <button type="submit">Save and Connect</button>
      </form>
      <p>After submitting, wait ~30 seconds and check the device screen for the QR code.</p>
    </div>
  </body>
</html>
)rawliteral";
  
  return html;
}

String generateProductPage(const char* msg = nullptr) {
  String notice = "";
  if (msg) {
    notice = String("<p style='color:green;'>" ) + msg + "</p>";
  }
  String html = R"rawliteral(
<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Device - Add Product</title>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>
      body{font-family:Arial,Helvetica,sans-serif;padding:20px;}
      input{padding:8px;margin:6px 0;width:100%;}
      button{padding:10px;width:100%;}
      .card{max-width:420px;margin:auto;}
      label{font-weight:600;}
    </style>
  </head>
  <body>
    <div class="card">
      <h2>Add New Product</h2>
      )rawliteral";
  html += notice;
  html += R"rawliteral(
      <form method="POST" action="/add">
        <label>Product name</label><br>
        <input name="product" placeholder="e.g. Soda can" required><br>
        <label>API key</label><br>
        <input name="apikey" placeholder="Your API key" required><br>
        <button type="submit">Verify & Show Payment QR</button>
      </form>
      <p>If verification succeeds, the device screen will show a payment QR with the price.</p>
    </div>
  </body>
</html>
)rawliteral";
  return html;
}

// ---------- WiFi scan ----------

void scanWiFiNetworks() {
  Serial.println("Scanning WiFi networks...");
  int n = WiFi.scanNetworks();
  availableNetworks = "";
  
  if (n <= 0) {
    availableNetworks = "<option value=\"\">No networks found</option>";
    Serial.printf("WiFi scan failed or no networks found: %d\n", n);
  } else {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() > 0) {
        availableNetworks += "<option value=\"" + ssid + "\">" + ssid;
        if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
          availableNetworks += " 🔒";
        }
        availableNetworks += "</option>";
      }
    }
    Serial.printf("Found %d networks\n", n);
  }
}

// ---------- Display helpers ----------

void showMessageOnDisplay(const char* line1, const char* line2 = nullptr, uint16_t bg = TFT_BLACK) {
  display.fillScreen(bg);
  display.setTextSize(2);
  if (bg == TFT_WHITE) display.setTextColor(TFT_BLACK, bg);
  else display.setTextColor(TFT_WHITE, bg);
  display.setCursor(4, 8);
  display.print(line1);
  if (line2) {
    display.setCursor(4, 40);
    display.print(line2);
  }
}

void showAPQRCode() {
  // White background so black text is visible
  display.fillScreen(TFT_WHITE);

  // Build WIFI QR payload
  String wifi_qr;
  if (strlen(ap_password) > 0) {
    wifi_qr = "WIFI:T:WPA;S:" + String(ap_ssid) + ";P:" + String(ap_password) + ";;";
  } else {
    wifi_qr = "WIFI:T:nopass;S:" + String(ap_ssid) + ";;";
  }

  // Draw the QR first (the library draws modules in black on white)
  qrcode.create(wifi_qr.c_str());

  // Now draw the header and SSID on top in black (with white background)
  display.setTextSize(2);                     // adjust as needed
  display.setTextColor(TFT_BLACK, TFT_WHITE); // black text, white bg
  display.setCursor(4, 4);
  display.print("Scan to Connect");

  display.setTextSize(1);
  display.setCursor(4, 36);                   // move down a bit
  display.print(ap_ssid);
}

// Show payment QR with website URL
void showPaymentQRCode(const String &productName, double price, const String &deviceId) {
  // Create payment URL
  String paymentUrl = "https://vendchain.abcxjntuh.in/pay/" + deviceId;

  // White background so black text readable
  display.fillScreen(TFT_WHITE);

  // Draw the QR code with payment URL
  qrcode.create(paymentUrl.c_str());

  // Overlay header text
  display.setTextSize(2);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setCursor(4, 4);
  display.print("Scan to Pay");

  display.setTextSize(1);
  display.setCursor(4, 36);
  display.print(productName);
}

// ---------- Web handlers (captive portal) ----------

void handleRoot() {
  scanWiFiNetworks();
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", generateProvisioningPage());
}

void handleSave() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String s = server.arg("ssid");
  String p = server.arg("pass");

  if (s.length() == 0) {
    server.send(400, "text/plain", "SSID required");
    return;
  }

  prov_ssid = s;
  prov_pass = p;
  credsReceived = true;
  credsMillis = millis();
  qrShown = false;

  // persist credentials
  prefs.begin("provision", false);
  prefs.putString("ssid", prov_ssid);
  prefs.putString("pass", prov_pass);
  prefs.end();

  // respond to client - redirect to product page
  String redirect = String("http://") + apIP.toString() + "/product";
  server.sendHeader("Location", redirect, true);
  server.send(302, "text/plain", "");

  // begin WiFi connect sequence (non-blocking) but keep AP running
  WiFi.mode(WIFI_AP_STA); // dual mode - keep AP + start STA
  WiFi.begin(prov_ssid.c_str(), prov_pass.c_str());
  Serial.println("WiFi.begin() called (attempting station connection)");
}

void handleNotFound() {
  String redirect = String("http://") + apIP.toString() + "/";
  server.sendHeader("Location", redirect, true);
  server.send(302, "text/plain", "");
}

void handleGenerate204() {
  String redirect = String("http://") + apIP.toString() + "/";
  server.sendHeader("Location", redirect, true);
  server.send(302, "text/plain", "");
}
void handleHotspotDetect() {
  server.sendHeader("Location", String("http://") + apIP.toString() + "/", true);
  server.send(302, "text/plain", "");
}
void handleNcsi() {
  String redirect = String("http://") + apIP.toString() + "/";
  server.sendHeader("Location", redirect, true);
  server.send(302, "text/plain", "");
}

// ---------- Product server (after STA connected) ----------

void handleProductRoot() {
  // Serve the product page
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", generateProductPage());
}

// POST /add -> verify API key by calling external API and show QR if ok
void handleAddProduct() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String product = server.arg("product");
  String apikey = server.arg("apikey");

  if (product.length() == 0 || apikey.length() == 0) {
    server.send(400, "text/plain", "Product and API key required");
    return;
  }

  // Use HTTP client to avoid SSL issues
  HTTPClient http;
  String httpUrl = String("https://merchant.abcxjntuh.in/api/") + apikey;
  Serial.printf("Verifying API key via: %s\n", httpUrl.c_str());
  
  if (!http.begin(httpUrl)) {
    Serial.println("HTTP begin failed");
    server.send(500, "text/plain", "Failed to start HTTP");
    return;
  }

  int httpCode = http.GET();
  String payload = "";
  if (httpCode > 0) {
    payload = http.getString();
    Serial.printf("HTTP %d, payload: %s\n", httpCode, payload.c_str());
  } else {
    Serial.printf("HTTP GET failed, code: %d\n", httpCode);
  }
  http.end();

  if (httpCode <= 0) {
    server.send(502, "text/html", "<html><body><h3>Connection failed</h3><p>Could not connect to API server. Check internet connection.</p></body></html>");
    return;
  }
  
  if (httpCode != 200) {
    server.send(502, "text/html", "<html><body><h3>Verification failed</h3><p>External API returned non-200.</p></body></html>");
    return;
  }

  // Parse JSON response
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("JSON parse failed");
    server.send(500, "text/html", "<html><body><h3>Verification failed</h3><p>Invalid JSON from API.</p></body></html>");
    return;
  }

  bool success = false;
  double price = 0.0;
  String deviceId = "";
  
  if (doc.containsKey("success")) {
    success = doc["success"].as<bool>();
  }
  if (doc.containsKey("price")) {
    price = doc["price"].as<double>();
  }
  if (doc.containsKey("deviceId")) {
    deviceId = doc["deviceId"].as<String>();
  }
  if (!success) {
    server.send(403, "text/html", "<html><body><h3>Verification failed</h3><p>API said success: false</p></body></html>");
    return;
  }

  // Save last product and show QR on device
  lastProductName = product;
  lastProductApiKey = apikey;
  lastProductPrice = price;
  lastDeviceId = deviceId;

  // Persist last product (optional)
  prefs.begin("product", false);
  prefs.putString("name", lastProductName);
  prefs.putString("apikey", lastProductApiKey);
  prefs.putDouble("price", lastProductPrice);
  prefs.putString("deviceId", lastDeviceId);
  prefs.end();

  // Show payment QR on TFT
  Serial.printf("Showing payment QR for: %s, price: %.2f, deviceId: %s\n", product.c_str(), price, deviceId.c_str());
  showPaymentQRCode(lastProductName, lastProductPrice, lastDeviceId);

  // Mark setup as complete but keep services running to prevent restart
  setupComplete = true;
  Serial.println("Product setup complete - setupComplete flag set to true");
  
  // Start WebSocket connection for payment listening
  setupWebSocket();

  // Respond to client with simple success page
  String resp = "<html><body><h3>Verified</h3><p>Price: " + String(price, 2) + "</p>"
                "<p>Check the device screen for the payment QR.</p></body></html>";
  server.send(200, "text/html", resp);
}

// ---------- Start AP + captive portal ----------

void setupAPandCaptivePortal() {
  Serial.printf("Starting AP: %s\n", ap_ssid);

  WiFi.mode(WIFI_AP);
  // configure AP IP and gateway
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  if (strlen(ap_password) > 0) {
    WiFi.softAP(ap_ssid, ap_password);
  } else {
    WiFi.softAP(ap_ssid);
  }

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP().toString());

  // start DNS server to capture all domains
  dnsServer.start(DNS_PORT, "*", apIP);

  // webserver handlers
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/product", HTTP_GET, handleProductRoot);
  server.on("/add", HTTP_POST, handleAddProduct);
  server.on("/generate_204", HTTP_ANY, handleGenerate204);        // Android
  server.on("/hotspot-detect.html", HTTP_ANY, handleHotspotDetect); // iOS
  server.on("/ncsi.txt", HTTP_ANY, handleNcsi);                 // Windows
  server.onNotFound(handleNotFound);

  server.begin();
  captiveRunning = true;
  Serial.println("Captive portal started (DNS + HTTP).");
}

// ---------- Start product server on STA IP ----------

void setupProductServer() {
  // Stop any previous handlers, re-use same server object
  server.stop();
  delay(50);

  // Setup handlers for product UI
  server.on("/", HTTP_GET, handleProductRoot);
  server.on("/add", HTTP_POST, handleAddProduct);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  productServerRunning = true;

  Serial.printf("Product server started at http://%s\n", WiFi.localIP().toString().c_str());
}

// ---------- WebSocket Functions ----------

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WSc] Disconnected!");
      wsConnected = false;
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WSc] Connected to Supabase!");
      wsConnected = true;
      subscribeToPaymentChannel();
      break;
      
    case WStype_TEXT:
      handleWebSocketMessage(payload, length);
      break;
      
    case WStype_ERROR:
      Serial.println("[WSc] Connection error!");
      break;
  }
}

void subscribeToPaymentChannel() {
  if (lastDeviceId.length() == 0) return;
  
  StaticJsonDocument<512> doc;
  doc["topic"] = "realtime:machine-" + lastDeviceId;
  doc["event"] = "phx_join";
  doc["join_ref"] = String(joinRef);
  doc["ref"] = String(messageRef);
  
  JsonObject payload = doc.createNestedObject("payload");
  JsonObject config = payload.createNestedObject("config");
  JsonObject broadcast = config.createNestedObject("broadcast");
  broadcast["self"] = true;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("[WSc] Subscribing to payment channel: " + lastDeviceId);
  webSocket.sendTXT(jsonString);
  
  joinRef++;
  messageRef++;
}

void handleWebSocketMessage(uint8_t * payload, size_t length) {
  String message = String((char*)payload);
  Serial.println("[WSc] Received: " + message);
  
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, message) != DeserializationError::Ok) return;
  
  String event = doc["event"];
  
  if (event == "broadcast") {
    JsonObject broadcastPayload = doc["payload"];
    String eventName = broadcastPayload["event"];
    
    if (eventName == "payment_approved") {
      String txnId = broadcastPayload["payload"]["txnId"];
      String machineId = broadcastPayload["payload"]["machineId"];
      
      Serial.println("=== PAYMENT APPROVED ===");
      Serial.println("Transaction ID: " + txnId);
      Serial.println("Machine ID: " + machineId);
      Serial.println("=========================");
    }
  }
}

void setupWebSocket() {
  if (lastDeviceId.length() == 0) return;
  
  String path = "/realtime/v1/websocket?apikey=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImxobmJpcGdzeHJ2b25iYmxjZWt3Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjA3NTc5NDgsImV4cCI6MjA3NjMzMzk0OH0.N4oalFAo-1TpjrU12bZ7PHNLwMhD2S3rHe339AW_m3M&log_level=info&vsn=1.0.0";
  
  Serial.println("Setting up WebSocket for device: " + lastDeviceId);
  webSocket.beginSSL("lhnbipgsxrvonbblcekw.supabase.co", 443, path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

// ---------- Clear saved data ----------

void clearSavedData() {
  prefs.begin("provision", false);
  prefs.clear();
  prefs.end();
  
  prefs.begin("product", false);
  prefs.clear();
  prefs.end();
  
  Serial.println("All saved data cleared. Restarting...");
  ESP.restart();
}

// ---------- Setup & Loop ----------

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("Starting provisioning + payment QR demo...");

  // init display + qrcode library
  display.init();
  display.setRotation(0);
  qrcode.init();

  // Always start with captive portal - no auto-loading of saved product data

  // show QR to join the AP
  showAPQRCode();

  // begin AP + captive portal
  setupAPandCaptivePortal();

  // load saved WiFi credentials
  prefs.begin("provision", true);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  if (savedSsid.length() > 0) {
    Serial.println("Found saved credentials (in NVM). They will be used if you choose to auto-connect.");
    // If you want to auto-connect at boot (instead of starting AP), uncomment:
    // WiFi.mode(WIFI_STA); WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  }
}

void loop() {
  // If setup is complete, handle WebSocket and maintain payment QR display
  if (setupComplete) {
    static bool loggedComplete = false;
    if (!loggedComplete) {
      Serial.println("Setup complete - maintaining payment QR display");
      loggedComplete = true;
    }
    webSocket.loop();
    delay(100);
    return;
  }

  // DNS + web server handlers (only while captive portal running)
  if (captiveRunning) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  // Just report connection status, don't show QR yet (unless setup is complete)
  if (credsReceived && WiFi.status() == WL_CONNECTED && !setupComplete) {
    static bool reportedConnecting = false;
    if (!reportedConnecting) {
      Serial.printf("Connected to SSID: %s\n", prov_ssid.c_str());
      Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
      showMessageOnDisplay("WiFi Connected", "Enter product info", TFT_WHITE);
      reportedConnecting = true;
    }
  }

  // Show connection status when connected
  static bool connectionShown = false;
  if (WiFi.status() == WL_CONNECTED && !connectionShown) {
    Serial.printf("STA connected. IP: %s\n", WiFi.localIP().toString().c_str());
    connectionShown = true;
  }
}
