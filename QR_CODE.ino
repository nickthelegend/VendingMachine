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

// product server state
bool productServerRunning = false;

// stored last product
String lastProductName = "";
String lastProductApiKey = "";
double lastProductPrice = 0.0;

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
  
  if (n == 0) {
    availableNetworks = "<option value=\"\">No networks found</option>";
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
  }
  Serial.printf("Found %d networks\n", n);
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

// Show payment QR with message "Pay X to dispense [name]"
void showPaymentQRCode(const String &productName, double price) {
  // Compose message for QR
  char buf[128];
  snprintf(buf, sizeof(buf), "Pay %.2f to dispense %s", price, productName.c_str());

  // White background so black text readable
  display.fillScreen(TFT_WHITE);

  // Print header + price
  display.setTextSize(2);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setCursor(4, 4);
  display.print("Pay to Dispense");

  display.setTextSize(1);
  display.setCursor(4, 36);
  display.print(productName);

  // Draw the QR below (adjust positions by moving cursor before drawing)
  // The qrcode library will draw its QR near top-left; to avoid text overlap,
  // we can shift the display's scroll or we can draw QR and then overlay text.
  // Here we'll place text at top then draw QR slightly lower by using a temporary
  // fill area and qrcode library default (it draws from top-left). To handle
  // positioning robustly, draw QR then overlay the header text on top.
  qrcode.create(buf); // draws QR (black modules on white)
  // Now overlay header so it remains visible (small y offsets)
  display.setTextSize(2);
  display.setCursor(4, 4);
  display.print("Pay to Dispense");
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

  // respond to client
  String resp = "<html><body><h3>Credentials saved.</h3>"
                "<p>Device will attempt to connect to <b>" + prov_ssid + "</b>.<br>"
                "Please wait ~30 seconds and check the device screen for the QR code.</p>"
                "</body></html>";
  server.send(200, "text/html", resp);

  // begin WiFi connect sequence (non-blocking)
  // stop captive portal services before switching to STA
  if (captiveRunning) {
    dnsServer.stop();
    server.stop();
    captiveRunning = false;
  }

  // stop AP and switch to station mode
  WiFi.softAPdisconnect(true);
  delay(200);

  WiFi.mode(WIFI_STA);
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

  // Compose verification URL: change domain/path as needed
  String verifyUrl = String("https://something.com/api/") + apikey;
  Serial.printf("Verifying API key via: %s\n", verifyUrl.c_str());

  // Perform HTTPS GET (note: setInsecure() used for simplicity)
  WiFiClientSecure *client = new WiFiClientSecure();
  client->setInsecure();

  HTTPClient https;
  if (!https.begin(*client, verifyUrl)) {
    Serial.println("HTTPS begin failed");
    delete client;
    server.send(500, "text/plain", "Failed to start HTTPS");
    return;
  }

  int httpCode = https.GET();
  String payload = "";
  if (httpCode > 0) {
    payload = https.getString();
    Serial.printf("HTTP %d, payload: %s\n", httpCode, payload.c_str());
  } else {
    Serial.printf("HTTPS GET failed, code: %d\n", httpCode);
  }
  https.end();
  delete client;

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
  if (doc.containsKey("success")) {
    success = doc["success"].as<bool>();
  }
  if (doc.containsKey("price")) {
  // If price is a string (JSON "price": "12.34"), read as const char*
  if (doc["price"].is<const char*>()) {
    const char* s = doc["price"].as<const char*>();
    price = atof(s); // convert string to double
  } else {
    // If price is numeric (JSON "price": 12.34), read as double
    price = doc["price"].as<double>();
  }
}
  if (!success) {
    server.send(403, "text/html", "<html><body><h3>Verification failed</h3><p>API said success: false</p></body></html>");
    return;
  }

  // Save last product and show QR on device
  lastProductName = product;
  lastProductApiKey = apikey;
  lastProductPrice = price;

  // Persist last product (optional)
  prefs.begin("product", false);
  prefs.putString("name", lastProductName);
  prefs.putString("apikey", lastProductApiKey);
  prefs.putDouble("price", lastProductPrice);
  prefs.end();

  // Show payment QR on TFT
  showPaymentQRCode(lastProductName, lastProductPrice);

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

  // show QR to join the AP
  showAPQRCode();

  // begin AP + captive portal
  setupAPandCaptivePortal();

  // load saved creds (optional)
  prefs.begin("provision", true);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  // load last product (optional)
  prefs.begin("product", true);
  lastProductName = prefs.getString("name", "");
  lastProductApiKey = prefs.getString("apikey", "");
  lastProductPrice = prefs.getDouble("price", 0.0);
  prefs.end();

  if (savedSsid.length() > 0) {
    Serial.println("Found saved credentials (in NVM). They will be used if you choose to auto-connect.");
    // If you want to auto-connect at boot (instead of starting AP), uncomment:
    // WiFi.mode(WIFI_STA); WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  }
}

void loop() {
  // DNS + web server handlers (only while captive portal running)
  if (captiveRunning) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  // If credentials were posted, check for internet connection then show QR
  if (credsReceived && !qrShown && WiFi.status() == WL_CONNECTED) {
    static bool reportedConnecting = false;
    if (!reportedConnecting) {
      Serial.printf("Connected to SSID: %s\n", prov_ssid.c_str());
      Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
      reportedConnecting = true;
    }

    // Test internet connectivity
    WiFiClient client;
    if (client.connect("google.com", 80)) {
      client.stop();
      Serial.println("Internet connected, showing Hello QR...");
      showPaymentQRCode("Waiting...", 0.0);
      qrShown = true;
    } else {
      Serial.println("WiFi connected but no internet access");
    }
  }

  // Once we are connected to STA, start product server if not started yet
  if (WiFi.status() == WL_CONNECTED && !productServerRunning) {
    Serial.printf("STA connected. IP: %s\n", WiFi.localIP().toString().c_str());
    // Start HTTP server on STA IP for product management
    setupProductServer();

    // Show a message telling user where to connect (on device display)
    String ipmsg = WiFi.localIP().toString();
    showMessageOnDisplay("Connected:", ipmsg.c_str(), TFT_WHITE);
    delay(1500);

    // If we have a previously saved product, show it
    if (lastProductName.length() > 0 && lastProductPrice > 0.0) {
      showPaymentQRCode(lastProductName, lastProductPrice);
    } else {
      // show product UI instruction
      showMessageOnDisplay("Open browser to:", ipmsg.c_str(), TFT_WHITE);
    }
  }

  // product server handling (works after server.begin())
  if (productServerRunning) {
    server.handleClient();
  }

  // Periodically print WiFi status (non-blocking)
  static unsigned long stTime = 0;
  if (millis() - stTime > 5000) {
    stTime = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("STA connected. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.printf("WiFi.status() = %d\n", WiFi.status());
    }
  }
}
