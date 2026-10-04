#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <math.h>

// ============================================================
// DESKBUDDY
// ESP32 + SSD1306 OLED
//
// GPIO0 = BUILT-IN BOOT BUTTON
//
// Press BOOT:
// Playlist 1 -> Playlist 2 -> Playlist 3 -> Playlist 1
//
// Button press shows playlist name for 2 seconds,
// then normal page cycling starts.
// ============================================================


// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID = "CAPCOMLow";
const char* WIFI_PASSWORD = "WowzerMowzer1!";


// ============================================================
// MQTT
// ============================================================

const char* MQTT_HOST = "mowzerserver.local";
const uint16_t MQTT_PORT = 1883;

const char* MQTT_STATUS_TOPIC       = "deskbuddy/status";
const char* MQTT_PLAYLIST_TOPIC     = "deskbuddy/playlist";
const char* MQTT_PLAYLIST_NAME_TOPIC = "deskbuddy/playlist_name";
const char* MQTT_PAGE_TOPIC         = "deskbuddy/page";
const char* MQTT_PAGE_NAME_TOPIC    = "deskbuddy/page_name";
const char* MQTT_IP_TOPIC           = "deskbuddy/ip";
const char* MQTT_WIFI_TOPIC         = "deskbuddy/wifi/rssi";
const char* MQTT_NTP_TOPIC          = "deskbuddy/ntp/status";
const char* MQTT_UPTIME_TOPIC       = "deskbuddy/uptime";
const char* MQTT_WEATHER_STATE      = "deskbuddy/weather/state";
const char* MQTT_WEATHER_TEMP       = "deskbuddy/weather/temperature";


// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);


// ============================================================
// BOOT BUTTON
// ============================================================

#define BOOT_BUTTON_PIN 0

bool lastButtonState = HIGH;
unsigned long lastButtonPress = 0;

const unsigned long BUTTON_DEBOUNCE = 250;


// ============================================================
// NTP
// ============================================================

const char* NTP_SERVER = "192.168.0.152";

const char* UK_TZ =
  "GMT0BST,M3.5.0/1,M10.5.0/2";


// ============================================================
// WEB SERVER
// ============================================================

WebServer server(80);


// ============================================================
// MQTT
// ============================================================

WiFiClient mqttWiFiClient;
PubSubClient mqtt(mqttWiFiClient);


// ============================================================
// PREFERENCES
// ============================================================

Preferences prefs;


// ============================================================
// PAGE SYSTEM
// ============================================================

#define MAX_PAGES 8

#define PAGE_CLOCK   0
#define PAGE_WEATHER 1
#define PAGE_ENTITY  2
#define PAGE_TEXT    3
#define PAGE_NETWORK 4
#define PAGE_MQTT    5
#define PAGE_NTP     6
#define PAGE_UPTIME  7


struct Page {

  uint8_t type;

  String title;

  String data;
};


// ============================================================
// THREE PLAYLISTS
// ============================================================

Page playlist1[MAX_PAGES];
Page playlist2[MAX_PAGES];
Page playlist3[MAX_PAGES];

uint8_t playlist1Count = 0;
uint8_t playlist2Count = 0;
uint8_t playlist3Count = 0;

uint8_t playlist1Index = 0;
uint8_t playlist2Index = 0;
uint8_t playlist3Index = 0;

String playlist1Name = "Playlist 1";
String playlist2Name = "Playlist 2";
String playlist3Name = "Playlist 3";


// ============================================================
// CURRENT PLAYLIST
// ============================================================

uint8_t selectedPlaylist = 1;


// ============================================================
// SETTINGS
// ============================================================

String haUrl =
  "http://mowzerserver.local:8123";

String haToken = "";

String weatherEntity =
  "weather.forecast_home";


// ============================================================
// TIMING
// ============================================================

unsigned long lastPageChange = 0;
unsigned long lastMQTTPublish = 0;
unsigned long lastHAUpdate = 0;
unsigned long lastDisplayUpdate = 0;

const unsigned long PAGE_INTERVAL = 5000;
const unsigned long HA_INTERVAL = 30000;
const unsigned long DISPLAY_INTERVAL = 250;


// ============================================================
// PLAYLIST SPLASH
// ============================================================

bool playlistSplash = false;

unsigned long playlistSplashStarted = 0;

const unsigned long PLAYLIST_SPLASH_TIME = 2000;


// ============================================================
// HA STATE
// ============================================================

String haCurrentEntity = "";
String haCurrentState = "";
String haCurrentFriendlyName = "";

String weatherState = "";

float weatherTemperature = NAN;

String weatherUnit = "°C";


// ============================================================
// HTML ESCAPING
// ============================================================

String htmlEscape(String s) {

  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");

  return s;
}


// ============================================================
// URL DECODE
// ============================================================

String urlDecode(String input) {

  String decoded = "";

  for (
    int i = 0;
    i < input.length();
    i++
  ) {

    if (input[i] == '+') {

      decoded += ' ';
    }

    else if (
      input[i] == '%' &&
      i + 2 < input.length()
    ) {

      char h1 = input[i + 1];
      char h2 = input[i + 2];

      int value = 0;

      if (
        h1 >= '0' &&
        h1 <= '9'
      )
        value +=
          (h1 - '0') * 16;

      else if (
        h1 >= 'A' &&
        h1 <= 'F'
      )
        value +=
          (h1 - 'A' + 10) * 16;

      else if (
        h1 >= 'a' &&
        h1 <= 'f'
      )
        value +=
          (h1 - 'a' + 10) * 16;


      if (
        h2 >= '0' &&
        h2 <= '9'
      )
        value +=
          h2 - '0';

      else if (
        h2 >= 'A' &&
        h2 <= 'F'
      )
        value +=
          h2 - 'A' + 10;

      else if (
        h2 >= 'a' &&
        h2 <= 'f'
      )
        value +=
          h2 - 'a' + 10;


      decoded += char(value);

      i += 2;
    }

    else {

      decoded += input[i];
    }
  }

  return decoded;
}


// ============================================================
// PAGE TYPE NAME
// ============================================================

String pageTypeName(uint8_t type) {

  switch (type) {

    case PAGE_CLOCK:
      return "Clock";

    case PAGE_WEATHER:
      return "Weather";

    case PAGE_ENTITY:
      return "HA Entity";

    case PAGE_TEXT:
      return "Text";

    case PAGE_NETWORK:
      return "Network";

    case PAGE_MQTT:
      return "MQTT";

    case PAGE_NTP:
      return "NTP";

    case PAGE_UPTIME:
      return "Uptime";
  }

  return "Unknown";
}


// ============================================================
// MAKE PAGE
// ============================================================

Page makePage(
  uint8_t type,
  String title,
  String data
) {

  Page p;

  p.type = type;
  p.title = title;
  p.data = data;

  return p;
}


// ============================================================
// DEFAULT PLAYLISTS
// ============================================================

void createDefaults() {

  // ----------------------------------------------------------
  // PLAYLIST 1
  // ----------------------------------------------------------

  playlist1Name = "Playlist 1";

  playlist1Count = 5;

  playlist1[0] =
    makePage(
      PAGE_CLOCK,
      "Clock",
      ""
    );

  playlist1[1] =
    makePage(
      PAGE_WEATHER,
      "Weather",
      ""
    );

  playlist1[2] =
    makePage(
      PAGE_ENTITY,
      "Alarm",
      "alarm_control_panel.zennor_alarmo"
    );

  playlist1[3] =
    makePage(
      PAGE_NETWORK,
      "Network",
      ""
    );

  playlist1[4] =
    makePage(
      PAGE_NTP,
      "NTP",
      ""
    );


  // ----------------------------------------------------------
  // PLAYLIST 2
  // ----------------------------------------------------------

  playlist2Name = "Playlist 2";

  playlist2Count = 5;

  playlist2[0] =
    makePage(
      PAGE_CLOCK,
      "Clock",
      ""
    );

  playlist2[1] =
    makePage(
      PAGE_WEATHER,
      "Weather",
      ""
    );

  playlist2[2] =
    makePage(
      PAGE_ENTITY,
      "Alarm",
      "alarm_control_panel.zennor_alarmo"
    );

  playlist2[3] =
    makePage(
      PAGE_MQTT,
      "MQTT",
      ""
    );

  playlist2[4] =
    makePage(
      PAGE_UPTIME,
      "Uptime",
      ""
    );


  // ----------------------------------------------------------
  // PLAYLIST 3
  // ----------------------------------------------------------

  playlist3Name = "Playlist 3";

  playlist3Count = 4;

  playlist3[0] =
    makePage(
      PAGE_CLOCK,
      "Clock",
      ""
    );

  playlist3[1] =
    makePage(
      PAGE_WEATHER,
      "Weather",
      ""
    );

  playlist3[2] =
    makePage(
      PAGE_NETWORK,
      "Network",
      ""
    );

  playlist3[3] =
    makePage(
      PAGE_UPTIME,
      "Uptime",
      ""
    );


  // ----------------------------------------------------------
  // GLOBAL SETTINGS
  // ----------------------------------------------------------

  haUrl =
    "http://mowzerserver.local:8123";

  haToken = "";

  weatherEntity =
    "weather.forecast_home";
}


// ============================================================
// SAVE CONFIG
// ============================================================

void saveConfig() {

  prefs.begin(
    "deskbuddy",
    false
  );


  prefs.putUChar(
    "p1count",
    playlist1Count
  );

  prefs.putUChar(
    "p2count",
    playlist2Count
  );

  prefs.putUChar(
    "p3count",
    playlist3Count
  );


  prefs.putString(
    "p1name",
    playlist1Name
  );

  prefs.putString(
    "p2name",
    playlist2Name
  );

  prefs.putString(
    "p3name",
    playlist3Name
  );


  prefs.putString(
    "haurl",
    haUrl
  );

  prefs.putString(
    "hatoken",
    haToken
  );

  prefs.putString(
    "weather",
    weatherEntity
  );


  // ----------------------------------------------------------
  // SAVE PAGES
  // ----------------------------------------------------------

  for (
    int i = 0;
    i < MAX_PAGES;
    i++
  ) {

    String prefix1 =
      "p1_" + String(i) + "_";

    String prefix2 =
      "p2_" + String(i) + "_";

    String prefix3 =
      "p3_" + String(i) + "_";


    if (i < playlist1Count) {

      prefs.putUChar(
        (prefix1 + "type").c_str(),
        playlist1[i].type
      );

      prefs.putString(
        (prefix1 + "title").c_str(),
        playlist1[i].title
      );

      prefs.putString(
        (prefix1 + "data").c_str(),
        playlist1[i].data
      );
    }


    if (i < playlist2Count) {

      prefs.putUChar(
        (prefix2 + "type").c_str(),
        playlist2[i].type
      );

      prefs.putString(
        (prefix2 + "title").c_str(),
        playlist2[i].title
      );

      prefs.putString(
        (prefix2 + "data").c_str(),
        playlist2[i].data
      );
    }


    if (i < playlist3Count) {

      prefs.putUChar(
        (prefix3 + "type").c_str(),
        playlist3[i].type
      );

      prefs.putString(
        (prefix3 + "title").c_str(),
        playlist3[i].title
      );

      prefs.putString(
        (prefix3 + "data").c_str(),
        playlist3[i].data
      );
    }
  }


  prefs.end();
}


// ============================================================
// LOAD CONFIG
// ============================================================

void loadConfig() {

  prefs.begin(
    "deskbuddy",
    true
  );


  bool exists =
    prefs.isKey("p1count");


  if (!exists) {

    prefs.end();

    createDefaults();

    saveConfig();

    return;
  }


  playlist1Count =
    prefs.getUChar(
      "p1count",
      5
    );

  playlist2Count =
    prefs.getUChar(
      "p2count",
      5
    );

  playlist3Count =
    prefs.getUChar(
      "p3count",
      4
    );


  if (
    playlist1Count < 1 ||
    playlist1Count > MAX_PAGES
  )
    playlist1Count = 5;


  if (
    playlist2Count < 1 ||
    playlist2Count > MAX_PAGES
  )
    playlist2Count = 5;


  if (
    playlist3Count < 1 ||
    playlist3Count > MAX_PAGES
  )
    playlist3Count = 4;


  playlist1Name =
    prefs.getString(
      "p1name",
      "Playlist 1"
    );

  playlist2Name =
    prefs.getString(
      "p2name",
      "Playlist 2"
    );

  playlist3Name =
    prefs.getString(
      "p3name",
      "Playlist 3"
    );


  haUrl =
    prefs.getString(
      "haurl",
      "http://mowzerserver.local:8123"
    );

  haToken =
    prefs.getString(
      "hatoken",
      ""
    );

  weatherEntity =
    prefs.getString(
      "weather",
      "weather.forecast_home"
    );


  // ----------------------------------------------------------
  // LOAD PAGES
  // ----------------------------------------------------------

  for (
    int i = 0;
    i < MAX_PAGES;
    i++
  ) {

    String prefix1 =
      "p1_" + String(i) + "_";

    String prefix2 =
      "p2_" + String(i) + "_";

    String prefix3 =
      "p3_" + String(i) + "_";


    if (i < playlist1Count) {

      playlist1[i].type =
        prefs.getUChar(
          (prefix1 + "type").c_str(),
          PAGE_CLOCK
        );

      playlist1[i].title =
        prefs.getString(
          (prefix1 + "title").c_str(),
          "Page"
        );

      playlist1[i].data =
        prefs.getString(
          (prefix1 + "data").c_str(),
          ""
        );
    }


    if (i < playlist2Count) {

      playlist2[i].type =
        prefs.getUChar(
          (prefix2 + "type").c_str(),
          PAGE_CLOCK
        );

      playlist2[i].title =
        prefs.getString(
          (prefix2 + "title").c_str(),
          "Page"
        );

      playlist2[i].data =
        prefs.getString(
          (prefix2 + "data").c_str(),
          ""
        );
    }


    if (i < playlist3Count) {

      playlist3[i].type =
        prefs.getUChar(
          (prefix3 + "type").c_str(),
          PAGE_CLOCK
        );

      playlist3[i].title =
        prefs.getString(
          (prefix3 + "title").c_str(),
          "Page"
        );

      playlist3[i].data =
        prefs.getString(
          (prefix3 + "data").c_str(),
          ""
        );
    }
  }


  prefs.end();
}


// ============================================================
// CURRENT PLAYLIST HELPERS
// ============================================================

Page* getCurrentPages() {

  if (selectedPlaylist == 1)
    return playlist1;

  if (selectedPlaylist == 2)
    return playlist2;

  return playlist3;
}


uint8_t getCurrentCount() {

  if (selectedPlaylist == 1)
    return playlist1Count;

  if (selectedPlaylist == 2)
    return playlist2Count;

  return playlist3Count;
}


uint8_t getCurrentIndex() {

  if (selectedPlaylist == 1)
    return playlist1Index;

  if (selectedPlaylist == 2)
    return playlist2Index;

  return playlist3Index;
}


String getPlaylistName(
  uint8_t number
) {

  if (number == 1)
    return playlist1Name;

  if (number == 2)
    return playlist2Name;

  return playlist3Name;
}


Page getCurrentPage() {

  Page* pages =
    getCurrentPages();

  uint8_t count =
    getCurrentCount();

  if (count == 0) {

    return makePage(
      PAGE_TEXT,
      "Empty",
      "No pages"
    );
  }

  uint8_t index =
    getCurrentIndex();

  if (index >= count)
    index = 0;

  return pages[index];
}


// ============================================================
// MQTT PUBLISH
// ============================================================

void mqttPublish(
  const char* topic,
  String payload,
  bool retained = true
) {

  if (!mqtt.connected())
    return;

  mqtt.publish(
    topic,
    payload.c_str(),
    retained
  );
}


// ============================================================
// MQTT CALLBACK
// ============================================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
) {
  // No playlist commands.
  // The physical onboard BOOT button is authoritative.
}


// ============================================================
// MQTT DISCOVERY
// ============================================================

void mqttDiscovery() {

  if (!mqtt.connected())
    return;


  String base;


  // ----------------------------------------------------------
  // ONLINE
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy Online\","
    "\"unique_id\":\"deskbuddy_online\","
    "\"state_topic\":\"deskbuddy/status\","
    "\"payload_on\":\"online\","
    "\"payload_off\":\"offline\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"],"
      "\"name\":\"DeskBuddy\","
      "\"manufacturer\":\"Zennor\","
      "\"model\":\"ESP32 DeskBuddy\""
    "}"
    "}";


  mqtt.publish(
    "homeassistant/binary_sensor/deskbuddy/online/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // PLAYLIST
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy Playlist\","
    "\"unique_id\":\"deskbuddy_playlist\","
    "\"state_topic\":\"deskbuddy/playlist\","
    "\"icon\":\"mdi:playlist-play\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/playlist/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // PLAYLIST NAME
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy Playlist Name\","
    "\"unique_id\":\"deskbuddy_playlist_name\","
    "\"state_topic\":\"deskbuddy/playlist_name\","
    "\"icon\":\"mdi:playlist-music\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/playlist_name/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // PAGE
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy Page\","
    "\"unique_id\":\"deskbuddy_page\","
    "\"state_topic\":\"deskbuddy/page\","
    "\"icon\":\"mdi:file-document-outline\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/page/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // PAGE NAME
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy Page Name\","
    "\"unique_id\":\"deskbuddy_page_name\","
    "\"state_topic\":\"deskbuddy/page_name\","
    "\"icon\":\"mdi:card-text-outline\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/page_name/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // IP
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy IP\","
    "\"unique_id\":\"deskbuddy_ip\","
    "\"state_topic\":\"deskbuddy/ip\","
    "\"icon\":\"mdi:ip\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/ip/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy WiFi\","
    "\"unique_id\":\"deskbuddy_wifi\","
    "\"state_topic\":\"deskbuddy/wifi/rssi\","
    "\"unit_of_measurement\":\"dBm\","
    "\"device_class\":\"signal_strength\","
    "\"icon\":\"mdi:wifi\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/wifi/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // NTP
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy NTP\","
    "\"unique_id\":\"deskbuddy_ntp\","
    "\"state_topic\":\"deskbuddy/ntp/status\","
    "\"icon\":\"mdi:clock-sync-outline\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/ntp/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // UPTIME
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy Uptime\","
    "\"unique_id\":\"deskbuddy_uptime\","
    "\"state_topic\":\"deskbuddy/uptime\","
    "\"icon\":\"mdi:timer-outline\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/uptime/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // WEATHER
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy Weather\","
    "\"unique_id\":\"deskbuddy_weather\","
    "\"state_topic\":\"deskbuddy/weather/state\","
    "\"icon\":\"mdi:weather-partly-cloudy\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/weather/config",
    base.c_str(),
    true
  );


  // ----------------------------------------------------------
  // TEMPERATURE
  // ----------------------------------------------------------

  base =
    "{"
    "\"name\":\"DeskBuddy Temperature\","
    "\"unique_id\":\"deskbuddy_weather_temperature\","
    "\"state_topic\":\"deskbuddy/weather/temperature\","
    "\"device_class\":\"temperature\","
    "\"unit_of_measurement\":\"°C\","
    "\"icon\":\"mdi:thermometer\","
    "\"device\":{"
      "\"identifiers\":[\"deskbuddy\"]"
    "}"
    "}";


  mqtt.publish(
    "homeassistant/sensor/deskbuddy/weather_temperature/config",
    base.c_str(),
    true
  );
}


// ============================================================
// CONNECT MQTT
// ============================================================

void connectMQTT() {

  if (mqtt.connected())
    return;


  String clientId =
    "deskbuddy-" +
    String(
      (uint32_t)ESP.getEfuseMac(),
      HEX
    );


  mqtt.setServer(
    MQTT_HOST,
    MQTT_PORT
  );

  mqtt.setCallback(
    mqttCallback
  );


  if (
    mqtt.connect(
      clientId.c_str(),
      MQTT_STATUS_TOPIC,
      0,
      true,
      "offline"
    )
  ) {

    mqtt.publish(
      MQTT_STATUS_TOPIC,
      "online",
      true
    );


    mqttDiscovery();

    mqttPublish(
      MQTT_IP_TOPIC,
      WiFi.localIP().toString()
    );
  }
}


// ============================================================
// MQTT STATE
// ============================================================

void publishMQTTState() {

  if (!mqtt.connected())
    return;


  Page p =
    getCurrentPage();


  mqttPublish(
    MQTT_PLAYLIST_TOPIC,
    String(selectedPlaylist)
  );


  mqttPublish(
    MQTT_PLAYLIST_NAME_TOPIC,
    getPlaylistName(selectedPlaylist)
  );


  mqttPublish(
    MQTT_PAGE_TOPIC,
    String(getCurrentIndex() + 1)
  );


  mqttPublish(
    MQTT_PAGE_NAME_TOPIC,
    p.title
  );


  mqttPublish(
    MQTT_IP_TOPIC,
    WiFi.localIP().toString()
  );


  mqttPublish(
    MQTT_WIFI_TOPIC,
    String(WiFi.RSSI())
  );


  mqttPublish(
    MQTT_NTP_TOPIC,
    timeIsValid()
      ? "synced"
      : "not synced"
  );


  mqttPublish(
    MQTT_UPTIME_TOPIC,
    uptimeString()
  );


  if (
    weatherState.length() > 0
  ) {

    mqttPublish(
      MQTT_WEATHER_STATE,
      weatherState
    );


    if (
      !isnan(weatherTemperature)
    ) {

      mqttPublish(
        MQTT_WEATHER_TEMP,
        String(
          weatherTemperature,
          1
        )
      );
    }
  }
}


// ============================================================
// TIME
// ============================================================

bool timeIsValid() {

  time_t now =
    time(nullptr);

  return now > 1700000000;
}


String uptimeString() {

  unsigned long seconds =
    millis() / 1000;

  unsigned long days =
    seconds / 86400;

  seconds %= 86400;

  unsigned long hours =
    seconds / 3600;

  seconds %= 3600;

  unsigned long minutes =
    seconds / 60;

  seconds %= 60;


  char buffer[40];

  snprintf(
    buffer,
    sizeof(buffer),
    "%lud %02lu:%02lu:%02lu",
    days,
    hours,
    minutes,
    seconds
  );


  return String(buffer);
}


// ============================================================
// JSON STRING HELPER
// ============================================================

String jsonString(
  String json,
  String key
) {

  String search =
    "\"" + key + "\"";


  int pos =
    json.indexOf(search);


  if (pos < 0)
    return "";


  pos =
    json.indexOf(
      ':',
      pos + search.length()
    );


  if (pos < 0)
    return "";


  pos++;


  while (
    pos < json.length() &&
    (
      json[pos] == ' ' ||
      json[pos] == '\n' ||
      json[pos] == '\r'
    )
  ) {

    pos++;
  }


  if (
    pos >= json.length()
  )
    return "";


  if (
    json[pos] != '"'
  )
    return "";


  pos++;


  String result = "";


  while (
    pos < json.length()
  ) {

    if (
      json[pos] == '"' &&
      (
        pos == 0 ||
        json[pos - 1] != '\\'
      )
    ) {

      break;
    }


    result +=
      json[pos];

    pos++;
  }


  result.replace(
    "\\\"",
    "\""
  );


  return result;
}


// ============================================================
// HA ENTITY FETCH
// ============================================================

bool fetchHAEntity(
  String entityId,
  String &state,
  String &friendlyName
) {

  if (
    haUrl.length() == 0 ||
    haToken.length() == 0 ||
    entityId.length() == 0
  ) {

    return false;
  }


  HTTPClient http;


  String url =
    haUrl;


  if (
    url.endsWith("/")
  ) {

    url.remove(
      url.length() - 1
    );
  }


  url +=
    "/api/states/" +
    entityId;


  http.setTimeout(5000);

  http.begin(url);


  http.addHeader(
    "Authorization",
    "Bearer " + haToken
  );


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  int code =
    http.GET();


  if (
    code != 200
  ) {

    http.end();

    return false;
  }


  String payload =
    http.getString();


  http.end();


  state =
    jsonString(
      payload,
      "state"
    );


  friendlyName =
    jsonString(
      payload,
      "friendly_name"
    );


  return state.length() > 0;
}


// ============================================================
// WEATHER FETCH
// ============================================================

bool fetchWeather() {

  if (
    weatherEntity.length() == 0 ||
    haUrl.length() == 0 ||
    haToken.length() == 0
  ) {

    return false;
  }


  HTTPClient http;


  String url =
    haUrl;


  if (
    url.endsWith("/")
  ) {

    url.remove(
      url.length() - 1
    );
  }


  url +=
    "/api/states/" +
    weatherEntity;


  http.setTimeout(5000);

  http.begin(url);


  http.addHeader(
    "Authorization",
    "Bearer " + haToken
  );


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  int code =
    http.GET();


  if (
    code != 200
  ) {

    http.end();

    return false;
  }


  String payload =
    http.getString();


  http.end();


  String state =
    jsonString(
      payload,
      "state"
    );


  if (
    state.length() == 0
  )
    return false;


  weatherState =
    state;


  // ----------------------------------------------------------
  // TEMPERATURE
  // ----------------------------------------------------------

  int tempPos =
    payload.indexOf(
      "\"temperature\""
    );


  if (
    tempPos >= 0
  ) {

    int colon =
      payload.indexOf(
        ':',
        tempPos
      );


    if (
      colon >= 0
    ) {

      int start =
        colon + 1;


      while (
        start < payload.length() &&
        (
          payload[start] == ' ' ||
          payload[start] == '\n' ||
          payload[start] == '\r'
        )
      ) {

        start++;
      }


      int end =
        start;


      while (
        end < payload.length() &&
        (
          (
            payload[end] >= '0' &&
            payload[end] <= '9'
          ) ||
          payload[end] == '.' ||
          payload[end] == '-'
        )
      ) {

        end++;
      }


      String number =
        payload.substring(
          start,
          end
        );


      weatherTemperature =
        number.toFloat();
    }
  }


  String unit =
    jsonString(
      payload,
      "temperature_unit"
    );


  if (
    unit.length() > 0
  )
    weatherUnit = unit;


  return true;
}


// ============================================================
// UPDATE HA DATA
// ============================================================

void updateHAData() {

  if (
    millis() - lastHAUpdate <
    HA_INTERVAL
  ) {

    return;
  }


  lastHAUpdate =
    millis();


  Page p =
    getCurrentPage();


  // ----------------------------------------------------------
  // WEATHER
  // ----------------------------------------------------------

  if (
    p.type == PAGE_WEATHER
  ) {

    String entity =
      p.data;


    if (
      entity.length() == 0
    )
      entity =
        weatherEntity;


    if (
      entity.length() > 0
    ) {

      fetchWeather();
    }
  }


  // ----------------------------------------------------------
  // ENTITY
  // ----------------------------------------------------------

  else if (
    p.type == PAGE_ENTITY
  ) {

    String state;
    String name;


    if (
      fetchHAEntity(
        p.data,
        state,
        name
      )
    ) {

      haCurrentEntity =
        p.data;

      haCurrentState =
        state;

      haCurrentFriendlyName =
        name;
    }
  }
}


// ============================================================
// FORCE HA UPDATE NOW
// ============================================================

void forceHAUpdate() {

  lastHAUpdate =
    millis() - HA_INTERVAL - 1;

  updateHAData();
}


// ============================================================
// OLED TITLE
// ============================================================

void oledTitle(
  String title
) {

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(1);

  display.setCursor(
    0,
    0
  );

  display.print(
    title.substring(
      0,
      20
    )
  );


  display.drawLine(
    0,
    10,
    127,
    10,
    SSD1306_WHITE
  );
}


// ============================================================
// PLAYLIST SPLASH
// ============================================================

void drawPlaylistSplash() {

  display.clearDisplay();


  display.setTextColor(
    SSD1306_WHITE
  );


  display.setTextSize(1);

  display.setCursor(
    25,
    8
  );

  display.print(
    "PLAYLIST"
  );


  display.setTextSize(3);

  display.setCursor(
    50,
    22
  );

  display.print(
    selectedPlaylist
  );


  display.setTextSize(1);

  String name =
    getPlaylistName(
      selectedPlaylist
    );


  display.setCursor(
    4,
    51
  );

  display.print(
    name.substring(
      0,
      20
    )
  );


  display.display();
}


// ============================================================
// SUN
// ============================================================

void drawSun(
  int x,
  int y
) {

  display.drawCircle(
    x,
    y,
    6,
    SSD1306_WHITE
  );


  for (
    int i = 0;
    i < 8;
    i++
  ) {

    float a =
      i * 3.1415926 / 4.0;


    int x1 =
      x + cos(a) * 9;


    int y1 =
      y + sin(a) * 9;


    int x2 =
      x + cos(a) * 12;


    int y2 =
      y + sin(a) * 12;


    display.drawLine(
      x1,
      y1,
      x2,
      y2,
      SSD1306_WHITE
    );
  }
}


// ============================================================
// CLOUD
// ============================================================

void drawCloud(
  int x,
  int y
) {

  display.fillCircle(
    x - 8,
    y,
    6,
    SSD1306_WHITE
  );


  display.fillCircle(
    x,
    y - 4,
    8,
    SSD1306_WHITE
  );


  display.fillCircle(
    x + 9,
    y,
    6,
    SSD1306_WHITE
  );


  display.fillRect(
    x - 14,
    y,
    28,
    7,
    SSD1306_WHITE
  );
}


// ============================================================
// RAIN
// ============================================================

void drawRain(
  int x,
  int y
) {

  drawCloud(
    x,
    y - 5
  );


  for (
    int i = -1;
    i <= 1;
    i++
  ) {

    display.drawLine(
      x + i * 7,
      y + 5,
      x + i * 7 - 2,
      y + 11,
      SSD1306_WHITE
    );
  }
}


// ============================================================
// SNOW
// ============================================================

void drawSnow(
  int x,
  int y
) {

  drawCloud(
    x,
    y - 5
  );


  for (
    int i = -1;
    i <= 1;
    i++
  ) {

    int sx =
      x + i * 8;


    int sy =
      y + 9;


    display.drawPixel(
      sx,
      sy,
      SSD1306_WHITE
    );

    display.drawPixel(
      sx - 1,
      sy,
      SSD1306_WHITE
    );

    display.drawPixel(
      sx + 1,
      sy,
      SSD1306_WHITE
    );

    display.drawPixel(
      sx,
      sy - 1,
      SSD1306_WHITE
    );

    display.drawPixel(
      sx,
      sy + 1,
      SSD1306_WHITE
    );
  }
}


// ============================================================
// WEATHER ICON
// ============================================================

void drawWeatherIcon(
  String state,
  int x,
  int y
) {

  state.toLowerCase();


  if (
    state == "sunny" ||
    state == "clear"
  ) {

    drawSun(
      x,
      y
    );

    return;
  }


  if (
    state == "partlycloudy" ||
    state == "cloudy"
  ) {

    drawCloud(
      x,
      y
    );

    return;
  }


  if (
    state.indexOf("rain") >= 0 ||
    state == "pouring"
  ) {

    drawRain(
      x,
      y
    );

    return;
  }


  if (
    state.indexOf("snow") >= 0
  ) {

    drawSnow(
      x,
      y
    );

    return;
  }


  if (
    state == "fog"
  ) {

    display.drawLine(
      x - 12,
      y - 4,
      x + 12,
      y - 4,
      SSD1306_WHITE
    );


    display.drawLine(
      x - 12,
      y + 2,
      x + 12,
      y + 2,
      SSD1306_WHITE
    );


    display.drawLine(
      x - 8,
      y + 8,
      x + 8,
      y + 8,
      SSD1306_WHITE
    );


    return;
  }


  drawCloud(
    x,
    y
  );
}


// ============================================================
// CLOCK
// ============================================================

void drawClockPage(
  Page &p
) {

  oledTitle(
    p.title
  );


  struct tm timeinfo;


  if (
    !getLocalTime(
      &timeinfo,
      50
    )
  ) {

    display.setTextSize(2);

    display.setCursor(
      12,
      25
    );

    display.print(
      "NO NTP"
    );

    return;
  }


  char timeBuffer[10];


  strftime(
    timeBuffer,
    sizeof(timeBuffer),
    "%H:%M",
    &timeinfo
  );


  char dateBuffer[20];


  strftime(
    dateBuffer,
    sizeof(dateBuffer),
    "%a %d %b",
    &timeinfo
  );


  display.setTextSize(3);

  display.setCursor(
    9,
    18
  );

  display.print(
    timeBuffer
  );


  display.setTextSize(1);

  display.setCursor(
    36,
    52
  );

  display.print(
    dateBuffer
  );
}


// ============================================================
// WEATHER
// ============================================================

void drawWeatherPage(
  Page &p
) {

  oledTitle(
    p.title
  );


  if (
    weatherEntity.length() == 0 &&
    p.data.length() == 0
  ) {

    display.setCursor(
      4,
      22
    );

    display.print(
      "Set weather entity"
    );


    display.setCursor(
      4,
      34
    );

    display.print(
      "in web UI"
    );


    return;
  }


  if (
    weatherState.length() == 0
  ) {

    display.setCursor(
      4,
      24
    );

    display.print(
      "Weather unavailable"
    );


    return;
  }


  drawWeatherIcon(
    weatherState,
    25,
    33
  );


  display.setTextSize(2);

  display.setCursor(
    54,
    22
  );


  if (
    !isnan(weatherTemperature)
  ) {

    display.print(
      weatherTemperature,
      1
    );

    display.print(
      weatherUnit
    );
  }

  else {

    display.print(
      "--"
    );
  }


  display.setTextSize(1);

  display.setCursor(
    50,
    46
  );


  String state =
    weatherState;


  state.replace(
    "partlycloudy",
    "Partly Cloudy"
  );

  state.replace(
    "clear-night",
    "Clear Night"
  );

  state.replace(
    "sunny",
    "Sunny"
  );

  state.replace(
    "cloudy",
    "Cloudy"
  );

  state.replace(
    "rainy",
    "Rain"
  );

  state.replace(
    "pouring",
    "Pouring"
  );

  state.replace(
    "snowy",
    "Snow"
  );

  state.replace(
    "snowy-rainy",
    "Snow + Rain"
  );


  display.print(
    state.substring(
      0,
      18
    )
  );
}


// ============================================================
// ENTITY
// ============================================================

void drawEntityPage(
  Page &p
) {

  oledTitle(
    p.title
  );


  if (
    p.data.length() == 0
  ) {

    display.setCursor(
      4,
      24
    );

    display.print(
      "No entity ID"
    );


    return;
  }


  if (
    haCurrentEntity != p.data
  ) {

    display.setCursor(
      4,
      24
    );

    display.print(
      "Loading..."
    );


    return;
  }


  display.setTextSize(1);

  display.setCursor(
    4,
    20
  );


  String name =
    haCurrentFriendlyName;


  if (
    name.length() == 0
  )
    name =
      p.data;


  display.print(
    name.substring(
      0,
      20
    )
  );


  display.setTextSize(2);

  display.setCursor(
    4,
    35
  );


  display.print(
    haCurrentState.substring(
      0,
      10
    )
  );
}


// ============================================================
// TEXT
// ============================================================

void drawTextPage(
  Page &p
) {

  oledTitle(
    p.title
  );


  display.setTextSize(1);

  int y = 18;

  String text =
    p.data;


  while (
    text.length() > 0 &&
    y < 62
  ) {

    int split =
      text.indexOf(
        '\n'
      );


    if (
      split < 0
    ) {

      display.setCursor(
        2,
        y
      );


      display.print(
        text.substring(
          0,
          21
        )
      );


      break;
    }


    String line =
      text.substring(
        0,
        split
      );


    display.setCursor(
      2,
      y
    );


    display.print(
      line.substring(
        0,
        21
      )
    );


    text =
      text.substring(
        split + 1
      );


    y += 10;
  }
}


// ============================================================
// NETWORK
// ============================================================

void drawNetworkPage(
  Page &p
) {

  oledTitle(
    p.title
  );


  display.setTextSize(1);


  display.setCursor(
    2,
    18
  );

  display.print(
    "IP: "
  );

  display.print(
    WiFi.localIP()
  );


  display.setCursor(
    2,
    31
  );

  display.print(
    "WiFi: "
  );

  display.print(
    WiFi.RSSI()
  );

  display.print(
    " dBm"
  );


  display.setCursor(
    2,
    44
  );

  display.print(
    "SSID: "
  );

  display.print(
    WIFI_SSID
  );


  display.setCursor(
    2,
    56
  );

  display.print(
    WiFi.status() == WL_CONNECTED
      ? "Connected"
      : "Disconnected"
  );
}


// ============================================================
// MQTT
// ============================================================

void drawMQTTPage(
  Page &p
) {

  oledTitle(
    p.title
  );


  display.setTextSize(1);


  display.setCursor(
    2,
    22
  );

  display.print(
    "Broker:"
  );


  display.setCursor(
    2,
    34
  );

  display.print(
    MQTT_HOST
  );


  display.setCursor(
    2,
    50
  );

  display.print(
    mqtt.connected()
      ? "MQTT: Connected"
      : "MQTT: Offline"
  );
}


// ============================================================
// NTP
// ============================================================

void drawNTPPage(
  Page &p
) {

  oledTitle(
    p.title
  );


  display.setTextSize(1);


  display.setCursor(
    2,
    20
  );

  display.print(
    "Server:"
  );


  display.setCursor(
    2,
    31
  );

  display.print(
    NTP_SERVER
  );


  display.setCursor(
    2,
    45
  );

  display.print(
    timeIsValid()
      ? "NTP: Synced"
      : "NTP: Waiting"
  );


  display.setCursor(
    2,
    56
  );

  display.print(
    "UK timezone"
  );
}


// ============================================================
// UPTIME
// ============================================================

void drawUptimePage(
  Page &p
) {

  oledTitle(
    p.title
  );


  display.setTextSize(1);


  display.setCursor(
    2,
    22
  );

  display.print(
    "Uptime:"
  );


  display.setCursor(
    2,
    37
  );

  display.print(
    uptimeString()
  );


  display.setCursor(
    2,
    52
  );

  display.print(
    "Heap: "
  );

  display.print(
    ESP.getFreeHeap()
  );
}


// ============================================================
// CURRENT PAGE
// ============================================================

void drawCurrentPage() {

  if (
    playlistSplash
  ) {

    drawPlaylistSplash();

    return;
  }


  display.clearDisplay();


  Page p =
    getCurrentPage();


  switch (
    p.type
  ) {

    case PAGE_CLOCK:
      drawClockPage(p);
      break;

    case PAGE_WEATHER:
      drawWeatherPage(p);
      break;

    case PAGE_ENTITY:
      drawEntityPage(p);
      break;

    case PAGE_TEXT:
      drawTextPage(p);
      break;

    case PAGE_NETWORK:
      drawNetworkPage(p);
      break;

    case PAGE_MQTT:
      drawMQTTPage(p);
      break;

    case PAGE_NTP:
      drawNTPPage(p);
      break;

    case PAGE_UPTIME:
      drawUptimePage(p);
      break;


    default:

      oledTitle(
        "Unknown Page"
      );

      break;
  }


  display.display();
}


// ============================================================
// SELECT PLAYLIST
// ============================================================

void selectPlaylist(
  uint8_t playlist
) {

  if (
    playlist < 1 ||
    playlist > 3
  )
    playlist = 1;


  selectedPlaylist =
    playlist;


  // Reset page to first page
  if (
    selectedPlaylist == 1
  )
    playlist1Index = 0;

  else if (
    selectedPlaylist == 2
  )
    playlist2Index = 0;

  else
    playlist3Index = 0;


  // Clear cached HA entity so new page gets fetched
  haCurrentEntity = "";
  haCurrentState = "";
  haCurrentFriendlyName = "";

  weatherState = "";
  weatherTemperature = NAN;


  // Show playlist splash
  playlistSplash = true;

  playlistSplashStarted =
    millis();


  lastPageChange =
    millis();


  forceHAUpdate();


  publishMQTTState();


  drawCurrentPage();
}


// ============================================================
// NEXT PLAYLIST
// ============================================================

void nextPlaylist() {

  uint8_t next =
    selectedPlaylist + 1;


  if (
    next > 3
  )
    next = 1;


  selectPlaylist(
    next
  );
}


// ============================================================
// BOOT BUTTON
// ============================================================

void handleBootButton() {

  bool currentState =
    digitalRead(
      BOOT_BUTTON_PIN
    );


  // HIGH -> LOW = button press

  if (
    lastButtonState == HIGH &&
    currentState == LOW
  ) {

    if (
      millis() - lastButtonPress >
      BUTTON_DEBOUNCE
    ) {

      lastButtonPress =
        millis();


      nextPlaylist();
    }
  }


  lastButtonState =
    currentState;
}


// ============================================================
// WEB HEADER
// ============================================================

String webHeader() {

  String html;


  html +=
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>DeskBuddy</title>"
    "<style>"


    "body{"
      "font-family:Arial,sans-serif;"
      "background:#07101c;"
      "color:#eaf2ff;"
      "margin:0;"
      "padding:20px;"
    "}"


    "h1{margin-top:0}"


    ".box{"
      "background:#101d2b;"
      "border:1px solid #26384d;"
      "border-radius:12px;"
      "padding:16px;"
      "margin-bottom:18px;"
    "}"


    ".page{"
      "background:#0b1724;"
      "border:1px solid #26384d;"
      "border-radius:10px;"
      "padding:12px;"
      "margin:10px 0;"
    "}"


    "input,select,textarea{"
      "box-sizing:border-box;"
      "width:100%;"
      "background:#07101c;"
      "color:#fff;"
      "border:1px solid #40566e;"
      "border-radius:7px;"
      "padding:9px;"
      "margin:5px 0 10px;"
    "}"


    "button,.button{"
      "display:inline-block;"
      "background:#087dff;"
      "color:white;"
      "border:0;"
      "border-radius:7px;"
      "padding:9px 12px;"
      "margin:3px;"
      "text-decoration:none;"
      "cursor:pointer;"
    "}"


    ".danger{"
      "background:#b52b35;"
    "}"


    ".secondary{"
      "background:#34475a;"
    "}"


    ".grid{"
      "display:grid;"
      "grid-template-columns:1fr 1fr;"
      "gap:18px;"
    "}"


    "@media(max-width:800px){"
      ".grid{grid-template-columns:1fr;}"
    "}"


    ".small{"
      "color:#9fb1c5;"
      "font-size:13px;"
    "}"


    ".status{"
      "padding:10px;"
      "background:#081521;"
      "border-radius:8px;"
      "margin-bottom:10px;"
    "}"


    "</style>"
    "</head>"
    "<body>";


  return html;
}


// ============================================================
// WEB FOOTER
// ============================================================

String webFooter() {

  return
    "</body>"
    "</html>";
}


// ============================================================
// PLAYLIST HTML
// ============================================================

String playlistHTML(
  int number
) {

  Page* pages;

  uint8_t count;

  String name;


  if (
    number == 1
  ) {

    pages =
      playlist1;

    count =
      playlist1Count;

    name =
      playlist1Name;
  }

  else if (
    number == 2
  ) {

    pages =
      playlist2;

    count =
      playlist2Count;

    name =
      playlist2Name;
  }

  else {

    pages =
      playlist3;

    count =
      playlist3Count;

    name =
      playlist3Name;
  }


  String html;


  html +=
    "<div class='box'>";


  html +=
    "<h2>Playlist " +
    String(number) +
    "</h2>";


  html +=
    "<label>Playlist Name</label>";


  html +=
    "<input name='p" +
    String(number) +
    "_name' value='" +
    htmlEscape(name) +
    "'>";


  html +=
    "<div class='small'>";


  if (
    number == selectedPlaylist
  ) {

    html +=
      "Currently selected.";
  }

  else {

    html +=
      "Press the onboard BOOT button to select this playlist.";
  }


  html +=
    "</div>";


  for (
    int i = 0;
    i < count;
    i++
  ) {

    Page &p =
      pages[i];


    html +=
      "<div class='page'>";


    html +=
      "<b>Page " +
      String(i + 1) +
      "</b>";


    // TYPE

    html +=
      "<label>Type</label>";


    html +=
      "<select name='p" +
      String(number) +
      "_type_" +
      String(i) +
      "'>";


    for (
      int t = 0;
      t <= PAGE_UPTIME;
      t++
    ) {

      html +=
        "<option value='" +
        String(t) +
        "'";


      if (
        p.type == t
      )
        html +=
          " selected";


      html +=
        ">" +
        pageTypeName(t) +
        "</option>";
    }


    html +=
      "</select>";


    // TITLE

    html +=
      "<label>Title</label>";


    html +=
      "<input name='p" +
      String(number) +
      "_title_" +
      String(i) +
      "' value='" +
      htmlEscape(p.title) +
      "'>";


    // DATA

    html +=
      "<label>Data / Entity ID / Text</label>";


    html +=
      "<textarea name='p" +
      String(number) +
      "_data_" +
      String(i) +
      "' rows='2'>" +
      htmlEscape(p.data) +
      "</textarea>";


    // BUTTONS

    html +=
      "<div>";


    if (
      i > 0
    ) {

      html +=
        "<a class='button secondary' href='/move?p=" +
        String(number) +
        "&i=" +
        String(i) +
        "&d=up'>Up</a>";
    }


    if (
      i < count - 1
    ) {

      html +=
        "<a class='button secondary' href='/move?p=" +
        String(number) +
        "&i=" +
        String(i) +
        "&d=down'>Down</a>";
    }


    html +=
      "<a class='button danger' href='/remove?p=" +
      String(number) +
      "&i=" +
      String(i) +
      "' "
      "onclick=\"return confirm('Remove this page?')\">"
      "Remove"
      "</a>";


    html +=
      "</div>";


    // HINT

    html +=
      "<div class='small'>";


    switch (
      p.type
    ) {

      case PAGE_CLOCK:

        html +=
          "No data required.";

        break;


      case PAGE_WEATHER:

        html +=
          "Leave Data blank to use the global weather entity, "
          "or enter a different weather entity ID.";

        break;


      case PAGE_ENTITY:

        html +=
          "Data should be a Home Assistant entity ID.";

        break;


      case PAGE_TEXT:

        html +=
          "Data is the text displayed on the OLED.";

        break;


      default:

        html +=
          "No data required.";

        break;
    }


    html +=
      "</div>";


    html +=
      "</div>";
  }


  // ADD PAGE

  if (
    count < MAX_PAGES
  ) {

    html +=
      "<a class='button' href='/add?p=" +
      String(number) +
      "'>"
      "+ Add Page"
      "</a>";
  }


  html +=
    "</div>";


  return html;
}


// ============================================================
// WEB ROOT
// ============================================================

void handleRoot() {

  String html =
    webHeader();


  html +=
    "<h1>DeskBuddy</h1>";


  html +=
    "<div class='status'>";


  html +=
    "<b>Current playlist:</b> " +
    String(selectedPlaylist) +
    " — " +
    htmlEscape(
      getPlaylistName(
        selectedPlaylist
      )
    );


  html +=
    "<br>";


  html +=
    "<b>Current page:</b> " +
    String(getCurrentIndex() + 1) +
    " / " +
    String(getCurrentCount());


  html +=
    "<br>";


  html +=
    "<b>WiFi:</b> " +
    WiFi.localIP().toString();


  html +=
    "<br>";


  html +=
    "<b>MQTT:</b> " +
    String(
      mqtt.connected()
        ? "Connected"
        : "Offline"
    );


  html +=
    "</div>";


  html +=
    "<form method='POST' action='/save'>";


  html +=
    "<div class='grid'>";


  html +=
    playlistHTML(1);


  html +=
    playlistHTML(2);


  html +=
    playlistHTML(3);


  html +=
    "</div>";


  // HA SETTINGS

  html +=
    "<div class='box'>";


  html +=
    "<h2>Home Assistant</h2>";


  html +=
    "<label>HA URL</label>";


  html +=
    "<input name='haurl' value='" +
    htmlEscape(haUrl) +
    "'>";


  html +=
    "<label>Long-Lived Access Token</label>";


  html +=
    "<input type='password' name='hatoken' value='" +
    htmlEscape(haToken) +
    "'>";


  html +=
    "<label>Weather Entity ID</label>";


  html +=
    "<input name='weather' value='" +
    htmlEscape(weatherEntity) +
    "'>";


  html +=
    "<div class='small'>"
    "Default: weather.forecast_home"
    "</div>";


  html +=
    "</div>";


  html +=
    "<button type='submit'>Save Everything</button>";


  html +=
    "</form>";


  // RESET

  html +=
    "<div class='box'>";


  html +=
    "<h2>Reset</h2>";


  html +=
    "<p class='small'>"
    "Restore the default DeskBuddy configuration. "
    "This also selects Playlist 1."
    "</p>";


  html +=
    "<a class='button danger' "
    "href='/reset' "
    "onclick=\"return confirm('Reset DeskBuddy to defaults?')\">"
    "Reset to Defaults"
    "</a>";


  html +=
    "</div>";


  // BUTTON INFO

  html +=
    "<div class='box'>";


  html +=
    "<h2>Playlist Button</h2>";


  html +=
    "<p>"
    "The onboard ESP32 BOOT button is used."
    "</p>";


  html +=
    "<p>"
    "<b>Press once:</b> next playlist<br>"
    "<b>1 → 2 → 3 → 1</b>"
    "</p>";


  html +=
    "<p class='small'>"
    "The playlist splash screen appears for 2 seconds after each press."
    "</p>";


  html +=
    "</div>";


  // PAGE TYPES

  html +=
    "<div class='box'>";


  html +=
    "<h2>Page types</h2>";


  html +=
    "<p>"
    "Clock, Weather, HA Entity, Text, Network, MQTT, NTP and Uptime."
    "</p>";


  html +=
    "</div>";


  html +=
    webFooter();


  server.send(
    200,
    "text/html",
    html
  );
}


// ============================================================
// SAVE WEB CONFIG
// ============================================================

void handleSave() {

  // ----------------------------------------------------------
  // PLAYLIST 1
  // ----------------------------------------------------------

  if (
    server.hasArg("p1_name")
  )
    playlist1Name =
      server.arg("p1_name");


  for (
    int i = 0;
    i < playlist1Count;
    i++
  ) {

    String typeName =
      "p1_type_" + String(i);

    String titleName =
      "p1_title_" + String(i);

    String dataName =
      "p1_data_" + String(i);


    if (
      server.hasArg(typeName)
    )
      playlist1[i].type =
        server.arg(typeName).toInt();


    if (
      server.hasArg(titleName)
    )
      playlist1[i].title =
        server.arg(titleName);


    if (
      server.hasArg(dataName)
    )
      playlist1[i].data =
        server.arg(dataName);
  }


  // ----------------------------------------------------------
  // PLAYLIST 2
  // ----------------------------------------------------------

  if (
    server.hasArg("p2_name")
  )
    playlist2Name =
      server.arg("p2_name");


  for (
    int i = 0;
    i < playlist2Count;
    i++
  ) {

    String typeName =
      "p2_type_" + String(i);

    String titleName =
      "p2_title_" + String(i);

    String dataName =
      "p2_data_" + String(i);


    if (
      server.hasArg(typeName)
    )
      playlist2[i].type =
        server.arg(typeName).toInt();


    if (
      server.hasArg(titleName)
    )
      playlist2[i].title =
        server.arg(titleName);


    if (
      server.hasArg(dataName)
    )
      playlist2[i].data =
        server.arg(dataName);
  }


  // ----------------------------------------------------------
  // PLAYLIST 3
  // ----------------------------------------------------------

  if (
    server.hasArg("p3_name")
  )
    playlist3Name =
      server.arg("p3_name");


  for (
    int i = 0;
    i < playlist3Count;
    i++
  ) {

    String typeName =
      "p3_type_" + String(i);

    String titleName =
      "p3_title_" + String(i);

    String dataName =
      "p3_data_" + String(i);


    if (
      server.hasArg(typeName)
    )
      playlist3[i].type =
        server.arg(typeName).toInt();


    if (
      server.hasArg(titleName)
    )
      playlist3[i].title =
        server.arg(titleName);


    if (
      server.hasArg(dataName)
    )
      playlist3[i].data =
        server.arg(dataName);
  }


  // ----------------------------------------------------------
  // HA
  // ----------------------------------------------------------

  if (
    server.hasArg("haurl")
  )
    haUrl =
      server.arg("haurl");


  if (
    server.hasArg("hatoken")
  )
    haToken =
      server.arg("hatoken");


  if (
    server.hasArg("weather")
  )
    weatherEntity =
      server.arg("weather");


  // Remove trailing slash

  while (
    haUrl.endsWith("/")
  ) {

    haUrl.remove(
      haUrl.length() - 1
    );
  }


  // Ensure weather default

  if (
    weatherEntity.length() == 0
  ) {

    weatherEntity =
      "weather.forecast_home";
  }


  saveConfig();


  // Clear HA cache

  haCurrentEntity = "";
  haCurrentState = "";
  haCurrentFriendlyName = "";

  weatherState = "";
  weatherTemperature = NAN;


  lastHAUpdate =
    millis() - HA_INTERVAL - 1;


  lastPageChange =
    millis();


  server.sendHeader(
    "Location",
    "/"
  );


  server.send(
    303
  );
}


// ============================================================
// GET /SAVE
// ============================================================

void handleSaveGet() {

  // This prevents /save from appearing broken
  // when opened directly in a browser.

  server.sendHeader(
    "Location",
    "/"
  );


  server.send(
    303
  );
}


// ============================================================
// RESET TO DEFAULTS
// ============================================================

void handleReset() {

  createDefaults();

  saveConfig();


  selectedPlaylist = 1;

  playlist1Index = 0;
  playlist2Index = 0;
  playlist3Index = 0;


  haCurrentEntity = "";
  haCurrentState = "";
  haCurrentFriendlyName = "";

  weatherState = "";
  weatherTemperature = NAN;


  lastHAUpdate =
    millis() - HA_INTERVAL - 1;


  lastPageChange =
    millis();


  playlistSplash = true;

  playlistSplashStarted =
    millis();


  publishMQTTState();

  drawCurrentPage();


  server.sendHeader(
    "Location",
    "/"
  );


  server.send(
    303
  );
}


// ============================================================
// ADD PAGE
// ============================================================

void handleAdd() {

  int playlist =
    server.arg("p").toInt();


  if (
    playlist == 1
  ) {

    if (
      playlist1Count < MAX_PAGES
    ) {

      playlist1[
        playlist1Count
      ] =
        makePage(
          PAGE_TEXT,
          "New Page",
          "Edit me"
        );


      playlist1Count++;


      saveConfig();
    }
  }


  else if (
    playlist == 2
  ) {

    if (
      playlist2Count < MAX_PAGES
    ) {

      playlist2[
        playlist2Count
      ] =
        makePage(
          PAGE_TEXT,
          "New Page",
          "Edit me"
        );


      playlist2Count++;


      saveConfig();
    }
  }


  else if (
    playlist == 3
  ) {

    if (
      playlist3Count < MAX_PAGES
    ) {

      playlist3[
        playlist3Count
      ] =
        makePage(
          PAGE_TEXT,
          "New Page",
          "Edit me"
        );


      playlist3Count++;


      saveConfig();
    }
  }


  server.sendHeader(
    "Location",
    "/"
  );


  server.send(
    303
  );
}


// ============================================================
// REMOVE PAGE
// ============================================================

void handleRemove() {

  int playlist =
    server.arg("p").toInt();


  int index =
    server.arg("i").toInt();


  if (
    index < 0 ||
    index >= MAX_PAGES
  ) {

    server.send(
      400,
      "text/plain",
      "Invalid page"
    );

    return;
  }


  if (
    playlist == 1
  ) {

    if (
      playlist1Count <= 1 ||
      index >= playlist1Count
    ) {

      server.sendHeader(
        "Location",
        "/"
      );

      server.send(
        303
      );

      return;
    }


    for (
      int i = index;
      i < playlist1Count - 1;
      i++
    ) {

      playlist1[i] =
        playlist1[i + 1];
    }


    playlist1Count--;


    if (
      playlist1Index >= playlist1Count
    )
      playlist1Index =
        playlist1Count - 1;


    saveConfig();
  }


  else if (
    playlist == 2
  ) {

    if (
      playlist2Count <= 1 ||
      index >= playlist2Count
    ) {

      server.sendHeader(
        "Location",
        "/"
      );

      server.send(
        303
      );

      return;
    }


    for (
      int i = index;
      i < playlist2Count - 1;
      i++
    ) {

      playlist2[i] =
        playlist2[i + 1];
    }


    playlist2Count--;


    if (
      playlist2Index >= playlist2Count
    )
      playlist2Index =
        playlist2Count - 1;


    saveConfig();
  }


  else if (
    playlist == 3
  ) {

    if (
      playlist3Count <= 1 ||
      index >= playlist3Count
    ) {

      server.sendHeader(
        "Location",
        "/"
      );

      server.send(
        303
      );

      return;
    }


    for (
      int i = index;
      i < playlist3Count - 1;
      i++
    ) {

      playlist3[i] =
        playlist3[i + 1];
    }


    playlist3Count--;


    if (
      playlist3Index >= playlist3Count
    )
      playlist3Index =
        playlist3Count - 1;


    saveConfig();
  }


  server.sendHeader(
    "Location",
    "/"
  );


  server.send(
    303
  );
}


// ============================================================
// MOVE PAGE
// ============================================================

void handleMove() {

  int playlist =
    server.arg("p").toInt();


  int index =
    server.arg("i").toInt();


  String direction =
    server.arg("d");


  if (
    playlist == 1
  ) {

    if (
      index >= 0 &&
      index < playlist1Count
    ) {

      if (
        direction == "up" &&
        index > 0
      ) {

        Page temp =
          playlist1[index - 1];

        playlist1[index - 1] =
          playlist1[index];

        playlist1[index] =
          temp;
      }


      else if (
        direction == "down" &&
        index < playlist1Count - 1
      ) {

        Page temp =
          playlist1[index + 1];

        playlist1[index + 1] =
          playlist1[index];

        playlist1[index] =
          temp;
      }


      saveConfig();
    }
  }


  else if (
    playlist == 2
  ) {

    if (
      index >= 0 &&
      index < playlist2Count
    ) {

      if (
        direction == "up" &&
        index > 0
      ) {

        Page temp =
          playlist2[index - 1];

        playlist2[index - 1] =
          playlist2[index];

        playlist2[index] =
          temp;
      }


      else if (
        direction == "down" &&
        index < playlist2Count - 1
      ) {

        Page temp =
          playlist2[index + 1];

        playlist2[index + 1] =
          playlist2[index];

        playlist2[index] =
          temp;
      }


      saveConfig();
    }
  }


  else if (
    playlist == 3
  ) {

    if (
      index >= 0 &&
      index < playlist3Count
    ) {

      if (
        direction == "up" &&
        index > 0
      ) {

        Page temp =
          playlist3[index - 1];

        playlist3[index - 1] =
          playlist3[index];

        playlist3[index] =
          temp;
      }


      else if (
        direction == "down" &&
        index < playlist3Count - 1
      ) {

        Page temp =
          playlist3[index + 1];

        playlist3[index + 1] =
          playlist3[index];

        playlist3[index] =
          temp;
      }


      saveConfig();
    }
  }


  server.sendHeader(
    "Location",
    "/"
  );


  server.send(
    303
  );
}


// ============================================================
// WEB SERVER
// ============================================================

void startWebServer() {

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );


  server.on(
    "/save",
    HTTP_POST,
    handleSave
  );


  server.on(
    "/save",
    HTTP_GET,
    handleSaveGet
  );


  server.on(
    "/reset",
    HTTP_GET,
    handleReset
  );


  server.on(
    "/add",
    HTTP_GET,
    handleAdd
  );


  server.on(
    "/remove",
    HTTP_GET,
    handleRemove
  );


  server.on(
    "/move",
    HTTP_GET,
    handleMove
  );


  server.begin();
}


// ============================================================
// WIFI
// ============================================================

void connectWiFi() {

  WiFi.mode(
    WIFI_STA
  );


  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );


  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(
    0,
    0
  );

  display.print(
    "DeskBuddy"
  );


  display.setCursor(
    0,
    18
  );

  display.print(
    "Connecting WiFi..."
  );


  display.display();


  unsigned long start =
    millis();


  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 30000
  ) {

    delay(250);
  }


  display.clearDisplay();

  display.setCursor(
    0,
    0
  );


  if (
    WiFi.status() == WL_CONNECTED
  ) {

    display.print(
      "WiFi connected"
    );


    display.setCursor(
      0,
      15
    );


    display.print(
      WiFi.localIP()
    );
  }

  else {

    display.print(
      "WiFi failed"
    );
  }


  display.display();


  delay(1000);
}


// ============================================================
// NTP
// ============================================================

void startNTP() {

  configTime(
    0,
    0,
    NTP_SERVER
  );


  setenv(
    "TZ",
    UK_TZ,
    1
  );


  tzset();
}


// ============================================================
// MDNS
// ============================================================

void startMDNS() {

  if (
    MDNS.begin("deskbuddy")
  ) {

    MDNS.addService(
      "http",
      "tcp",
      80
    );
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(
    115200
  );


  // ----------------------------------------------------------
  // BOOT BUTTON
  // ----------------------------------------------------------

  pinMode(
    BOOT_BUTTON_PIN,
    INPUT_PULLUP
  );


  lastButtonState =
    digitalRead(
      BOOT_BUTTON_PIN
    );


  // ----------------------------------------------------------
  // OLED
  // ----------------------------------------------------------

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );


  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDR
    )
  ) {

    while (true) {

      delay(1000);
    }
  }


  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(2);

  display.setCursor(
    15,
    20
  );

  display.print(
    "DeskBuddy"
  );

  display.display();


  delay(1000);


  // ----------------------------------------------------------
  // CONFIG
  // ----------------------------------------------------------

  loadConfig();


  // ----------------------------------------------------------
  // ALWAYS START PLAYLIST 1
  // ----------------------------------------------------------

  selectedPlaylist = 1;

  playlist1Index = 0;
  playlist2Index = 0;
  playlist3Index = 0;


  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  connectWiFi();


  // ----------------------------------------------------------
  // NTP / MDNS / WEB
  // ----------------------------------------------------------

  if (
    WiFi.status() == WL_CONNECTED
  ) {

    startNTP();

    startMDNS();

    startWebServer();
  }


  // ----------------------------------------------------------
  // MQTT
  // ----------------------------------------------------------

  mqtt.setServer(
    MQTT_HOST,
    MQTT_PORT
  );


  mqtt.setCallback(
    mqttCallback
  );


  // ----------------------------------------------------------
  // FORCE FIRST HA FETCH
  // ----------------------------------------------------------

  lastHAUpdate =
    millis() - HA_INTERVAL - 1;


  lastPageChange =
    millis();


  drawCurrentPage();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // ----------------------------------------------------------
  // WEB
  // ----------------------------------------------------------

  server.handleClient();


  // ----------------------------------------------------------
  // BOOT BUTTON
  // ----------------------------------------------------------

  handleBootButton();


  // ----------------------------------------------------------
  // PLAYLIST SPLASH TIMER
  // ----------------------------------------------------------

  if (
    playlistSplash &&
    millis() - playlistSplashStarted >=
      PLAYLIST_SPLASH_TIME
  ) {

    playlistSplash =
      false;


    lastPageChange =
      millis();


    forceHAUpdate();


    publishMQTTState();


    drawCurrentPage();
  }


  // ----------------------------------------------------------
  // WIFI RECONNECT
  // ----------------------------------------------------------

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    static unsigned long lastReconnect = 0;


    if (
      millis() - lastReconnect >
      10000
    ) {

      lastReconnect =
        millis();


      WiFi.disconnect();


      WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
      );
    }
  }


  // ----------------------------------------------------------
  // MQTT
  // ----------------------------------------------------------

  if (
    WiFi.status() == WL_CONNECTED
  ) {

    if (
      !mqtt.connected()
    ) {

      static unsigned long lastMQTTAttempt = 0;


      if (
        millis() - lastMQTTAttempt >
        5000
      ) {

        lastMQTTAttempt =
          millis();


        connectMQTT();
      }
    }

    else {

      mqtt.loop();
    }
  }


  // ----------------------------------------------------------
  // DON'T AUTO-SCROLL DURING SPLASH
  // ----------------------------------------------------------

  if (
    !playlistSplash
  ) {

    if (
      millis() - lastPageChange >=
      PAGE_INTERVAL
    ) {

      lastPageChange =
        millis();


      if (
        selectedPlaylist == 1
      ) {

        playlist1Index++;


        if (
          playlist1Index >=
          playlist1Count
        )
          playlist1Index = 0;
      }


      else if (
        selectedPlaylist == 2
      ) {

        playlist2Index++;


        if (
          playlist2Index >=
          playlist2Count
        )
          playlist2Index = 0;
      }


      else {

        playlist3Index++;


        if (
          playlist3Index >=
          playlist3Count
        )
          playlist3Index = 0;
      }


      // Clear entity cache so new page loads immediately

      haCurrentEntity = "";
      haCurrentState = "";
      haCurrentFriendlyName = "";


      lastHAUpdate =
        millis() - HA_INTERVAL - 1;


      forceHAUpdate();


      publishMQTTState();


      drawCurrentPage();
    }
  }


  // ----------------------------------------------------------
  // HA
  // ----------------------------------------------------------

  if (
    !playlistSplash
  ) {

    updateHAData();
  }


  // ----------------------------------------------------------
  // DISPLAY
  // ----------------------------------------------------------

  if (
    millis() - lastDisplayUpdate >=
    DISPLAY_INTERVAL
  ) {

    lastDisplayUpdate =
      millis();


    drawCurrentPage();
  }


  // ----------------------------------------------------------
  // MQTT STATE
  // ----------------------------------------------------------

  if (
    millis() - lastMQTTPublish >=
    10000
  ) {

    lastMQTTPublish =
      millis();


    publishMQTTState();
  }


  delay(5);
}
