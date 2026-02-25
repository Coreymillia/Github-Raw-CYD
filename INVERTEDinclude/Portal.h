#pragma once

#include <Arduino_GFX_Library.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// gfx is defined in main.cpp
extern Arduino_GFX *gfx;

// ---------------------------------------------------------------------------
// Persisted settings
// ---------------------------------------------------------------------------
static char wc_wifi_ssid[64]  = "";
static char wc_wifi_pass[64]  = "";
static char wc_raw_url[256]   = "";  // raw.githubusercontent.com URL
static int  wc_text_color_idx = 0;   // 0=white,1=green,2=cyan,3=yellow,4=orange,5=red,6=rainbow
static int  wc_text_size      = 1;   // 1=small, 2=medium, 3=large
static bool wc_has_settings   = false;

// ---------------------------------------------------------------------------
// Portal state
// ---------------------------------------------------------------------------
static WebServer  *portalServer = nullptr;
static DNSServer  *portalDNS    = nullptr;
static bool        portalDone   = false;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------
static void wcLoadSettings() {
  Preferences prefs;
  prefs.begin("githubraw", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  String url  = prefs.getString("url",  "");
  prefs.end();

  ssid.toCharArray(wc_wifi_ssid, sizeof(wc_wifi_ssid));
  pass.toCharArray(wc_wifi_pass, sizeof(wc_wifi_pass));
  url.toCharArray(wc_raw_url,   sizeof(wc_raw_url));
  wc_text_color_idx = prefs.getInt("coloridx", 0);
  wc_text_size      = prefs.getInt("textsize",  1);
  wc_has_settings   = (ssid.length() > 0);
}

static void wcSaveSettings(const char *ssid, const char *pass, const char *url, int colorIdx, int textSize) {
  Preferences prefs;
  prefs.begin("githubraw", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("url",  url);
  prefs.putInt("coloridx", colorIdx);
  prefs.putInt("textsize",  textSize);
  prefs.end();

  strncpy(wc_wifi_ssid, ssid, sizeof(wc_wifi_ssid) - 1);
  strncpy(wc_wifi_pass, pass, sizeof(wc_wifi_pass) - 1);
  strncpy(wc_raw_url,   url,  sizeof(wc_raw_url)   - 1);
  wc_text_color_idx = colorIdx;
  wc_text_size      = textSize;
  wc_has_settings   = true;
}

// ---------------------------------------------------------------------------
// On-screen setup instructions (320x240 landscape)
// ---------------------------------------------------------------------------
static void wcShowPortalScreen() {
  gfx->fillScreen(RGB565_BLACK);

  gfx->setTextColor(0x07FF);  // cyan
  gfx->setTextSize(2);
  gfx->setCursor(22, 5);
  gfx->print("GithubRaw Setup");

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(50, 26);
  gfx->print("Raw Text File Viewer");

  gfx->setTextColor(0xFFE0);  // yellow
  gfx->setCursor(4, 46);
  gfx->print("1. Connect to WiFi:");
  gfx->setTextColor(0x07FF);
  gfx->setTextSize(2);
  gfx->setCursor(14, 58);
  gfx->print("GithubRaw_Setup");

  gfx->setTextColor(0xFFE0);
  gfx->setTextSize(1);
  gfx->setCursor(4, 82);
  gfx->print("2. Open your browser and go to:");
  gfx->setTextColor(0x07FF);
  gfx->setTextSize(2);
  gfx->setCursor(50, 94);
  gfx->print("192.168.4.1");

  gfx->setTextColor(0xFFE0);
  gfx->setTextSize(1);
  gfx->setCursor(4, 118);
  gfx->print("3. Enter WiFi & raw URL, then");
  gfx->setCursor(4, 130);
  gfx->print("   tap Save & Connect.");

  if (wc_has_settings) {
    gfx->setTextColor(0x07E0);  // green
    gfx->setCursor(4, 152);
    gfx->print("Existing settings found. Tap");
    gfx->setCursor(4, 164);
    gfx->print("'No Changes' to keep them.");
  }
}

// ---------------------------------------------------------------------------
// Web handlers
// ---------------------------------------------------------------------------
static void wcHandleRoot() {
  String html = "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>GithubRaw Setup</title>"
    "<style>"
    "body{background:#001a33;color:#00ccff;font-family:Arial,sans-serif;"
         "text-align:center;padding:20px;max-width:480px;margin:auto;}"
    "h1{color:#00ffff;font-size:1.6em;margin-bottom:4px;}"
    "p{color:#88aacc;font-size:0.9em;}"
    "label{display:block;text-align:left;margin:14px 0 4px;color:#88ddff;font-weight:bold;}"
    "input{width:100%;box-sizing:border-box;background:#002244;color:#00ccff;"
          "border:2px solid #0066aa;border-radius:6px;padding:10px;font-size:1em;}"
    ".btn{display:block;width:100%;padding:14px;margin:10px 0;font-size:1.05em;"
         "border-radius:8px;border:none;cursor:pointer;font-weight:bold;}"
    ".btn-save{background:#004488;color:#00ffff;border:2px solid #0099dd;}"
    ".btn-save:hover{background:#0066bb;}"
    ".btn-skip{background:#1a1a2e;color:#667788;border:2px solid #334455;}"
    ".btn-skip:hover{background:#223344;color:#aabbcc;}"
    ".note{color:#445566;font-size:0.82em;margin-top:16px;}"
    "select{width:100%;box-sizing:border-box;background:#002244;color:#00ccff;"
           "border:2px solid #0066aa;border-radius:6px;padding:10px;font-size:1em;"
           "margin-bottom:4px;}"
    "hr{border:1px solid #113355;margin:20px 0;}"
    "</style></head><body>"
    "<h1>&#128196; GithubRaw Setup</h1>"
    "<p>Display any raw text file from GitHub on your CYD.</p>"
    "<form method='post' action='/save'>"
    "<label>WiFi Network Name (SSID):</label>"
    "<input type='text' name='ssid' value='";
  html += String(wc_wifi_ssid);
  html += "' placeholder='Your 2.4 GHz WiFi name' maxlength='63' required>"
    "<label>WiFi Password:</label>"
    "<input type='password' name='pass' value='";
  html += String(wc_wifi_pass);
  html += "' placeholder='Leave blank if open network' maxlength='63'>"
    "<label>Raw GitHub URL:</label>"
    "<input type='url' name='url' value='";
  html += String(wc_raw_url);
  html += "' placeholder='https://raw.githubusercontent.com/user/repo/main/file.txt'"
    " maxlength='255' required>";

  // Text color dropdown
  html += "<label>Text Color:</label><select name='color'>";
  const char* colorNames[] = {"White", "Green", "Cyan", "Yellow", "Orange", "Red", "&#127752; Rainbow (multi-color)"};
  for (int i = 0; i <= 6; i++) {
    html += "<option value='" + String(i) + "'";
    if (wc_text_color_idx == i) html += " selected";
    html += ">";
    html += colorNames[i];
    html += "</option>";
  }
  html += "</select>";

  // Text size dropdown
  html += "<label>Text Size:</label><select name='size'>";
  const char* sizeNames[] = {"", "Small (default)", "Medium", "Large"};
  for (int i = 1; i <= 3; i++) {
    html += "<option value='" + String(i) + "'";
    if (wc_text_size == i) html += " selected";
    html += ">";
    html += sizeNames[i];
    html += "</option>";
  }
  html += "</select>";

  html += "<br><button class='btn btn-save' type='submit'>&#128190; Save &amp; Connect</button>"
    "</form>";
  if (wc_has_settings) {
    html += "<hr>"
      "<form method='post' action='/nochange'>"
      "<button class='btn btn-skip' type='submit'>&#10006; No Changes &mdash; Use Current Settings</button>"
      "</form>";
  }
  html += "<p class='note'>&#9888; ESP32 supports 2.4 GHz WiFi networks only.</p>"
    "<p class='note'>The URL must start with <b>https://raw.githubusercontent.com/</b></p>"
    "</body></html>";

  portalServer->send(200, "text/html", html);
}

static void wcHandleSave() {
  String ssid = portalServer->hasArg("ssid") ? portalServer->arg("ssid") : "";
  String pass = portalServer->hasArg("pass") ? portalServer->arg("pass") : "";
  String url  = portalServer->hasArg("url")  ? portalServer->arg("url")  : "";

  if (ssid.length() == 0) {
    portalServer->send(400, "text/html",
      "<html><body style='background:#001a33;color:#ff5555;font-family:Arial;"
      "text-align:center;padding:40px'>"
      "<h2>&#10060; SSID cannot be empty!</h2>"
      "<a href='/' style='color:#00ccff'>&#8592; Go Back</a></body></html>");
    return;
  }

  wcSaveSettings(ssid.c_str(), pass.c_str(), url.c_str(),
    portalServer->hasArg("color") ? constrain(portalServer->arg("color").toInt(), 0, 6) : 0,
    portalServer->hasArg("size")  ? constrain(portalServer->arg("size").toInt(),  1, 3) : 1);

  String html = "<html><head><meta charset='UTF-8'>"
    "<style>body{background:#001a33;color:#00ccff;font-family:Arial;"
    "text-align:center;padding:40px;}h2{color:#00ffff;}"
    "p{color:#88aacc;}small{color:#445566;font-size:0.8em;word-break:break-all;}</style></head><body>"
    "<h2>&#9989; Settings Saved!</h2>"
    "<p>Connecting to <b>" + ssid + "</b>...</p>"
    "<p><small>" + url + "</small></p>"
    "<p>You can close this page and disconnect from <b>GithubRaw_Setup</b>.</p>"
    "</body></html>";
  portalServer->send(200, "text/html", html);

  delay(1500);
  portalDone = true;
}

static void wcHandleNoChange() {
  String html = "<html><head><meta charset='UTF-8'>"
    "<style>body{background:#001a33;color:#00ccff;font-family:Arial;"
    "text-align:center;padding:40px;}h2{color:#00ffff;}"
    "p{color:#88aacc;}</style></head><body>"
    "<h2>&#128077; No Changes</h2>"
    "<p>Using your saved settings. Device is connecting now.</p>"
    "<p>You can close this page and disconnect from <b>GithubRaw_Setup</b>.</p>"
    "</body></html>";
  portalServer->send(200, "text/html", html);

  delay(1500);
  portalDone = true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static void wcInitPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("GithubRaw_Setup", "");
  delay(500);

  portalDNS    = new DNSServer();
  portalServer = new WebServer(80);

  portalDNS->start(53, "*", WiFi.softAPIP());

  portalServer->on("/",         wcHandleRoot);
  portalServer->on("/save",     HTTP_POST, wcHandleSave);
  portalServer->on("/nochange", HTTP_POST, wcHandleNoChange);
  portalServer->onNotFound(wcHandleRoot);
  portalServer->begin();

  portalDone = false;
  wcShowPortalScreen();

  Serial.printf("[Portal] AP up — connect to GithubRaw_Setup, open %s\n",
                WiFi.softAPIP().toString().c_str());
}

static void wcRunPortal() {
  portalDNS->processNextRequest();
  portalServer->handleClient();
}

static void wcClosePortal() {
  portalServer->stop();
  portalDNS->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(300);

  delete portalServer; portalServer = nullptr;
  delete portalDNS;    portalDNS    = nullptr;
}
