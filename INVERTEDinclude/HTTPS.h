#pragma once

#include <FS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Fetch a URL over HTTPS and return the full response body as a String.
// Returns an empty String on any error.
String https_get_string(const String &url) {
  Serial.printf("[HTTPS] GET %s\n", url.c_str());
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) return "";
  client->setInsecure();
  String body;
  {
    HTTPClient https;
    https.begin(*client, url);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    https.addHeader("User-Agent", "esp32-githubraw (github.com/Coreymillia)");
    https.setTimeout(15000);
    int code = https.GET();
    Serial.printf("[HTTPS] code: %d\n", code);
    if (code == HTTP_CODE_OK) {
      body = https.getString();
    } else {
      Serial.printf("[HTTPS] error: %s\n", https.errorToString(code).c_str());
    }
    https.end();
  }
  delete client;
  return body;
}
