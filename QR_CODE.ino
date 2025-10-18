// ESP32 WiFi provisioning -> captive portal + show QR after 30s
// Requires: TFT_eSPI and qrcode_espi libraries
// Uses WebServer, DNSServer, Preferences (ESP32 core)

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

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

// Simple provisioning page (form)
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>ESP32 WiFi Provisioning</title>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>
      body{font-family:Arial,Helvetica,sans-serif;padding:20px;}
      input{padding:8px;margin:6px 0;width:100%;}
      button{padding:10px;width:100%;}
      .card{max-width:420px;margin:auto;}
    </style>
  </head>
  <body>
    <div class="card">
      <h2>Provision Wi-Fi</h2>
      <form method="POST" action="/save">
        <label>SSID</label><br>
        <input name="ssid" placeholder="Network name" required><br>
        <label>Password</label><br>
        <input name="pass" placeholder="Password (leave empty for open)" type="password"><br>
        <button type="submit">Save and Connect</button>
      </form>
      <p>After submitting, wait ~30 seconds and the QR will be shown on the device.</p>
    </div>
  </body>
</html>
)rawliteral";

// Serve root provisioning page
void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", index_html);
}

// Handle the form POST to save credentials
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

// Redirect unknown paths to root (captive portal behavior)
void handleNotFound() {
  String redirect = String("http://") + apIP.toString() + "/";
  server.sendHeader("Location", redirect, true);
  server.send(302, "text/plain", "");
}

// Help trigger OS captive-portal checks so device pop-up appears
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

// Show a small text on TFT
void showMessageOnDisplay(const char* line1, const char* line2 = nullptr) {
  display.fillScreen(TFT_BLACK);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 8);
  display.print(line1);
  if (line2) {
    display.setCursor(4, 40);
    display.print(line2);
  }
}

// Create the "Hello world." QR after provisioning delay
void showQRCodeHello() {
  display.fillScreen(TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  display.setCursor(4, 4);
  display.print("QR (Hello world):");
  qrcode.create("Hello world."); // draws QR to display
}

// Build and show QR for joining the ESP32 AP
void showAPQRCode() {
  display.fillScreen(TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 4);
  display.print("Scan to join Wi-Fi:");
  display.setCursor(4, 20);
  display.print(ap_ssid);

  String wifi_qr;
  if (strlen(ap_password) > 0) {
    wifi_qr = "WIFI:T:WPA;S:" + String(ap_ssid) + ";P:" + String(ap_password) + ";;";
  } else {
    wifi_qr = "WIFI:T:nopass;S:" + String(ap_ssid) + ";;";
  }

  qrcode.create(wifi_qr.c_str());
}

// Start AP + DNSServer + WebServer for captive portal
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

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("Starting provisioning + QR demo...");

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

  if (savedSsid.length() > 0) {
    Serial.println("Found saved credentials (in NVM). They will be used if you choose to auto-connect.");
    // If you want to auto-connect at boot (instead of starting AP), you could:
    // WiFi.mode(WIFI_STA); WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  }
}

void loop() {
  // DNS + web server handlers (only while captive portal running)
  if (captiveRunning) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  // If credentials were posted, wait ~30s then show the Hello QR
  if (credsReceived && !qrShown) {
    unsigned long now = millis();
    unsigned long elapsed = now - credsMillis;

    static bool reportedConnecting = false;
    if (!reportedConnecting) {
      Serial.printf("Attempting connection to SSID: %s\n", prov_ssid.c_str());
      reportedConnecting = true;
    }

    if (elapsed >= SHOW_QR_AFTER) {
      Serial.println("30s elapsed, showing Hello QR...");
      showQRCodeHello();
      qrShown = true;
    }
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
