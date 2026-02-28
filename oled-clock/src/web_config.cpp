/**
 * @file web_config.cpp
 * @brief 天气城市 Web 配置：Preferences 持久化，GET 表单 / POST 保存
 */
#include "web_config.h"
#include "app_state.h"
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>

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
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>天气城市</title></head><body style=\"font-family:sans-serif;padding:1em;\">";
    html += "<h2>天气城市设置</h2><p>心知天气城市 ID（如 kunming、beijing、shanghai）</p>";
    html += "<form method=\"post\" action=\"/\">";
    html += "<input type=\"text\" name=\"location\" value=\"" + String(g_weatherLocation) + "\" maxlength=\"31\" size=\"20\"> ";
    html += "<button type=\"submit\">保存</button></form>";
    html += "<p><small>保存后进入设备「天气」页将自动拉取新城市数据。</small></p></body></html>";
    webServer.send(200, "text/html; charset=utf-8", html);
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
    webServer.begin();
}

void webConfigHandleClient(void) {
    webServer.handleClient();
}
