/**
 * @file web_config.cpp
 * @brief 天气城市 Web 配置、WiFi 重新配网
 */
#include "web_config.h"
#include "app_state.h"
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESP.h>

#define PREF_NAMESPACE  "vibe"
#define PREF_KEY_LOC   "wloc"
#define WEATHER_LOCATION_DEFAULT  "kunming"

static WebServer webServer(80);
static Preferences preferences;

static void handleWebRoot(void) {
    if (webServer.method() == HTTP_POST) {
        if (webServer.hasArg("location")) {
            String loc = webServer.arg("location");
            loc.trim();
            if (loc.length() > 0 && loc.length() < sizeof(g_weatherLocation)) {
                loc.toCharArray(g_weatherLocation, sizeof(g_weatherLocation));
                g_weatherLocation[sizeof(g_weatherLocation) - 1] = '\0';
                preferences.begin(PREF_NAMESPACE, false);
                preferences.putString(PREF_KEY_LOC, g_weatherLocation);
                preferences.end();
                g_weatherLastFetch = 0;
            }
        }
        webServer.sendHeader("Location", "/");
        webServer.send(302, "text/plain", "");
        return;
    }
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>OLED 时钟配置</title></head><body style=\"font-family:sans-serif;padding:1em;\">";
    html += "<h2>天气城市设置</h2><p>心知天气城市 ID（如 kunming、beijing、shanghai）</p>";
    html += "<form method=\"post\" action=\"/\">";
    html += "<input type=\"text\" name=\"location\" value=\"" + String(g_weatherLocation) + "\" maxlength=\"31\" size=\"20\"> ";
    html += "<button type=\"submit\">保存</button></form>";
    html += "<p><small>保存后进入设备「天气」页将自动拉取新城市数据。</small></p>";
    html += "<hr><h3>WiFi 配网</h3><p>若更换路由器或需重新配网，点击下方按钮。设备将重启并开放热点 <strong>OLEDClock</strong>，用手机连接后选择新 WiFi 并输入密码。</p>";
    html += "<form method=\"post\" action=\"/resetwifi\" onsubmit=\"return confirm('确定清除当前 WiFi 并重新配网？');\">";
    html += "<button type=\"submit\">清除 WiFi 并重新配网</button></form></body></html>";
    webServer.send(200, "text/html; charset=utf-8", html);
}

static void handleResetWifi(void) {
    if (webServer.method() != HTTP_POST) {
        webServer.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    webServer.send(200, "text/html; charset=utf-8",
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta http-equiv=\"refresh\" content=\"5;url=/\"></head><body><p>已清除 WiFi 配置，设备即将重启。请用手机连接热点 <b>OLEDClock</b> 重新配网。</p><p>5 秒后跳转…</p></body></html>");
    webServer.client().stop();
    delay(200);
    WiFiManager wm;
    wm.resetSettings();
    delay(500);
    ESP.restart();
}

void webConfigBegin(void) {
    preferences.begin(PREF_NAMESPACE, true);
    String saved = preferences.getString(PREF_KEY_LOC, WEATHER_LOCATION_DEFAULT);
    preferences.end();
    if (saved.length() > 0 && saved.length() < sizeof(g_weatherLocation)) {
        saved.toCharArray(g_weatherLocation, sizeof(g_weatherLocation));
        g_weatherLocation[sizeof(g_weatherLocation) - 1] = '\0';
    }
    webServer.on("/", HTTP_GET, handleWebRoot);
    webServer.on("/", HTTP_POST, handleWebRoot);
    webServer.on("/resetwifi", HTTP_POST, handleResetWifi);
    webServer.begin();
}

void webConfigHandleClient(void) {
    webServer.handleClient();
}
