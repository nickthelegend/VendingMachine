// ESP32 WiFi provisioning -> show QR after 30s
// Requires: TFT_eSPI and qrcode_espi libraries
// Also uses WebServer (built into ESP32 core) and Preferences for persistence

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include <SPI.h>
#include <TFT_eSPI.h>
#include <qrcode_espi.h>

TFT_eSPI display = TFT_eSPI();
QRcode_eSPI qrcode(&display);

WebServer server(80);
Preferences prefs;

const char* ap_ssid = "ESP32-Setup";   // hotspot name
const char* ap_password = "setup1234"; // optional; make empty "" for open AP

// provisioning state
String prov_ssid = "";
String prov_pass = "";
bool credsReceived = false;
unsigned long credsMillis = 0;
bool qrShown = false;
const unsigned long SHOW_QR_AFTER = 30000UL; // 30 seconds

// HTML served to client (simple form)
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
      .card{max-width:400px;margin:auto;}
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

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", index_html);
}

void handleSave() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  // read form fields
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
  qrShown = false; // reset if previously shown

  // save persistently
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

  // begin attempting WiFi connection (non-blocking)
  connectToWiFi(prov_ssid.c_str(), prov_pass.c_str());
}

void connectToWiFi(const char* ssid, const char* pass) {
  Serial.printf("Stopping AP and switching to STA to connect to %s\n", ssid);
  // stop AP (so device uses station mode)
  WiFi.softAPdisconnect(true);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.println("WiFi.begin() called");
  // we will let loop() continue and not block here
}

void setupAPandServer() {
  Serial.printf("Starting AP: %s\n", ap_ssid);
  // Start soft AP (use password or open)
  if (strlen(ap_password) > 0) {
    WiFi.softAP(ap_ssid, ap_password);
  } else {
    WiFi.softAP(ap_ssid);
  }

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(apIP);

  // webserver endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Web server started on port 80");
}

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

void showQRCodeHello() {
  display.fillScreen(TFT_BLACK);
  // optional: show small message
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  display.setCursor(4, 4);
  display.print("QR (Hello world):");

  // Create the QR centered on screen.
  // qrcode.create accepts const char* or String; library handles drawing
  qrcode.create("Hello world.");
  // qrcode_espi typically draws QR to the display. If your version requires explicit draw,
  // check library docs. Most qrcode_espi implementations draw on create().
}

void showAPQRCode() {
  // Clear screen and show small header
  display.fillScreen(TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(4, 4);
  display.print("Scan to join Wi-Fi:");

  display.setCursor(4, 20);
  display.print(ap_ssid); // shows SSID text

  // Build Wi-Fi QR string in the standard format:
  // WIFI:T:<auth>;S:<SSID>;P:<password>;;
  String wifi_qr;
  if (strlen(ap_password) > 0) {
    // assume WPA/WPA2 for a non-empty password
    wifi_qr = "WIFI:T:WPA;S:" + String(ap_ssid) + ";P:" + String(ap_password) + ";;";
  } else {
    // open network (no password). Some scanners prefer T:nopass
    wifi_qr = "WIFI:T:nopass;S:" + String(ap_ssid) + ";;";
  }

  // Create the QR on the display (library draws it)
  // qrcode.create accepts const char*
  qrcode.create(wifi_qr.c_str());
}


void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("Starting provisioning + QR demo...");

  // init display + qrcode lib
  display.init();
  display.setRotation(0);
  qrcode.init();

  // show initial message
showAPQRCode();

  // start AP and server
  setupAPandServer();

  // load saved creds (optional)
  prefs.begin("provision", true);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  if (savedSsid.length() > 0) {
    Serial.println("Found saved credentials. Will attempt to use them after AP (if not replaced).");
    // If you want to auto-connect on boot instead of showing AP, comment out AP start above
    // and uncomment the WiFi connect lines below:
    // WiFi.mode(WIFI_STA);
    // WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  }
}

void loop() {
  // handle http clients while in AP
  server.handleClient();

  // If credentials were posted, we track 30s and show QR after delay
  if (credsReceived && !qrShown) {
    unsigned long now = millis();
    unsigned long elapsed = now - credsMillis;

    // Optional: report connection progress to serial once
    static bool reportedConnecting = false;
    if (!reportedConnecting) {
      Serial.printf("Attempting connection to SSID: %s\n", prov_ssid.c_str());
      reportedConnecting = true;
    }

    // After the timeout show the Hello QR
    if (elapsed >= SHOW_QR_AFTER) {
      Serial.println("30s elapsed, showing QR...");
      showQRCodeHello();
      qrShown = true;
    }
  }

  // optional: you can check WiFi status and print it (non-blocking)
  static unsigned long stTime = 0;
  if (millis() - stTime > 5000) { // every 5s
    stTime = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("STA connected. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.printf("WiFi.status() = %d\n", WiFi.status());
    }
  }
}
