// GithubRaw - GitHub Raw Text File Viewer for CYD (Cheap Yellow Display)
// Fetches a plain text file from raw.githubusercontent.com and displays it.
// Setup: First boot opens GithubRaw_Setup AP for configuration.
//        On subsequent boots, hold the BOOT button during startup to re-enter setup.
//        WiFi credentials and URL are persisted to NVS flash.

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include <Arduino_GFX_Library.h>
#include <XPT2046_Touchscreen.h>
#include "Portal.h"
#include "HTTPS.h"

// Text color palettes
static const uint16_t TEXT_COLORS[] = {
  0xFFFF,  // 0 white
  0x07E0,  // 1 green
  0x07FF,  // 2 cyan
  0xFFE0,  // 3 yellow
  0xFD20,  // 4 orange
  0xF800,  // 5 red
};
static const uint16_t MULTI_COLORS[] = {
  0x07FF, 0x07E0, 0xFFE0, 0xFD20, 0xF800, 0xF81F, 0xFFFF
};
#define MULTI_COLOR_COUNT 7

/*******************************************************************************
 * Display setup - CYD (Cheap Yellow Display) proven working config
 * ILI9341 320x240 landscape via hardware SPI
 ******************************************************************************/
#define GFX_BL 21

Arduino_DataBus *bus = new Arduino_HWSPI(
    2  /* DC */,
    15 /* CS */,
    14 /* SCK */,
    13 /* MOSI */,
    12 /* MISO */);

Arduino_GFX *gfx = new Arduino_ILI9341(bus, GFX_NOT_DEFINED, 1 /* landscape */);

/*******************************************************************************
 * Touch screen - XPT2046 on separate VSPI bus
 ******************************************************************************/
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define TOUCH_DEBOUNCE 250

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
static unsigned long lastTouchTime = 0;
/*******************************************************************************
 * End of display setup
 ******************************************************************************/

#define UPDATE_INTERVAL (15UL * 60UL * 1000UL)  // refresh every 15 minutes
#define BOOT_LONG_MS    800UL                    // hold threshold: long press = re-fetch

// Cached body and pagination state
static String wc_body       = "";
static int    wc_page_start = 0;  // char offset of current page
static int    wc_page_next  = -1; // char offset of next page (-1 = end of content)

// Page history stack for going backwards (up to 64 pages)
static int    wc_page_hist[64];
static int    wc_hist_size  = 0;

// Print a status line in the top bar
void showStatus(const char *msg) {
  gfx->fillRect(0, 0, gfx->width(), 20, RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(4, 6);
  gfx->print(msg);
  Serial.println(msg);
}

// Draw UTC timestamp in the bottom-right corner
void drawTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  char buf[12];
  strftime(buf, sizeof(buf), "%H:%M UTC", &timeinfo);
  int tw = strlen(buf) * 6;
  int tx = gfx->width()  - tw - 3;
  int ty = gfx->height() - 10;
  gfx->fillRect(tx - 1, ty - 1, tw + 2, 10, RGB565_BLACK);
  gfx->setTextColor(0x7BEF);  // gray
  gfx->setTextSize(1);
  gfx->setCursor(tx, ty);
  gfx->print(buf);
}

// Render the current page from wc_page_start. Sets wc_page_next (-1 = end).
void renderPage() {
  if (wc_body.isEmpty()) return;

  int sz = constrain(wc_text_size, 1, 3);
  gfx->setTextSize(sz);

  const int lineH   = 8 * sz + 2;
  const int charW   = 6 * sz;
  const int maxX    = 4;
  const int startY  = 24;
  const int maxY    = gfx->height() - 14;
  const int maxCols = (gfx->width() - maxX - 4) / charW;

  gfx->fillRect(0, 20, gfx->width(), gfx->height() - 20, RGB565_BLACK);

  int y         = startY;
  int lineStart = wc_page_start;
  int bodyLen   = wc_body.length();
  int colorStep = 0;

  wc_page_next = -1;  // assume we'll reach the end

  while (lineStart <= bodyLen) {
    int savedLineStart = lineStart;
    int lineEnd = wc_body.indexOf('\n', lineStart);
    if (lineEnd == -1) lineEnd = bodyLen;

    String line = wc_body.substring(lineStart, lineEnd);
    if (line.endsWith("\r")) line.remove(line.length() - 1);

    bool pageBreak = false;
    while (line.length() > 0) {
      if (y + lineH > maxY) {
        wc_page_next = savedLineStart;  // resume from start of this line
        pageBreak = true;
        break;
      }
      String segment;
      if ((int)line.length() <= maxCols) {
        segment = line;
        line    = "";
      } else {
        int cut = maxCols;
        for (int i = maxCols; i > 0; i--) {
          if (line[i] == ' ') { cut = i; break; }
        }
        segment = line.substring(0, cut);
        line    = line.substring(cut);
        line.trim();
      }
      if (wc_text_color_idx == 6) {
        gfx->setTextColor(MULTI_COLORS[colorStep % MULTI_COLOR_COUNT]);
      } else {
        gfx->setTextColor(TEXT_COLORS[wc_text_color_idx]);
      }
      gfx->setCursor(maxX, y);
      gfx->print(segment);
      y += lineH;
      colorStep++;
    }
    if (pageBreak) break;
    lineStart = lineEnd + 1;
  }

  // Bottom-left hint
  gfx->setTextSize(1);
  gfx->setTextColor(0x7BEF);  // gray
  gfx->setCursor(4, gfx->height() - 10);
  if (wc_page_next == -1) {
    gfx->print("< prev   restart >   hold=refetch");
  } else {
    gfx->print("< prev     next >    hold=refetch");
  }
  drawTimestamp();
}

// Fetch content and render first page. Returns true on success.
bool fetchAndRender() {
  if (strlen(wc_raw_url) == 0) {
    showStatus("No URL set - hold BOOT to configure");
    return false;
  }
  String body = https_get_string(String(wc_raw_url));
  if (body.isEmpty()) return false;
  wc_body       = body;
  wc_page_start = 0;
  renderPage();
  return true;
}

// Navigate to the next page (or wrap to start at end)
void goNextPage() {
  if (wc_body.isEmpty()) return;
  if (wc_page_next == -1) {
    // At end — wrap to start and clear history
    wc_hist_size  = 0;
    wc_page_start = 0;
  } else {
    // Push current position to history, advance
    if (wc_hist_size < 64) wc_page_hist[wc_hist_size++] = wc_page_start;
    wc_page_start = wc_page_next;
  }
  renderPage();
  showStatus(wc_raw_url);
}

// Navigate to the previous page
void goPrevPage() {
  if (wc_body.isEmpty() || wc_hist_size == 0) return;  // already on first page
  wc_page_start = wc_page_hist[--wc_hist_size];
  renderPage();
  showStatus(wc_raw_url);
}

// Check for touch input and handle left/right tap navigation
void handleTouch() {
  if (ts.tirqTouched() && ts.touched()) {
    if (millis() - lastTouchTime > TOUCH_DEBOUNCE) {
      lastTouchTime = millis();
      TS_Point p = ts.getPoint();
      int x = map(p.x, 200, 3700, 0, gfx->width());
      if (x >= gfx->width() / 2) {
        goNextPage();   // right half = next
      } else {
        goPrevPage();   // left half = prev
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("GithubRaw - GitHub Raw Text Viewer (CYD)");

  if (!gfx->begin()) Serial.println("gfx->begin() failed!");
  gfx->fillScreen(RGB565_BLACK);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  // Init touch screen on VSPI
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  pinMode(0, INPUT_PULLUP);  // BOOT button

  wcLoadSettings();

  bool showPortal = !wc_has_settings;

  if (!showPortal) {
    showStatus("Hold BOOT to change settings...");
    for (int i = 0; i < 30 && !showPortal; i++) {
      if (digitalRead(0) == LOW) showPortal = true;
      delay(100);
    }
  }

  if (showPortal) {
    wcInitPortal();
    while (!portalDone) {
      wcRunPortal();
      delay(5);
    }
    wcClosePortal();
  }

  gfx->fillScreen(RGB565_BLACK);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wc_wifi_ssid, wc_wifi_pass);

  int dots = 0;
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart > 30000) {
      char errMsg[60];
      snprintf(errMsg, sizeof(errMsg), "WiFi failed: \"%s\"", wc_wifi_ssid);
      showStatus(errMsg);
      while (true) delay(1000);
    }
    delay(500);
    char msg[48];
    snprintf(msg, sizeof(msg), "Connecting to WiFi%.*s", (dots % 4) + 1, "....");
    showStatus(msg);
    dots++;
  }
  showStatus("WiFi connected!");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  delay(600);
}

unsigned long last_update = 0;
unsigned long last_clock  = 0;
#define CLOCK_INTERVAL (60UL * 1000UL)

void loop() {
  handleTouch();  // tap right=next, tap left=prev

  // BOOT button: short press = next page, long press (hold) = re-fetch
  if (digitalRead(0) == LOW) {
    delay(50);  // debounce
    if (digitalRead(0) == LOW) {
      unsigned long pressStart = millis();
      while (digitalRead(0) == LOW) delay(10);
      unsigned long held = millis() - pressStart;

      if (held >= BOOT_LONG_MS) {
        // Long press — force re-fetch from page 1
        wc_body       = "";
        wc_page_start = 0;
        wc_hist_size  = 0;
        last_update   = 0;
      } else {
        goNextPage();  // short press = next page
      }
    }
  }

  if ((last_update == 0) || (millis() - last_update > UPDATE_INTERVAL)) {
    showStatus("Fetching...");
    if (fetchAndRender()) {
      wc_hist_size = 0;  // new content resets page history
      showStatus(wc_raw_url);
      last_update = millis();
    } else {
      showStatus("Fetch failed - retrying in 60s");
      last_update = millis() - UPDATE_INTERVAL + 60000;
    }
  }

  if (last_update != 0 && millis() - last_clock > CLOCK_INTERVAL) {
    drawTimestamp();
    last_clock = millis();
  }

  delay(50);
}
