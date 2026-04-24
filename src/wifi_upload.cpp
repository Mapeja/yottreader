#include "wifi_upload.h"
#include "wifi_upload_page.h"
#include "fonts.h"
#include "theme.h"
#include "settings.h"
#include <GxEPD2_BW.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <string.h>

extern GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT> display;

static const char* AP_SSID = "Yottreader";
static const char* AP_PASS = "yottreader";

static WebServer server(80);
static DNSServer dns;
static bool      wifiActive        = false;
static bool      serverReady       = false; // routes registered once; server.stop() doesn't clear them
static File      uploadFile;
static bool      uploadOk          = true;
static String    uploadPath        = "";
static size_t    uploadBytesWritten = 0;
static size_t    uploadMaxBytes    = 0; // bytes available at upload start (with safety margin)

// ── Display ──────────────────────────────────────────────────────────────────

static void drawWifiScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(theme_bg());
    display.setTextColor(theme_fg());

    const int x = 4;
    display.setFont(UI_FONT);
    display.setCursor(x, 13);
    display.print("WiFi Upload");

    display.setFont(UI_FONT_S);
    int y = 13 + UI_FONT->yAdvance + 4;
    display.setCursor(x, y); display.print("Network:  Yottreader");
    y += UI_FONT_S->yAdvance + 1;
    display.setCursor(x, y); display.print("Password: yottreader");
    y += UI_FONT_S->yAdvance + 1;
    display.setCursor(x, y); display.print("Open: 192.168.4.1");
    y += UI_FONT_S->yAdvance + 1;
    display.setCursor(x, y); display.print("or yottreader.local");

    int yBot = display.height() - 3;
    display.setCursor(x, yBot);
    display.print("Press button to exit");
  } while (display.nextPage());
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// f.name() returns with or without leading '/' depending on ESP32 core version.
static String bookPath(const char* rawName) {
  String s = String(rawName);
  while (s.startsWith("/")) s = s.substring(1);
  return "/" + s;
}

// ── Handlers ─────────────────────────────────────────────────────────────────

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send_P(200, "text/html", WIFI_PAGE);
}

static void handleUploadDone() {
  if (uploadOk) {
    server.send(200, "text/plain", "OK");
  } else {
    // Clean up partial file if it's still open
    if (uploadFile) { uploadFile.close(); }
    if (uploadPath.length()) { LittleFS.remove(uploadPath); LittleFS.remove(uploadPath + ".idx"); }
    server.send(500, "text/plain", "Upload failed: storage full or write error");
  }
}

static void handleUpload() {
  HTTPUpload& u = server.upload();

  if (u.status == UPLOAD_FILE_START) {
    uploadOk           = true;
    uploadBytesWritten = 0;

    // Sanitize filename
    String name = u.filename;
    int sep = name.lastIndexOf('/');
    if (sep >= 0) name = name.substring(sep + 1);
    sep = name.lastIndexOf('\\');
    if (sep >= 0) name = name.substring(sep + 1);
    if (!name.endsWith(".book")) name += ".book";
    uploadPath = "/" + name;

    // Calculate available space with a 32 KB safety margin so we never
    // let LittleFS fill completely (the driver crashes with divide-by-zero
    // when the filesystem is 100% full during a write).
    size_t total   = LittleFS.totalBytes();
    size_t used    = LittleFS.usedBytes();
    size_t avail   = (total > used) ? (total - used) : 0;
    uploadMaxBytes = (avail > 32768) ? (avail - 32768) : 0;

    Serial.printf("[upload] start: %s  free: %u KB  max: %u KB\n",
                  uploadPath.c_str(), avail / 1024, uploadMaxBytes / 1024);

    if (uploadMaxBytes == 0) {
      Serial.println("[upload] ERROR: filesystem full before upload");
      uploadOk = false;
      return;
    }

    LittleFS.remove(uploadPath + ".idx"); // remove stale index
    uploadFile = LittleFS.open(uploadPath, "w");
    if (!uploadFile) {
      Serial.printf("[upload] ERROR: cannot open %s for writing\n", uploadPath.c_str());
      uploadOk = false;
    }

  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!uploadOk) return; // drain remaining chunks without writing after an error

    // Check BEFORE writing — never let LittleFS reach 100% full (crashes the driver)
    if (uploadBytesWritten + u.currentSize > uploadMaxBytes) {
      Serial.printf("[upload] ERROR: filesystem full at %u KB written\n",
                    uploadBytesWritten / 1024);
      if (uploadFile) { uploadFile.close(); }
      LittleFS.remove(uploadPath);
      uploadPath = "";
      uploadOk = false;
      return;
    }

    if (uploadFile) {
      size_t written = uploadFile.write(u.buf, u.currentSize);
      if (written != u.currentSize) {
        Serial.printf("[upload] ERROR: wrote %u of %u bytes\n", written, u.currentSize);
        uploadOk = false;
      }
      uploadBytesWritten += u.currentSize;
    }

  } else if (u.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("[upload] done: %u KB written, ok=%d\n",
                    uploadBytesWritten / 1024, uploadOk);
    }
    if (!uploadOk && uploadPath.length()) {
      LittleFS.remove(uploadPath);
      uploadPath = "";
    }
  }
}

static void handleBooks() {
  String json = "[";
  bool first = true;
  File root = LittleFS.open("/");
  if (root) {
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String path = bookPath(f.name());
        if (path.endsWith(".book")) {
          if (!first) json += ",";
          json += "\"" + path + "\"";
          first = false;
        }
      }
      f = root.openNextFile();
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

static void handleSpace() {
  size_t total = LittleFS.totalBytes();
  size_t used  = LittleFS.usedBytes();
  String json  = "{\"total\":" + String(total) + ",\"used\":" + String(used) + "}";
  server.send(200, "application/json", json);
}

static void handleDelete() {
  if (!server.hasArg("name")) { server.send(400, "text/plain", "Missing name"); return; }
  String name = server.arg("name");
  while (name.startsWith("/")) name = name.substring(1);
  String path = "/" + name;
  LittleFS.remove(path);
  LittleFS.remove(path + ".idx");
  server.send(200, "text/plain", "OK");
}

static void handleGetSettings() {
  WebSettings ws = settings_get_web();
  String json = "{\"fontSz\":"  + String(ws.fontSize)
    + ",\"fontFam\":"  + String(ws.fontFamily)
    + ",\"hyphen\":"   + String(ws.hyphenation)
    + ",\"display\":"  + String(ws.displayMode)
    + ",\"orient\":"   + String(ws.orientation)
    + ",\"refresh\":"  + String(ws.refresh)
    + ",\"stats\":"    + String(ws.stats)
    + ",\"sleep\":"    + String(ws.sleep) + "}";
  server.send(200, "application/json", json);
}

static void handlePostSettings() {
  WebSettings ws;
  ws.fontSize    = server.hasArg("fontSz")  ? server.arg("fontSz").toInt()  : 1;
  ws.fontFamily  = server.hasArg("fontFam") ? server.arg("fontFam").toInt() : 0;
  ws.hyphenation = server.hasArg("hyphen")  ? server.arg("hyphen").toInt()  : 0;
  ws.displayMode = server.hasArg("display") ? server.arg("display").toInt() : 0;
  ws.orientation = server.hasArg("orient")  ? server.arg("orient").toInt()  : 0;
  ws.refresh     = server.hasArg("refresh") ? server.arg("refresh").toInt() : 10;
  ws.stats       = server.hasArg("stats")   ? server.arg("stats").toInt()   : 0;
  ws.sleep       = server.hasArg("sleep")   ? server.arg("sleep").toInt()   : 0;
  settings_apply_web(ws);
  server.send(200, "text/plain", "OK");
}

static void handleNotFound() {
  // Only redirect GET requests for captive portal.
  // POST/DELETE must never be silently swallowed by a redirect.
  if (server.method() == HTTP_GET) {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ── Public API ───────────────────────────────────────────────────────────────

void wifi_upload_begin() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  MDNS.begin("yottreader");
  MDNS.addService("http", "tcp", 80);

  if (!serverReady) {
    server.on("/",       HTTP_GET,    handleRoot);
    server.on("/upload", HTTP_POST,   handleUploadDone, handleUpload);
    server.on("/books",  HTTP_GET,    handleBooks);
    server.on("/space",  HTTP_GET,    handleSpace);
    server.on("/delete",   HTTP_DELETE, handleDelete);
    server.on("/settings", HTTP_GET,    handleGetSettings);
    server.on("/settings", HTTP_POST,   handlePostSettings);
    server.onNotFound(handleNotFound);
    serverReady = true;
  }
  server.begin();

  Serial.printf("[wifi] AP started, free heap: %u\n", ESP.getFreeHeap());
  wifiActive = true;
  drawWifiScreen();
}

void wifi_upload_handle() {
  if (!wifiActive) return;
  dns.processNextRequest();
  server.handleClient();
}

void wifi_upload_end() {
  if (!wifiActive) return;
  dns.stop();
  MDNS.end();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiActive = false;
}

bool wifi_upload_active() { return wifiActive; }
