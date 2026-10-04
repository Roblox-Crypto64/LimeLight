#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <time.h>

// ============================================================
// LIMETOOLS
// ESP32 + SSD1306 OLED
//
// GPIO 21 -> SDA
// GPIO 22 -> SCL
// GPIO 0  -> BOOT button
//
// No emojis are used.
// ============================================================

// -------------------- NETWORK --------------------

const char* WIFI_SSID = "CAPCOMLow";
const char* WIFI_PASSWORD = "WowzerMowzer1!";

const char* NTP_SERVER = "192.168.0.152";
const char* TIMEZONE = "GMT0BST,M3.5.0/1,M10.5.0/2";

// -------------------- OLED --------------------

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// -------------------- WEB --------------------

WebServer server(80);

// -------------------- BUTTON --------------------

#define BUTTON_PIN 0

const unsigned long DEBOUNCE_TIME = 50;
const unsigned long DOUBLE_PRESS_TIME = 2000;
const unsigned long LONG_PRESS_TIME = 2000;

// -------------------- TIMEOUTS --------------------

const unsigned long HOME_SLEEP_TIME = 5000;
const unsigned long LIST_SLEEP_TIME = 5000;

const unsigned long POWER_OFF_TIME = 120000;

const unsigned long BOOT_MINIMUM_TIME = 3000;
const unsigned long WIFI_CONNECT_TIMEOUT = 10000;
const unsigned long WIFI_DOWN_DELAY = 3000;

const unsigned long PINGER_INTERVAL = 3000;
const unsigned long PINGER_MAX_TIME = 15000;

// -------------------- ENUMS --------------------

enum ScreenState {
  STATE_HOME,
  STATE_CATEGORY_LIST,
  STATE_TOOL_LIST,
  STATE_TOOL,
  STATE_HELP,
  STATE_SETTINGS,
  STATE_WIFI
};

enum Category {
  CAT_CORE,
  CAT_RANDOM,
  CAT_BREATHING,
  CAT_NETWORK,
  CAT_SYSTEM
};

enum Tool {
  TOOL_STOPWATCH,
  TOOL_TIMER,
  TOOL_CLOCK,
  TOOL_DATE,
  TOOL_DICE,
  TOOL_2DICE,
  TOOL_COIN,
  TOOL_NUMBER,
  TOOL_YESNO,
  TOOL_BREATHING,
  TOOL_PINGER,
  TOOL_SYSTEM,
  TOOL_COUNT
};

enum TimerState {
  TIMER_STOPPED,
  TIMER_RUNNING,
  TIMER_FINISHED
};

enum BreathPattern {
  BREATH_BOX,
  BREATH_478,
  BREATH_RELAX,
  BREATH_CUSTOM
};

enum AutoSleepSetting {
  SLEEP_OFF,
  SLEEP_3SEC,
  SLEEP_10SEC,
  SLEEP_1MIN,
  SLEEP_5MIN,
  SLEEP_7MIN
};

// -------------------- STATE --------------------

ScreenState screenState = STATE_HOME;

Category currentCategory = CAT_CORE;
Tool currentTool = TOOL_STOPWATCH;

int categoryIndex = 0;
int toolIndex = 0;

bool waitingForSecondPress = false;
unsigned long firstPressTime = 0;

bool buttonReading = HIGH;
bool buttonStable = HIGH;
unsigned long buttonChangedAt = 0;
unsigned long buttonDownAt = 0;

bool longPressHandled = false;

// -------------------- ACTIVITY --------------------

unsigned long lastActivity = 0;

// -------------------- WIFI --------------------

bool wifiManualOff = false;
bool wifiWasConnected = false;
unsigned long wifiLostAt = 0;
bool networkOfflineMode = false;

bool powerOffState = false;

// -------------------- NTP --------------------

bool timeValid = false;
unsigned long lastNtpCheck = 0;

// -------------------- STOPWATCH --------------------

bool stopwatchRunning = false;
unsigned long stopwatchStarted = 0;
unsigned long stopwatchElapsed = 0;

// -------------------- TIMER --------------------

TimerState timerState = TIMER_STOPPED;

unsigned long timerDuration = 60000;
unsigned long timerRemaining = 60000;
unsigned long timerStartMillis = 0;

// -------------------- RANDOM --------------------

int dice1 = 1;
int dice2 = 1;

String coinResult = "HEADS";
int randomNumber = 0;

int randomMin = 1;
int randomMax = 100;

// -------------------- BREATHING --------------------

bool breathingRunning = false;

BreathPattern breathPattern = BREATH_BOX;

int breathPhase = 0;
unsigned long breathPhaseStarted = 0;
unsigned long breathPhaseLength = 4000;

String breathPhaseName = "INHALE";

int customInhale = 4;
int customHold = 4;
int customExhale = 4;

// -------------------- PINGER --------------------

struct PingPreset {
  String name;
  String ip;
};

PingPreset pingPresets[5];

int pingIndex = 0;

bool pingerRunning = false;
unsigned long pingerStarted = 0;
unsigned long lastPing = 0;

long lastPingResult = -1;
bool lastPingSuccess = false;

// -------------------- SETTINGS --------------------

AutoSleepSetting autoSleepSetting = SLEEP_7MIN;

// -------------------- HELP --------------------

int helpIndex = 0;

// -------------------- SYSTEM MENU --------------------

int systemIndex = 0;

// -------------------- SETTINGS STORAGE --------------------

Preferences prefs;

// ============================================================
// BASIC HELPERS
// ============================================================

void touchActivity() {
  lastActivity = millis();
}

String getTimeString() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 100)) {
    return "--:--";
  }

  if (timeinfo.tm_year < 120) {
    return "--:--";
  }

  char buffer[8];
  strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);

  return String(buffer);
}

String getDateString() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 100)) {
    return "--/--/----";
  }

  char buffer[16];
  strftime(buffer, sizeof(buffer), "%d/%m/%Y", &timeinfo);

  return String(buffer);
}

String getDayString() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 100)) {
    return "Unknown";
  }

  char buffer[16];
  strftime(buffer, sizeof(buffer), "%A", &timeinfo);

  return String(buffer);
}

String formatTime(unsigned long ms) {
  unsigned long totalSeconds = ms / 1000;

  unsigned long minutes = totalSeconds / 60;
  unsigned long seconds = totalSeconds % 60;

  char buffer[16];

  sprintf(
    buffer,
    "%02lu:%02lu",
    minutes,
    seconds
  );

  return String(buffer);
}

String formatStopwatch(unsigned long ms) {
  unsigned long minutes = ms / 60000;
  unsigned long seconds = (ms / 1000) % 60;
  unsigned long hundredths = (ms / 10) % 100;

  char buffer[20];

  sprintf(
    buffer,
    "%02lu:%02lu.%02lu",
    minutes,
    seconds,
    hundredths
  );

  return String(buffer);
}

// ============================================================
// OLED HELPERS
// ============================================================

void oledClear() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

void oledTitle(String title) {
  oledClear();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("LimeTools");

  display.setCursor(0, 10);
  display.println(title);

  display.drawLine(
    0,
    19,
    127,
    19,
    SSD1306_WHITE
  );
}

// ============================================================
// HOME
// ============================================================

void drawHome() {
  oledClear();

  display.setTextSize(2);
  display.setCursor(0, 5);
  display.println("LimeLight");

  display.setTextSize(2);
  display.setCursor(24, 36);
  display.println(getTimeString());

  if (networkOfflineMode) {
    display.setTextSize(1);
    display.setCursor(105, 0);
    display.print("N/I");
  }

  display.display();
}

// ============================================================
// CATEGORY LIST
// ============================================================

void drawCategoryList() {
  oledTitle("Categories");

  const char* names[] = {
    "Core",
    "Random",
    "Breathing",
    "Network",
    "System"
  };

  for (int i = 0; i < 3; i++) {
    int index = (categoryIndex + i) % 5;

    display.setCursor(
      0,
      25 + (i * 11)
    );

    if (i == 0) {
      display.print("> ");
    } else {
      display.print("  ");
    }

    display.println(names[index]);
  }

  display.display();
}

// ============================================================
// TOOL NAMES
// ============================================================

String getToolName(Tool tool) {
  switch (tool) {
    case TOOL_STOPWATCH:
      return "Stopwatch";

    case TOOL_TIMER:
      return "Timer";

    case TOOL_CLOCK:
      return "Clock";

    case TOOL_DATE:
      return "Date";

    case TOOL_DICE:
      return "Dice";

    case TOOL_2DICE:
      return "2 Dice";

    case TOOL_COIN:
      return "Coin";

    case TOOL_NUMBER:
      return "Number";

    case TOOL_YESNO:
      return "Yes / No";

    case TOOL_BREATHING:
      return "Breathing";

    case TOOL_PINGER:
      return "Pinger";

    case TOOL_SYSTEM:
      return "System";

    default:
      return "Unknown";
  }
}

// ============================================================
// CATEGORY TOOLS
// ============================================================

void getCategoryTools(
  Category cat,
  Tool* output,
  int &count
) {
  count = 0;

  if (cat == CAT_CORE) {
    output[count++] = TOOL_STOPWATCH;
    output[count++] = TOOL_TIMER;
    output[count++] = TOOL_CLOCK;
    output[count++] = TOOL_DATE;
  }

  else if (cat == CAT_RANDOM) {
    output[count++] = TOOL_DICE;
    output[count++] = TOOL_2DICE;
    output[count++] = TOOL_COIN;
    output[count++] = TOOL_NUMBER;
    output[count++] = TOOL_YESNO;
  }

  else if (cat == CAT_BREATHING) {
    output[count++] = TOOL_BREATHING;
  }

  else if (cat == CAT_NETWORK) {
    output[count++] = TOOL_PINGER;
  }

  else if (cat == CAT_SYSTEM) {
    output[count++] = TOOL_SYSTEM;
  }
}

// ============================================================
// TOOL LIST
// ============================================================

void drawToolList() {
  oledTitle("Tools");

  Tool tools[10];
  int count;

  getCategoryTools(
    currentCategory,
    tools,
    count
  );

  if (count == 0) {
    display.display();
    return;
  }

  if (toolIndex >= count) {
    toolIndex = 0;
  }

  for (int i = 0; i < 3 && i < count; i++) {

    int index =
      (toolIndex + i) % count;

    display.setCursor(
      0,
      25 + (i * 11)
    );

    if (i == 0) {
      display.print("> ");
    } else {
      display.print("  ");
    }

    display.println(
      getToolName(tools[index])
    );
  }

  display.display();
}

// ============================================================
// STOPWATCH SCREEN
// ============================================================

void drawStopwatch() {
  oledTitle("Stopwatch");

  unsigned long elapsed =
    stopwatchElapsed;

  if (stopwatchRunning) {
    elapsed +=
      millis() - stopwatchStarted;
  }

  display.setTextSize(2);
  display.setCursor(0, 29);
  display.println(
    formatStopwatch(elapsed)
  );

  display.setTextSize(1);
  display.setCursor(0, 51);

  if (stopwatchRunning) {
    display.println("RUNNING");
  } else {
    display.println("STOPPED");
  }

  display.display();
}

// ============================================================
// TIMER SCREEN
// ============================================================

void drawTimer() {
  oledTitle("Timer");

  unsigned long remaining =
    timerRemaining;

  if (timerState == TIMER_RUNNING) {

    unsigned long elapsed =
      millis() - timerStartMillis;

    if (elapsed >= timerDuration) {
      remaining = 0;
    } else {
      remaining =
        timerDuration - elapsed;
    }
  }

  display.setTextSize(2);
  display.setCursor(20, 30);
  display.println(
    formatTime(remaining)
  );

  display.setTextSize(1);
  display.setCursor(0, 52);

  if (timerState == TIMER_RUNNING) {
    display.println("RUNNING");
  }

  else if (timerState == TIMER_FINISHED) {
    display.println("FINISHED");
  }

  else {
    display.println("STOPPED");
  }

  display.display();
}

// ============================================================
// CLOCK
// ============================================================

void drawClock() {
  oledTitle("Clock");

  display.setTextSize(3);
  display.setCursor(7, 30);
  display.println(getTimeString());

  display.display();
}

// ============================================================
// DATE
// ============================================================

void drawDate() {
  oledTitle("Date");

  display.setTextSize(2);
  display.setCursor(8, 29);
  display.println(getDateString());

  display.setTextSize(1);
  display.setCursor(31, 52);
  display.println(getDayString());

  display.display();
}

// ============================================================
// DICE
// ============================================================

void drawDiceFace(
  int value,
  int x,
  int y,
  int size
) {
  display.drawRect(
    x,
    y,
    size,
    size,
    SSD1306_WHITE
  );

  int cx = x + size / 2;
  int cy = y + size / 2;

  int q1x = x + size / 4;
  int q3x = x + (size * 3) / 4;

  int q1y = y + size / 4;
  int q3y = y + (size * 3) / 4;

  if (value == 1) {
    display.fillCircle(
      cx,
      cy,
      3,
      SSD1306_WHITE
    );
  }

  if (value == 2) {
    display.fillCircle(q1x, q1y, 3, SSD1306_WHITE);
    display.fillCircle(q3x, q3y, 3, SSD1306_WHITE);
  }

  if (value == 3) {
    display.fillCircle(q1x, q1y, 3, SSD1306_WHITE);
    display.fillCircle(cx, cy, 3, SSD1306_WHITE);
    display.fillCircle(q3x, q3y, 3, SSD1306_WHITE);
  }

  if (value == 4) {
    display.fillCircle(q1x, q1y, 3, SSD1306_WHITE);
    display.fillCircle(q3x, q1y, 3, SSD1306_WHITE);
    display.fillCircle(q1x, q3y, 3, SSD1306_WHITE);
    display.fillCircle(q3x, q3y, 3, SSD1306_WHITE);
  }

  if (value == 5) {
    display.fillCircle(q1x, q1y, 3, SSD1306_WHITE);
    display.fillCircle(q3x, q1y, 3, SSD1306_WHITE);
    display.fillCircle(cx, cy, 3, SSD1306_WHITE);
    display.fillCircle(q1x, q3y, 3, SSD1306_WHITE);
    display.fillCircle(q3x, q3y, 3, SSD1306_WHITE);
  }

  if (value == 6) {
    display.fillCircle(q1x, q1y, 3, SSD1306_WHITE);
    display.fillCircle(q3x, q1y, 3, SSD1306_WHITE);
    display.fillCircle(q1x, cy, 3, SSD1306_WHITE);
    display.fillCircle(q3x, cy, 3, SSD1306_WHITE);
    display.fillCircle(q1x, q3y, 3, SSD1306_WHITE);
    display.fillCircle(q3x, q3y, 3, SSD1306_WHITE);
  }
}

void drawDice() {
  oledTitle("Dice");

  drawDiceFace(
    dice1,
    45,
    27,
    38
  );

  display.display();
}

void draw2Dice() {
  oledTitle("2 Dice");

  drawDiceFace(
    dice1,
    15,
    28,
    30
  );

  drawDiceFace(
    dice2,
    83,
    28,
    30
  );

  display.display();
}

// ============================================================
// COIN
// ============================================================

void drawCoin() {
  oledTitle("Coin");

  display.drawCircle(
    64,
    40,
    18,
    SSD1306_WHITE
  );

  display.setTextSize(1);
  display.setCursor(43, 37);
  display.println(coinResult);

  display.display();
}

// ============================================================
// NUMBER
// ============================================================

void drawNumber() {
  oledTitle("Number");

  display.setTextSize(3);
  display.setCursor(22, 31);
  display.println(randomNumber);

  display.display();
}

// ============================================================
// YES / NO
// ============================================================

void drawYesNo() {
  oledTitle("Yes / No");

  display.setTextSize(3);
  display.setCursor(42, 31);

  if (randomNumber == 1) {
    display.println("YES");
  } else {
    display.println("NO");
  }

  display.display();
}

// ============================================================
// BREATHING
// ============================================================

void drawBreathing() {
  oledTitle("Breathing");

  display.setTextSize(1);
  display.setCursor(0, 25);
  display.println(breathPhaseName);

  display.drawCircle(
    95,
    42,
    15,
    SSD1306_WHITE
  );

  if (breathingRunning) {

    unsigned long elapsed =
      millis() - breathPhaseStarted;

    int radius = 10;

    if (breathPhaseName == "INHALE") {

      radius = map(
        min(elapsed, breathPhaseLength),
        0,
        breathPhaseLength,
        4,
        15
      );
    }

    else if (breathPhaseName == "EXHALE") {

      radius = map(
        min(elapsed, breathPhaseLength),
        0,
        breathPhaseLength,
        15,
        4
      );
    }

    display.fillCircle(
      95,
      42,
      radius,
      SSD1306_WHITE
    );
  }

  display.setCursor(0, 42);

  if (breathingRunning) {

    unsigned long elapsed =
      millis() - breathPhaseStarted;

    unsigned long used =
      min(elapsed, breathPhaseLength);

    unsigned long remaining =
      breathPhaseLength - used;

    display.println(
      String((remaining + 999) / 1000) +
      " sec"
    );
  }

  else {
    display.println("STOPPED");
  }

  display.display();
}

// ============================================================
// PINGER
// ============================================================

void drawPinger() {
  oledTitle("Pinger");

  display.setCursor(0, 25);
  display.println(
    pingPresets[pingIndex].name
  );

  display.setCursor(0, 36);
  display.println(
    pingPresets[pingIndex].ip
  );

  display.setTextSize(2);
  display.setCursor(0, 48);

  if (!pingerRunning) {

    if (lastPingSuccess) {
      display.print(lastPingResult);
      display.println(" ms");
    } else {
      display.println("READY");
    }
  }

  else if (!lastPingSuccess) {
    display.println("TIMEOUT");
  }

  else {
    display.print(lastPingResult);
    display.println(" ms");
  }

  display.display();
}

// ============================================================
// SYSTEM
// ============================================================

void drawSystem() {
  oledTitle("System");

  const char* options[] = {
    "Sleep",
    "Shut Down",
    "WiFi",
    "Settings",
    "Help"
  };

  for (int i = 0; i < 3; i++) {

    int index =
      (systemIndex + i) % 5;

    display.setCursor(
      0,
      25 + (i * 11)
    );

    if (i == 0) {
      display.print("> ");
    } else {
      display.print("  ");
    }

    display.println(options[index]);
  }

  display.display();
}

// ============================================================
// HELP
// ============================================================

void drawHelp() {
  oledClear();

  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println("HELP");

  display.drawLine(
    0,
    10,
    127,
    10,
    SSD1306_WHITE
  );

  String title;
  String line1;
  String line2;
  String line3;

  switch (helpIndex) {

    case 0:
      title = "GENERAL";
      line1 = "1 press: next";
      line2 = "2 presses: select";
      line3 = "Hold 2 sec: home";
      break;

    case 1:
      title = "STOPWATCH";
      line1 = "1 press: start/stop";
      line2 = "2 presses: reset";
      line3 = "Hold 2 sec: home";
      break;

    case 2:
      title = "TIMER";
      line1 = "1 press: start/stop";
      line2 = "2 presses: reset";
      line3 = "Hold 2 sec: home";
      break;

    case 3:
      title = "DICE";
      line1 = "1 press: roll";
      line2 = "Hold 2 sec: home";
      line3 = "";
      break;

    case 4:
      title = "BREATHING";
      line1 = "1 press: start/stop";
      line2 = "Hold 2 sec: home";
      line3 = "";
      break;

    case 5:
      title = "PINGER";
      line1 = "1 press: stop";
      line2 = "Max 15 seconds";
      line3 = "Hold 2 sec: home";
      break;

    default:
      title = "GENERAL";
      line1 = "1 press: next";
      line2 = "2 presses: select";
      line3 = "Hold 2 sec: home";
      break;
  }

  display.setCursor(0, 18);
  display.println(title);

  display.setCursor(0, 31);
  display.println(line1);

  display.setCursor(0, 42);
  display.println(line2);

  display.setCursor(0, 53);
  display.println(line3);

  display.display();
}

// ============================================================
// SETTINGS SCREEN
// ============================================================

String getAutoSleepName() {

  switch (autoSleepSetting) {

    case SLEEP_OFF:
      return "OFF";

    case SLEEP_3SEC:
      return "3 SEC";

    case SLEEP_10SEC:
      return "10 SEC";

    case SLEEP_1MIN:
      return "1 MIN";

    case SLEEP_5MIN:
      return "5 MIN";

    case SLEEP_7MIN:
      return "7 MIN";
  }

  return "7 MIN";
}

void drawSettings() {
  oledTitle("Settings");

  display.setCursor(0, 25);
  display.println("Auto Sleep:");

  display.setTextSize(2);
  display.setCursor(0, 38);
  display.println(getAutoSleepName());

  display.setTextSize(1);
  display.setCursor(0, 55);
  display.println("Press to change");

  display.display();
}

// ============================================================
// WIFI SCREEN
// ============================================================

void drawWiFi() {
  oledTitle("WiFi");

  display.setCursor(0, 25);

  if (wifiManualOff) {
    display.println("WiFi OFF");
  }

  else if (WiFi.status() == WL_CONNECTED) {
    display.println("CONNECTED");
  }

  else {
    display.println("DISCONNECTED");
  }

  display.setCursor(0, 37);

  if (WiFi.status() == WL_CONNECTED) {
    display.println(
      WiFi.localIP().toString()
    );
  } else {
    display.println("No connection");
  }

  display.setCursor(0, 51);

  if (wifiManualOff) {
    display.println("Press to turn ON");
  } else {
    display.println("Press to turn OFF");
  }

  display.display();
}

// ============================================================
// CURRENT SCREEN
// ============================================================

void drawCurrentScreen() {

  if (powerOffState) {
    display.clearDisplay();
    display.display();
    return;
  }

  switch (screenState) {

    case STATE_HOME:
      drawHome();
      break;

    case STATE_CATEGORY_LIST:
      drawCategoryList();
      break;

    case STATE_TOOL_LIST:
      drawToolList();
      break;

    case STATE_TOOL:

      switch (currentTool) {

        case TOOL_STOPWATCH:
          drawStopwatch();
          break;

        case TOOL_TIMER:
          drawTimer();
          break;

        case TOOL_CLOCK:
          drawClock();
          break;

        case TOOL_DATE:
          drawDate();
          break;

        case TOOL_DICE:
          drawDice();
          break;

        case TOOL_2DICE:
          draw2Dice();
          break;

        case TOOL_COIN:
          drawCoin();
          break;

        case TOOL_NUMBER:
          drawNumber();
          break;

        case TOOL_YESNO:
          drawYesNo();
          break;

        case TOOL_BREATHING:
          drawBreathing();
          break;

        case TOOL_PINGER:
          drawPinger();
          break;

        case TOOL_SYSTEM:
          drawSystem();
          break;

        default:
          break;
      }

      break;

    case STATE_HELP:
      drawHelp();
      break;

    case STATE_SETTINGS:
      drawSettings();
      break;

    case STATE_WIFI:
      drawWiFi();
      break;
  }
}

// ============================================================
// NTP
// ============================================================

void setupTime() {

  timeValid = false;

  configTzTime(
    TIMEZONE,
    NTP_SERVER
  );

  unsigned long started =
    millis();

  while (
    millis() - started <
    8000
  ) {

    struct tm timeinfo;

    if (
      getLocalTime(
        &timeinfo,
        250
      )
    ) {

      if (timeinfo.tm_year >= 120) {
        timeValid = true;
        return;
      }
    }

    delay(100);
  }

  timeValid = false;
}

// ============================================================
// WIFI CONNECTION
// ============================================================

bool connectWiFi() {

  if (wifiManualOff) {
    return false;
  }

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long started =
    millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - started <
    WIFI_CONNECT_TIMEOUT
  ) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {

    wifiWasConnected = true;
    networkOfflineMode = false;
    wifiLostAt = 0;

    return true;
  }

  return false;
}

void disconnectWiFi() {

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  wifiWasConnected = false;
}

void reconnectWiFi() {

  powerOffState = false;

  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("LimeLight");

  display.setCursor(0, 22);
  display.println("Reconnecting");

  display.setCursor(0, 34);
  display.println("To Wifi");

  display.display();

  wifiManualOff = false;

  if (connectWiFi()) {

    setupTime();

    networkOfflineMode = false;

    MDNS.end();
    MDNS.begin("lime");

    delay(500);

    drawCurrentScreen();
  }
}

// ============================================================
// WIFI MONITOR
// ============================================================

void monitorWiFi() {

  if (wifiManualOff) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {

    wifiWasConnected = true;
    wifiLostAt = 0;

    return;
  }

  if (!wifiWasConnected) {
    return;
  }

  if (wifiLostAt == 0) {
    wifiLostAt = millis();
  }

  if (
    millis() - wifiLostAt >=
    WIFI_DOWN_DELAY
  ) {

    if (!networkOfflineMode) {

      networkOfflineMode = true;

      display.clearDisplay();

      display.setTextSize(1);

      display.setCursor(0, 0);
      display.println("LimeLight");

      display.setCursor(0, 22);
      display.println("Internet Down");

      display.setCursor(0, 34);
      display.println("Waiting to");

      display.setCursor(0, 46);
      display.println("Reconnect");

      display.display();
    }
  }
}

// ============================================================
// SLEEP / POWER
// ============================================================

void enterSleep() {

  breathingRunning = false;

  screenState = STATE_HOME;

  waitingForSecondPress = false;

  touchActivity();

  drawHome();
}

void powerOffDevice() {

  powerOffState = true;

  display.clearDisplay();
  display.display();

  disconnectWiFi();
}

void wakeFromPowerOff() {

  powerOffState = false;

  wifiManualOff = false;

  if (connectWiFi()) {

    setupTime();

    MDNS.end();
    MDNS.begin("lime");
  }

  screenState = STATE_CATEGORY_LIST;

  categoryIndex = 0;
  toolIndex = 0;

  touchActivity();

  drawCategoryList();
}

// ============================================================
// TIMER
// ============================================================

void startTimer() {

  if (timerDuration == 0) {
    timerDuration = 1000;
  }

  timerRemaining =
    timerDuration;

  timerState =
    TIMER_RUNNING;

  timerStartMillis =
    millis();

  touchActivity();
}

void stopTimer() {

  if (timerState == TIMER_RUNNING) {

    unsigned long elapsed =
      millis() -
      timerStartMillis;

    if (elapsed >= timerDuration) {

      timerRemaining = 0;

      timerState =
        TIMER_FINISHED;
    }

    else {

      timerRemaining =
        timerDuration -
        elapsed;

      timerState =
        TIMER_STOPPED;
    }
  }

  touchActivity();
}

void resetTimer() {

  timerState =
    TIMER_STOPPED;

  timerRemaining =
    timerDuration;

  touchActivity();
}

// ============================================================
// STOPWATCH
// ============================================================

void startStopwatch() {

  if (!stopwatchRunning) {

    stopwatchStarted =
      millis();

    stopwatchRunning =
      true;
  }

  touchActivity();
}

void stopStopwatch() {

  if (stopwatchRunning) {

    stopwatchElapsed +=
      millis() -
      stopwatchStarted;

    stopwatchRunning =
      false;
  }

  touchActivity();
}

void resetStopwatch() {

  stopwatchRunning =
    false;

  stopwatchElapsed =
    0;

  touchActivity();
}

// ============================================================
// RANDOM
// ============================================================

void rollDice() {

  dice1 =
    random(1, 7);

  touchActivity();
}

void roll2Dice() {

  dice1 =
    random(1, 7);

  dice2 =
    random(1, 7);

  touchActivity();
}

void flipCoin() {

  if (random(0, 2) == 0) {
    coinResult = "HEADS";
  } else {
    coinResult = "TAILS";
  }

  touchActivity();
}

void generateNumber() {

  randomNumber =
    random(
      randomMin,
      randomMax + 1
    );

  touchActivity();
}

void generateYesNo() {

  randomNumber =
    random(0, 2);

  touchActivity();
}

// ============================================================
// BREATHING
// ============================================================

void setBreathingPattern(
  BreathPattern pattern
) {

  breathPattern =
    pattern;

  breathPhase =
    0;

  breathPhaseName =
    "INHALE";

  if (pattern == BREATH_BOX) {

    breathPhaseLength =
      4000;
  }

  else if (pattern == BREATH_478) {

    breathPhaseLength =
      4000;
  }

  else if (pattern == BREATH_RELAX) {

    breathPhaseLength =
      6000;
  }

  else {

    breathPhaseLength =
      customInhale * 1000;
  }

  breathPhaseStarted =
    millis();

  touchActivity();
}

void startBreathing() {

  breathingRunning =
    true;

  breathPhase =
    0;

  breathPhaseName =
    "INHALE";

  if (breathPattern == BREATH_BOX) {
    breathPhaseLength = 4000;
  }

  else if (breathPattern == BREATH_478) {
    breathPhaseLength = 4000;
  }

  else if (breathPattern == BREATH_RELAX) {
    breathPhaseLength = 6000;
  }

  else {
    breathPhaseLength =
      customInhale * 1000;
  }

  breathPhaseStarted =
    millis();

  touchActivity();
}

void stopBreathing() {

  breathingRunning =
    false;

  touchActivity();
}

void updateBreathing() {

  if (!breathingRunning) {
    return;
  }

  if (
    millis() -
    breathPhaseStarted <
    breathPhaseLength
  ) {
    return;
  }

  breathPhase++;

  if (breathPattern == BREATH_BOX) {

    if (breathPhase == 1) {

      breathPhaseName =
        "HOLD";

      breathPhaseLength =
        4000;
    }

    else if (breathPhase == 2) {

      breathPhaseName =
        "EXHALE";

      breathPhaseLength =
        4000;
    }

    else if (breathPhase == 3) {

      breathPhaseName =
        "HOLD";

      breathPhaseLength =
        4000;
    }

    else {

      breathPhase =
        0;

      breathPhaseName =
        "INHALE";

      breathPhaseLength =
        4000;
    }
  }

  else if (breathPattern == BREATH_478) {

    if (breathPhase == 1) {

      breathPhaseName =
        "HOLD";

      breathPhaseLength =
        7000;
    }

    else if (breathPhase == 2) {

      breathPhaseName =
        "EXHALE";

      breathPhaseLength =
        8000;
    }

    else {

      breathPhase =
        0;

      breathPhaseName =
        "INHALE";

      breathPhaseLength =
        4000;
    }
  }

  else if (breathPattern == BREATH_RELAX) {

    if (breathPhase == 1) {

      breathPhaseName =
        "EXHALE";

      breathPhaseLength =
        6000;
    }

    else {

      breathPhase =
        0;

      breathPhaseName =
        "INHALE";

      breathPhaseLength =
        6000;
    }
  }

  else {

    if (breathPhase == 1) {

      breathPhaseName =
        "HOLD";

      breathPhaseLength =
        customHold * 1000;
    }

    else if (breathPhase == 2) {

      breathPhaseName =
        "EXHALE";

      breathPhaseLength =
        customExhale * 1000;
    }

    else {

      breathPhase =
        0;

      breathPhaseName =
        "INHALE";

      breathPhaseLength =
        customInhale * 1000;
    }
  }

  breathPhaseStarted =
    millis();
}

// ============================================================
// PINGER
// ============================================================

long pingHost(String host) {

  WiFiClient client;

  unsigned long started =
    millis();

  if (
    client.connect(
      host.c_str(),
      80
    )
  ) {

    long result =
      millis() - started;

    client.stop();

    return result;
  }

  return -1;
}

void startPinger() {

  pingerRunning =
    true;

  pingerStarted =
    millis();

  lastPing =
    0;

  lastPingResult =
    -1;

  lastPingSuccess =
    false;

  touchActivity();
}

void stopPinger() {

  pingerRunning =
    false;

  touchActivity();
}

void updatePinger() {

  if (!pingerRunning) {
    return;
  }

  if (
    millis() -
    pingerStarted >=
    PINGER_MAX_TIME
  ) {

    pingerRunning =
      false;

    drawPinger();

    return;
  }

  if (
    lastPing != 0 &&
    millis() -
    lastPing <
    PINGER_INTERVAL
  ) {
    return;
  }

  lastPing =
    millis();

  lastPingResult =
    pingHost(
      pingPresets[pingIndex].ip
    );

  lastPingSuccess =
    lastPingResult >= 0;

  drawPinger();
}

// ============================================================
// AUTO SLEEP
// ============================================================

unsigned long getAutoSleepTime() {

  switch (autoSleepSetting) {

    case SLEEP_OFF:
      return 0;

    case SLEEP_3SEC:
      return 3000;

    case SLEEP_10SEC:
      return 10000;

    case SLEEP_1MIN:
      return 60000;

    case SLEEP_5MIN:
      return 300000;

    case SLEEP_7MIN:
      return 420000;
  }

  return 420000;
}

void processAutoSleep() {

  unsigned long idle =
    millis() -
    lastActivity;

  if (
    screenState ==
    STATE_CATEGORY_LIST ||
    screenState ==
    STATE_TOOL_LIST
  ) {

    if (
      idle >=
      LIST_SLEEP_TIME
    ) {

      enterSleep();
    }

    return;
  }

  if (screenState == STATE_TOOL) {

    if (
      currentTool ==
      TOOL_PINGER &&
      pingerRunning
    ) {
      return;
    }

    unsigned long timeout =
      getAutoSleepTime();

    if (
      timeout > 0 &&
      idle >= timeout
    ) {

      enterSleep();
    }
  }

  if (
    screenState ==
    STATE_HELP ||
    screenState ==
    STATE_SETTINGS ||
    screenState ==
    STATE_WIFI
  ) {

    if (
      idle >=
      LIST_SLEEP_TIME
    ) {

      enterSleep();
    }
  }
}

// ============================================================
// BUTTON NAVIGATION
// ============================================================

void handleSystemSelection() {

  switch (systemIndex) {

    case 0:
      enterSleep();
      break;

    case 1:
      powerOffDevice();
      break;

    case 2:
      screenState =
        STATE_WIFI;
      drawWiFi();
      break;

    case 3:
      screenState =
        STATE_SETTINGS;
      drawSettings();
      break;

    case 4:
      screenState =
        STATE_HELP;
      helpIndex = 0;
      drawHelp();
      break;
  }
}

void buttonPressed() {

  touchActivity();

  // Power off wake.
  if (powerOffState) {
    wakeFromPowerOff();
    return;
  }

  // Home.
  if (screenState == STATE_HOME) {

    screenState =
      STATE_CATEGORY_LIST;

    categoryIndex =
      0;

    drawCategoryList();

    return;
  }

  // Help.
  if (screenState == STATE_HELP) {

    helpIndex++;

    if (helpIndex > 5) {
      helpIndex = 0;
    }

    drawHelp();

    return;
  }

  // Settings.
  if (screenState == STATE_SETTINGS) {

    autoSleepSetting =
      (AutoSleepSetting)(
        ((int)autoSleepSetting + 1) %
        6
      );

    saveSettings();

    drawSettings();

    return;
  }

  // WiFi.
  if (screenState == STATE_WIFI) {

    if (wifiManualOff) {

      wifiManualOff =
        false;

      if (connectWiFi()) {

        setupTime();

        networkOfflineMode =
          false;

        MDNS.end();
        MDNS.begin("lime");
      }
    }

    else {

      wifiManualOff =
        true;

      disconnectWiFi();

      networkOfflineMode =
        false;
    }

    drawWiFi();

    return;
  }

  // System tool.
  if (
    screenState == STATE_TOOL &&
    currentTool == TOOL_SYSTEM
  ) {

    handleSystemSelection();

    return;
  }

  // Active tools.
  if (screenState == STATE_TOOL) {

    if (
      currentTool ==
      TOOL_STOPWATCH
    ) {

      if (stopwatchRunning) {
        stopStopwatch();
      } else {
        startStopwatch();
      }

      drawStopwatch();

      return;
    }

    if (
      currentTool ==
      TOOL_TIMER
    ) {

      if (
        timerState ==
        TIMER_RUNNING
      ) {
        stopTimer();
      } else {
        startTimer();
      }

      drawTimer();

      return;
    }

    if (
      currentTool ==
      TOOL_CLOCK
    ) {

      screenState =
        STATE_TOOL_LIST;

      drawToolList();

      return;
    }

    if (
      currentTool ==
      TOOL_DATE
    ) {

      screenState =
        STATE_TOOL_LIST;

      drawToolList();

      return;
    }

    if (
      currentTool ==
      TOOL_DICE
    ) {

      rollDice();
      drawDice();

      return;
    }

    if (
      currentTool ==
      TOOL_2DICE
    ) {

      roll2Dice();
      draw2Dice();

      return;
    }

    if (
      currentTool ==
      TOOL_COIN
    ) {

      flipCoin();
      drawCoin();

      return;
    }

    if (
      currentTool ==
      TOOL_NUMBER
    ) {

      generateNumber();
      drawNumber();

      return;
    }

    if (
      currentTool ==
      TOOL_YESNO
    ) {

      generateYesNo();
      drawYesNo();

      return;
    }

    if (
      currentTool ==
      TOOL_BREATHING
    ) {

      if (breathingRunning) {
        stopBreathing();
      } else {
        startBreathing();
      }

      drawBreathing();

      return;
    }

    if (
      currentTool ==
      TOOL_PINGER
    ) {

      if (pingerRunning) {

        stopPinger();

        screenState =
          STATE_TOOL_LIST;

        drawToolList();
      }

      else {

        startPinger();
        drawPinger();
      }

      return;
    }
  }

  // Category/tool lists.
  if (
    screenState ==
    STATE_CATEGORY_LIST ||
    screenState ==
    STATE_TOOL_LIST
  ) {

    if (!waitingForSecondPress) {

      waitingForSecondPress =
        true;

      firstPressTime =
        millis();

      return;
    }

    unsigned long now =
      millis();

    if (
      now -
      firstPressTime <=
      DOUBLE_PRESS_TIME
    ) {

      waitingForSecondPress =
        false;

      if (
        screenState ==
        STATE_CATEGORY_LIST
      ) {

        currentCategory =
          (Category)categoryIndex;

        toolIndex =
          0;

        screenState =
          STATE_TOOL_LIST;

        drawToolList();

        return;
      }

      Tool tools[10];
      int count;

      getCategoryTools(
        currentCategory,
        tools,
        count
      );

      if (
        toolIndex >= 0 &&
        toolIndex < count
      ) {

        currentTool =
          tools[toolIndex];

        screenState =
          STATE_TOOL;

        touchActivity();

        if (
          currentTool ==
          TOOL_DICE
        ) {
          rollDice();
        }

        else if (
          currentTool ==
          TOOL_2DICE
        ) {
          roll2Dice();
        }

        else if (
          currentTool ==
          TOOL_COIN
        ) {
          flipCoin();
        }

        else if (
          currentTool ==
          TOOL_NUMBER
        ) {
          generateNumber();
        }

        else if (
          currentTool ==
          TOOL_YESNO
        ) {
          generateYesNo();
        }

        drawCurrentScreen();
      }
    }
  }
}

// ============================================================
// SINGLE PRESS
// ============================================================

void processSinglePress() {

  if (!waitingForSecondPress) {
    return;
  }

  if (
    millis() -
    firstPressTime <
    DOUBLE_PRESS_TIME
  ) {
    return;
  }

  waitingForSecondPress =
    false;

  touchActivity();

  if (
    screenState ==
    STATE_CATEGORY_LIST
  ) {

    categoryIndex++;

    if (categoryIndex >= 5) {
      categoryIndex = 0;
    }

    drawCategoryList();
  }

  else if (
    screenState ==
    STATE_TOOL_LIST
  ) {

    Tool tools[10];
    int count;

    getCategoryTools(
      currentCategory,
      tools,
      count
    );

    toolIndex++;

    if (toolIndex >= count) {
      toolIndex = 0;
    }

    drawToolList();
  }
}

// ============================================================
// BUTTON PROCESSING
// ============================================================

void processButton() {

  bool reading =
    digitalRead(BUTTON_PIN);

  if (reading != buttonReading) {

    buttonReading =
      reading;

    buttonChangedAt =
      millis();
  }

  if (
    millis() -
    buttonChangedAt >=
    DEBOUNCE_TIME
  ) {

    if (
      buttonStable !=
      buttonReading
    ) {

      buttonStable =
        buttonReading;

      if (buttonStable == LOW) {

        buttonDownAt =
          millis();

        longPressHandled =
          false;
      }

      else {

        if (!longPressHandled) {
          buttonPressed();
        }
      }
    }
  }

  if (
    buttonStable == LOW &&
    !longPressHandled &&
    millis() -
    buttonDownAt >=
    LONG_PRESS_TIME
  ) {

    longPressHandled =
      true;

    waitingForSecondPress =
      false;

    touchActivity();

    enterSleep();
  }
}

// ============================================================
// PREFERENCES
// ============================================================

void saveSettings() {

  prefs.begin(
    "limetools",
    false
  );

  prefs.putInt(
    "autosleep",
    (int)autoSleepSetting
  );

  for (int i = 0; i < 5; i++) {

    prefs.putString(
      ("pn" + String(i)).c_str(),
      pingPresets[i].name
    );

    prefs.putString(
      ("pi" + String(i)).c_str(),
      pingPresets[i].ip
    );
  }

  prefs.end();
}

void loadSettings() {

  prefs.begin(
    "limetools",
    true
  );

  int storedSleep =
    prefs.getInt(
      "autosleep",
      SLEEP_7MIN
    );

  if (
    storedSleep < 0 ||
    storedSleep > 5
  ) {
    storedSleep =
      SLEEP_7MIN;
  }

  autoSleepSetting =
    (AutoSleepSetting)storedSleep;

  for (int i = 0; i < 5; i++) {

    pingPresets[i].name =
      prefs.getString(
        ("pn" + String(i)).c_str(),
        "Preset " + String(i + 1)
      );

    pingPresets[i].ip =
      prefs.getString(
        ("pi" + String(i)).c_str(),
        "192.168.0.1"
      );
  }

  prefs.end();
}

// ============================================================
// HTML
// ============================================================

String htmlPage() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>

<meta name="viewport"
      content="width=device-width,initial-scale=1">

<title>LimeTools</title>

<style>

* {
  box-sizing:border-box;
}

body {
  margin:0;
  font-family:Arial,sans-serif;
  background:
    radial-gradient(
      circle at top left,
      #123b32 0,
      #07100f 35%,
      #050708 100%
    );
  color:white;
  min-height:100vh;
}

header {
  padding:22px;
  display:flex;
  justify-content:space-between;
  align-items:center;
}

.logo {
  font-size:28px;
  font-weight:800;
  letter-spacing:1px;
}

.status {
  font-size:13px;
  color:#9fffcf;
}

main {
  max-width:900px;
  margin:auto;
  padding:10px 20px 40px;
}

h2 {
  margin-top:10px;
}

.grid {
  display:grid;
  grid-template-columns:
    repeat(
      auto-fit,
      minmax(160px,1fr)
    );
  gap:14px;
}

.card {
  border:1px solid #1d5145;
  border-radius:18px;
  padding:20px;
  background:
    linear-gradient(
      145deg,
      #10231f,
      #09100f
    );
  box-shadow:0 0 20px #0008;
  cursor:pointer;
  transition:.15s;
}

.card:hover {
  transform:translateY(-2px);
  border-color:#59ffbd;
  box-shadow:0 0 22px #59ffbd33;
}

.card h3 {
  margin:0 0 8px;
}

.card p {
  color:#9bb3ad;
  margin:0;
}

.panel {
  margin-top:20px;
  padding:24px;
  border-radius:20px;
  background:#09110fdd;
  border:1px solid #204c42;
}

.big {
  font-size:48px;
  text-align:center;
  margin:25px 0;
}

button,
select,
input {
  width:100%;
  padding:13px;
  margin:6px 0;
  border-radius:12px;
  border:1px solid #285d50;
  background:#0c1815;
  color:white;
  font-size:16px;
}

button {
  cursor:pointer;
}

button:hover {
  border-color:#63ffc2;
}

.nav {
  display:flex;
  gap:8px;
  margin-bottom:15px;
}

.nav button {
  flex:1;
}

.green {
  color:#63ffc2;
}

.red {
  color:#ff7070;
}

.blue {
  color:#70bfff;
}

</style>
</head>

<body>

<header>

<div class="logo">
LimeTools
</div>

<div class="status"
     id="status">
Connecting...
</div>

</header>

<main id="app"></main>

<script>

let state={};

async function getState(){

  try{

    let r=await fetch('/api/state');
    state=await r.json();

    render();

  }

  catch(e){

    document.getElementById(
      'status'
    ).innerText='Offline';
  }
}

async function action(a){

  await fetch(
    '/api/action?action='+
    encodeURIComponent(a)
  );

  getState();
}

function render(){

  document.getElementById(
    'status'
  ).innerText =
    state.wifi
    ? 'WiFi Connected'
    : 'WiFi Offline';

  let app =
    document.getElementById('app');

  if(state.screen==='home'){

    app.innerHTML=`
      <div class="panel">

        <h2>LimeLight</h2>

        <div class="big">
          ${state.time}
        </div>

        <button
          onclick="action('categories')">
          Open LimeTools
        </button>

      </div>`;

    return;
  }

  if(state.screen==='categories'){

    app.innerHTML=`

      <h2>Categories</h2>

      <div class="grid">

        <div class="card"
             onclick="action('category:0')">
          <h3>Core</h3>
          <p>
            Timers, stopwatch and clock
          </p>
        </div>

        <div class="card"
             onclick="action('category:1')">
          <h3>Random</h3>
          <p>
            Dice, coin and random tools
          </p>
        </div>

        <div class="card"
             onclick="action('category:2')">
          <h3>Breathing</h3>
          <p>
            Guided breathing exercises
          </p>
        </div>

        <div class="card"
             onclick="action('category:3')">
          <h3>Network</h3>
          <p>
            Network diagnostic tools
          </p>
        </div>

        <div class="card"
             onclick="action('category:4')">
          <h3>System</h3>
          <p>
            Device and WiFi controls
          </p>
        </div>

      </div>`;

    return;
  }

  if(state.screen==='tools'){

    let cards='';

    state.tools.forEach(
      (x,i)=>{

        cards+=`
        <div class="card"
             onclick="action('tool:${i}')">

          <h3>${x}</h3>

          <p>
            Open tool
          </p>

        </div>`;
      }
    );

    app.innerHTML=`

      <div class="nav">

        <button
          onclick="action('categories')">
          Back
        </button>

        <button
          onclick="action('home')">
          Home
        </button>

      </div>

      <h2>Tools</h2>

      <div class="grid">
        ${cards}
      </div>`;

    return;
  }

  if(state.screen==='tool'){

    let t=state.tool;

    if(t==='Stopwatch'){

      app.innerHTML=`

        <div class="panel">

          <h2>Stopwatch</h2>

          <div class="big">
            ${state.stopwatch}
          </div>

          <button
            onclick="action('stopwatch')">

            ${state.stopwatchRunning
              ? 'STOP'
              : 'START'}

          </button>

          <button
            onclick="action('stopwatchReset')">

            RESET

          </button>

        </div>`;
    }

    else if(t==='Timer'){

      app.innerHTML=`

        <div class="panel">

          <h2>Timer</h2>

          <div class="big">
            ${state.timer}
          </div>

          <button
            onclick="action('timer')">

            ${state.timerRunning
              ? 'STOP'
              : 'START'}

          </button>

          <button
            onclick="action('timerReset')">

            RESET

          </button>

          <h3>Custom Timer</h3>

          <input
            id="mins"
            type="number"
            min="0"
            placeholder="Minutes">

          <input
            id="secs"
            type="number"
            min="0"
            max="59"
            placeholder="Seconds">

          <button
            onclick="
              action(
                'settimer:'+
                document.getElementById('mins').value+
                ':'+
                document.getElementById('secs').value
              )">

            Set Custom Timer

          </button>

        </div>`;
    }

    else if(t==='Clock'){

      app.innerHTML=`

        <div class="panel">

          <h2>Clock</h2>

          <div class="big">
            ${state.time}
          </div>

        </div>`;
    }

    else if(t==='Date'){

      app.innerHTML=`

        <div class="panel">

          <h2>Date</h2>

          <div class="big">
            ${state.date}
          </div>

          <h2 style="text-align:center">
            ${state.day}
          </h2>

        </div>`;
    }

    else if(t==='Dice'){

      app.innerHTML=`

        <div class="panel">

          <h2>Dice</h2>

          <div class="big">
            ${state.dice}
          </div>

          <button
            onclick="action('dice')">
            ROLL
          </button>

        </div>`;
    }

    else if(t==='2 Dice'){

      app.innerHTML=`

        <div class="panel">

          <h2>2 Dice</h2>

          <div class="big">
            ${state.dice1}
            +
            ${state.dice2}
          </div>

          <button
            onclick="action('2dice')">
            ROLL
          </button>

        </div>`;
    }

    else if(t==='Coin'){

      app.innerHTML=`

        <div class="panel">

          <h2>Coin</h2>

          <div class="big">
            ${state.coin}
          </div>

          <button
            onclick="action('coin')">
            FLIP
          </button>

        </div>`;
    }

    else if(t==='Number'){

      app.innerHTML=`

        <div class="panel">

          <h2>Random Number</h2>

          <div class="big">
            ${state.number}
          </div>

          <input
            id="min"
            type="number"
            placeholder="Minimum">

          <input
            id="max"
            type="number"
            placeholder="Maximum">

          <button
            onclick="
              action(
                'number:'+
                document.getElementById('min').value+
                ':'+
                document.getElementById('max').value
              )">

            GENERATE

          </button>

        </div>`;
    }

    else if(t==='Yes / No'){

      app.innerHTML=`

        <div class="panel">

          <h2>Yes / No</h2>

          <div class="big">
            ${state.yesno}
          </div>

          <button
            onclick="action('yesno')">

            DECIDE

          </button>

        </div>`;
    }

    else if(t==='Breathing'){

      app.innerHTML=`

        <div class="panel">

          <h2>Breathing</h2>

          <div class="big">
            ${state.breathPhase}
          </div>

          <button
            onclick="action('breath:start')">
            START
          </button>

          <button
            onclick="action('breath:stop')">
            STOP
          </button>

          <button
            onclick="action('breath:box')">
            Box Breathing
          </button>

          <button
            onclick="action('breath:478')">
            4-7-8
          </button>

          <button
            onclick="action('breath:relax')">
            Relax
          </button>

        </div>`;
    }

    else if(t==='Pinger'){

      let presets='';

      state.pingers.forEach(
        (p,i)=>{

          presets+=`

          <button
            onclick="action('pingselect:${i}')">

            ${p.name}<br>
            ${p.ip}

          </button>`;
        }
      );

      app.innerHTML=`

        <div class="panel">

          <h2>Pinger</h2>

          <div class="big">
            ${state.pingResult}
          </div>

          <button
            onclick="action('pingstart')">
            START
          </button>

          <button
            onclick="action('pingstop')">
            STOP
          </button>

          <h3>Presets</h3>

          ${presets}

          <h3>Edit presets</h3>

          ${state.pingers.map(
            (p,i)=>`

            <input
              id="pn${i}"
              maxlength="20"
              value="${p.name}"
              placeholder="Name">

            <input
              id="pi${i}"
              value="${p.ip}"
              placeholder="IP address">

            <button
              onclick="
                action(
                  'pingedit:${i}:'+
                  encodeURIComponent(
                    document.getElementById(
                      'pn${i}'
                    ).value
                  )+
                  ':'+
                  document.getElementById(
                    'pi${i}'
                  ).value
                )">

              Save ${i+1}

            </button>
          `
          ).join('')}

        </div>`;
    }

    else if(t==='System'){

      app.innerHTML=`

        <div class="panel">

          <h2>System</h2>

          <button
            onclick="action('sleep')">
            SLEEP
          </button>

          <button
            onclick="action('poweroff')">
            SHUT DOWN
          </button>

          <button
            onclick="action('wifi')">
            WiFi
          </button>

          <button
            onclick="action('settings')">
            Settings
          </button>

          <button
            onclick="action('help')">
            Help
          </button>

        </div>`;
    }

    return;
  }

  if(state.screen==='settings'){

    app.innerHTML=`

      <div class="panel">

        <h2>Settings</h2>

        <h3>Auto Sleep</h3>

        <select
          onchange="
            action(
              'autosleep:'+
              this.value
            )
          ">

          <option value="0">
            Off
          </option>

          <option value="1">
            3 sec
          </option>

          <option value="2">
            10 sec
          </option>

          <option value="3">
            1 min
          </option>

          <option value="4">
            5 min
          </option>

          <option value="5">
            7 min
          </option>

        </select>

        <button
          onclick="action('home')">
          HOME
        </button>

      </div>`;

    return;
  }

  if(state.screen==='wifi'){

    app.innerHTML=`

      <div class="panel">

        <h2>WiFi</h2>

        <div class="big">

          ${
            state.wifi
            ? 'Connected'
            : 'OFF'
          }

        </div>

        <p>
          ${state.ip}
        </p>

        <button
          onclick="action('wifitoggle')">

          ${
            state.wifi
            ? 'Turn WiFi Off'
            : 'Turn WiFi On'
          }

        </button>

        <button
          onclick="action('home')">

          HOME

        </button>

      </div>`;
  }
}

setInterval(
  getState,
  500
);

getState();

</script>

</body>
</html>
)rawliteral";

  return html;
}

// ============================================================
// JSON ESCAPE
// ============================================================

String jsonEscape(String s) {

  s.replace(
    "\\",
    "\\\\"
  );

  s.replace(
    "\"",
    "\\\""
  );

  s.replace(
    "\n",
    "\\n"
  );

  return s;
}

// ============================================================
// SCREEN NAME
// ============================================================

String getScreenName() {

  if (
    screenState ==
    STATE_HOME
  ) {
    return "home";
  }

  if (
    screenState ==
    STATE_CATEGORY_LIST
  ) {
    return "categories";
  }

  if (
    screenState ==
    STATE_TOOL_LIST
  ) {
    return "tools";
  }

  if (
    screenState ==
    STATE_TOOL
  ) {
    return "tool";
  }

  if (
    screenState ==
    STATE_SETTINGS
  ) {
    return "settings";
  }

  if (
    screenState ==
    STATE_WIFI
  ) {
    return "wifi";
  }

  if (
    screenState ==
    STATE_HELP
  ) {
    return "help";
  }

  return "home";
}

// ============================================================
// WEB STOPWATCH
// ============================================================

String getStopwatchValue() {

  unsigned long value =
    stopwatchElapsed;

  if (stopwatchRunning) {

    value +=
      millis() -
      stopwatchStarted;
  }

  return formatStopwatch(value);
}

// ============================================================
// WEB TIMER
// ============================================================

String getTimerValue() {

  unsigned long remaining =
    timerRemaining;

  if (
    timerState ==
    TIMER_RUNNING
  ) {

    unsigned long elapsed =
      millis() -
      timerStartMillis;

    if (
      elapsed >=
      timerDuration
    ) {

      remaining = 0;
    }

    else {

      remaining =
        timerDuration -
        elapsed;
    }
  }

  return formatTime(remaining);
}

// ============================================================
// WEB STATE
// ============================================================

void handleState() {

  String json = "{";

  json +=
    "\"screen\":\"" +
    getScreenName() +
    "\",";

  json +=
    "\"wifi\":" +
    String(
      WiFi.status() ==
      WL_CONNECTED
      ? "true"
      : "false"
    ) +
    ",";

  json +=
    "\"ip\":\"" +
    jsonEscape(
      WiFi.localIP().toString()
    ) +
    "\",";

  json +=
    "\"time\":\"" +
    jsonEscape(
      getTimeString()
    ) +
    "\",";

  json +=
    "\"date\":\"" +
    jsonEscape(
      getDateString()
    ) +
    "\",";

  json +=
    "\"day\":\"" +
    jsonEscape(
      getDayString()
    ) +
    "\",";

  json +=
    "\"tool\":\"" +
    jsonEscape(
      getToolName(currentTool)
    ) +
    "\",";

  json +=
    "\"stopwatch\":\"" +
    getStopwatchValue() +
    "\",";

  json +=
    "\"stopwatchRunning\":" +
    String(
      stopwatchRunning
      ? "true"
      : "false"
    ) +
    ",";

  json +=
    "\"timer\":\"" +
    getTimerValue() +
    "\",";

  json +=
    "\"timerRunning\":" +
    String(
      timerState ==
      TIMER_RUNNING
      ? "true"
      : "false"
    ) +
    ",";

  json +=
    "\"dice\":" +
    String(dice1) +
    ",";

  json +=
    "\"dice1\":" +
    String(dice1) +
    ",";

  json +=
    "\"dice2\":" +
    String(dice2) +
    ",";

  json +=
    "\"coin\":\"" +
    jsonEscape(
      coinResult
    ) +
    "\",";

  json +=
    "\"number\":" +
    String(randomNumber) +
    ",";

  json +=
    "\"yesno\":\"" +
    String(
      randomNumber == 1
      ? "YES"
      : "NO"
    ) +
    "\",";

  json +=
    "\"breathPhase\":\"" +
    jsonEscape(
      breathPhaseName
    ) +
    "\",";

  json +=
    "\"pingResult\":\"" +
    String(
      lastPingSuccess
      ? String(lastPingResult) +
        " ms"
      : "No response"
    ) +
    "\",";

  json += "\"tools\":[";

  Tool tools[10];
  int count;

  getCategoryTools(
    currentCategory,
    tools,
    count
  );

  for (
    int i = 0;
    i < count;
    i++
  ) {

    if (i > 0) {
      json += ",";
    }

    json +=
      "\"" +
      jsonEscape(
        getToolName(tools[i])
      ) +
      "\"";
  }

  json += "],";

  json += "\"pingers\":[";

  for (
    int i = 0;
    i < 5;
    i++
  ) {

    if (i > 0) {
      json += ",";
    }

    json += "{";

    json +=
      "\"name\":\"" +
      jsonEscape(
        pingPresets[i].name
      ) +
      "\",";

    json +=
      "\"ip\":\"" +
      jsonEscape(
        pingPresets[i].ip
      ) +
      "\"";

    json += "}";
  }

  json += "]";

  json += "}";

  server.send(
    200,
    "application/json",
    json
  );
}

// ============================================================
// WEB ACTIONS
// ============================================================

void handleAction() {

  String a =
    server.arg("action");

  touchActivity();

  // ---------------- HOME ----------------

  if (a == "home") {

    enterSleep();
  }

  // ---------------- CATEGORIES ----------------

  else if (a == "categories") {

    screenState =
      STATE_CATEGORY_LIST;

    categoryIndex =
      0;

    drawCategoryList();
  }

  // ---------------- CATEGORY ----------------

  else if (
    a.startsWith("category:")
  ) {

    categoryIndex =
      a.substring(9).toInt();

    if (
      categoryIndex < 0 ||
      categoryIndex > 4
    ) {
      categoryIndex = 0;
    }

    currentCategory =
      (Category)categoryIndex;

    toolIndex =
      0;

    screenState =
      STATE_TOOL_LIST;

    drawToolList();
  }

  // ---------------- TOOL ----------------

  else if (
    a.startsWith("tool:")
  ) {

    int index =
      a.substring(5).toInt();

    Tool tools[10];
    int count;

    getCategoryTools(
      currentCategory,
      tools,
      count
    );

    if (
      index >= 0 &&
      index < count
    ) {

      toolIndex =
        index;

      currentTool =
        tools[index];

      screenState =
        STATE_TOOL;

      if (
        currentTool ==
        TOOL_DICE
      ) {
        rollDice();
      }

      else if (
        currentTool ==
        TOOL_2DICE
      ) {
        roll2Dice();
      }

      else if (
        currentTool ==
        TOOL_COIN
      ) {
        flipCoin();
      }

      else if (
        currentTool ==
        TOOL_NUMBER
      ) {
        generateNumber();
      }

      else if (
        currentTool ==
        TOOL_YESNO
      ) {
        generateYesNo();
      }

      drawCurrentScreen();
    }
  }

  // ---------------- STOPWATCH ----------------

  else if (a == "stopwatch") {

    if (stopwatchRunning) {
      stopStopwatch();
    } else {
      startStopwatch();
    }

    drawStopwatch();
  }

  else if (a == "stopwatchReset") {

    resetStopwatch();

    drawStopwatch();
  }

  // ---------------- TIMER ----------------

  else if (a == "timer") {

    if (
      timerState ==
      TIMER_RUNNING
    ) {
      stopTimer();
    } else {
      startTimer();
    }

    drawTimer();
  }

  else if (a == "timerReset") {

    resetTimer();

    drawTimer();
  }

  else if (
    a.startsWith("settimer:")
  ) {

    String value =
      a.substring(9);

    int separator =
      value.indexOf(':');

    if (separator > 0) {

      int mins =
        value.substring(
          0,
          separator
        ).toInt();

      int secs =
        value.substring(
          separator + 1
        ).toInt();

      if (mins < 0) {
        mins = 0;
      }

      if (secs < 0) {
        secs = 0;
      }

      if (secs > 59) {
        secs = 59;
      }

      timerDuration =
        ((unsigned long)mins *
         60000UL) +
        ((unsigned long)secs *
         1000UL);

      if (timerDuration == 0) {
        timerDuration = 1000;
      }

      timerRemaining =
        timerDuration;

      timerState =
        TIMER_STOPPED;

      drawTimer();
    }
  }

  // ---------------- RANDOM ----------------

  else if (a == "dice") {

    rollDice();
    drawDice();
  }

  else if (a == "2dice") {

    roll2Dice();
    draw2Dice();
  }

  else if (a == "coin") {

    flipCoin();
    drawCoin();
  }

  else if (a == "yesno") {

    generateYesNo();
    drawYesNo();
  }

  else if (
    a.startsWith("number:")
  ) {

    String value =
      a.substring(7);

    int separator =
      value.indexOf(':');

    if (separator > 0) {

      randomMin =
        value.substring(
          0,
          separator
        ).toInt();

      randomMax =
        value.substring(
          separator + 1
        ).toInt();

      if (
        randomMax <
        randomMin
      ) {

        int temp =
          randomMin;

        randomMin =
          randomMax;

        randomMax =
          temp;
      }

      generateNumber();

      drawNumber();
    }
  }

  // ---------------- BREATHING ----------------

  else if (
    a == "breath:start"
  ) {

    startBreathing();

    drawBreathing();
  }

  else if (
    a == "breath:stop"
  ) {

    stopBreathing();

    drawBreathing();
  }

  else if (
    a == "breath:box"
  ) {

    setBreathingPattern(
      BREATH_BOX
    );

    drawBreathing();
  }

  else if (
    a == "breath:478"
  ) {

    setBreathingPattern(
      BREATH_478
    );

    drawBreathing();
  }

  else if (
    a == "breath:relax"
  ) {

    setBreathingPattern(
      BREATH_RELAX
    );

    drawBreathing();
  }

  // ---------------- PINGER ----------------

  else if (
    a == "pingstart"
  ) {

    startPinger();

    drawPinger();
  }

  else if (
    a == "pingstop"
  ) {

    stopPinger();

    drawPinger();
  }

  else if (
    a.startsWith("pingselect:")
  ) {

    pingIndex =
      a.substring(11).toInt();

    if (pingIndex < 0) {
      pingIndex = 0;
    }

    if (pingIndex > 4) {
      pingIndex = 4;
    }

    if (
      screenState !=
      STATE_TOOL
    ) {
      currentTool =
        TOOL_PINGER;

      screenState =
        STATE_TOOL;
    }

    drawPinger();
  }

  // ---------------- PINGER EDIT ----------------

  else if (
    a.startsWith("pingedit:")
  ) {

    int p1 =
      a.indexOf(':');

    int p2 =
      a.indexOf(
        ':',
        p1 + 1
      );

    int p3 =
      a.indexOf(
        ':',
        p2 + 1
      );

    if (
      p1 >= 0 &&
      p2 >= 0 &&
      p3 >= 0
    ) {

      int index =
        a.substring(
          p1 + 1,
          p2
        ).toInt();

      String name =
        a.substring(
          p2 + 1,
          p3
        );

      String ip =
        a.substring(
          p3 + 1
        );

      name.replace(
        "%20",
        " "
      );

      if (name.length() > 20) {
        name =
          name.substring(
            0,
            20
          );
      }

      if (
        index >= 0 &&
        index < 5
      ) {

        pingPresets[index].name =
          name;

        pingPresets[index].ip =
          ip;

        saveSettings();
      }
    }
  }

  // ---------------- SYSTEM ----------------

  else if (a == "system") {

    currentTool =
      TOOL_SYSTEM;

    screenState =
      STATE_TOOL;

    drawSystem();
  }

  else if (a == "sleep") {

    enterSleep();
  }

  else if (a == "poweroff") {

    powerOffDevice();
  }

  else if (a == "wifi") {

    screenState =
      STATE_WIFI;

    drawWiFi();
  }

  // ---------------- WIFI TOGGLE ----------------

  else if (
    a == "wifitoggle"
  ) {

    if (
      WiFi.status() ==
      WL_CONNECTED
    ) {

      wifiManualOff =
        true;

      disconnectWiFi();

      networkOfflineMode =
        false;
    }

    else {

      wifiManualOff =
        false;

      if (connectWiFi()) {

        setupTime();

        networkOfflineMode =
          false;

        MDNS.end();
        MDNS.begin("lime");
      }
    }

    drawWiFi();
  }

  // ---------------- SETTINGS ----------------

  else if (
    a == "settings"
  ) {

    screenState =
      STATE_SETTINGS;

    drawSettings();
  }

  // ---------------- HELP ----------------

  else if (
    a == "help"
  ) {

    screenState =
      STATE_HELP;

    helpIndex =
      0;

    drawHelp();
  }

  // ---------------- AUTO SLEEP ----------------

  else if (
    a.startsWith("autosleep:")
  ) {

    int value =
      a.substring(10).toInt();

    if (
      value < 0 ||
      value > 5
    ) {
      value = 5;
    }

    autoSleepSetting =
      (AutoSleepSetting)value;

    saveSettings();

    screenState =
      STATE_SETTINGS;

    drawSettings();
  }

  server.send(
    200,
    "text/plain",
    "OK"
  );
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  randomSeed(
    micros()
  );

  pinMode(
    BUTTON_PIN,
    INPUT_PULLUP
  );

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDRESS
    )
  ) {

    while (true) {
      delay(100);
    }
  }

  loadSettings();

  // ----------------------------------------------------------
  // BOOT SCREEN
  // ----------------------------------------------------------

  unsigned long bootStarted =
    millis();

  unsigned long spinnerAt =
    0;

  int spinner =
    0;

  bool wifiConnected =
    false;

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  while (
    millis() -
    bootStarted <
    BOOT_MINIMUM_TIME
  ) {

    if (
      millis() -
      spinnerAt >=
      150
    ) {

      spinnerAt =
        millis();

      const char spinnerChars[] =
        "|/-\\";

      oledClear();

      display.setTextSize(2);
      display.setCursor(0, 8);
      display.println("LimeLight");

      display.setTextSize(2);
      display.setCursor(58, 35);

      display.print(
        spinnerChars[
          spinner++ % 4
        ]
      );

      display.display();
    }

    if (
      WiFi.status() ==
      WL_CONNECTED
    ) {
      wifiConnected =
        true;
    }

    delay(5);
  }

  // ----------------------------------------------------------
  // WIFI CONTINUE TO 10 SECOND LIMIT
  // ----------------------------------------------------------

  while (
    !wifiConnected &&
    millis() -
    bootStarted <
    WIFI_CONNECT_TIMEOUT
  ) {

    if (
      WiFi.status() ==
      WL_CONNECTED
    ) {

      wifiConnected =
        true;

      break;
    }

    delay(20);
  }

  // ----------------------------------------------------------
  // WIFI FAILURE
  // ----------------------------------------------------------

  if (!wifiConnected) {

    display.clearDisplay();

    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("LimeLight");

    display.setCursor(0, 20);
    display.println("Failed To");

    display.setCursor(0, 32);
    display.println("Connect To");

    display.setCursor(0, 44);
    display.println("WIFI");

    display.display();

    while (true) {
      delay(100);
    }
  }

  // ----------------------------------------------------------
  // WIFI SUCCESS
  // ----------------------------------------------------------

  wifiWasConnected =
    true;

  setupTime();

  MDNS.begin("lime");

  // ----------------------------------------------------------
  // WEB SERVER
  // ----------------------------------------------------------

  server.on(
    "/",
    []() {

      server.send(
        200,
        "text/html",
        htmlPage()
      );
    }
  );

  server.on(
    "/api/state",
    handleState
  );

  server.on(
    "/api/action",
    handleAction
  );

  server.begin();

  // ----------------------------------------------------------
  // BOOT BUTTON SAFETY
  // ----------------------------------------------------------

  while (
    digitalRead(BUTTON_PIN) ==
    LOW
  ) {
    delay(10);
  }

  buttonReading =
    HIGH;

  buttonStable =
    HIGH;

  buttonChangedAt =
    millis();

  // ----------------------------------------------------------
  // CONNECTED SCREEN
  // ----------------------------------------------------------

  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("LimeLight");

  display.setCursor(0, 18);
  display.println("WiFi Connected");

  display.setCursor(0, 32);
  display.println(
    WiFi.localIP().toString()
  );

  display.setCursor(0, 48);
  display.println("lime.local");

  display.display();

  delay(2000);

  // ----------------------------------------------------------
  // HOME
  // ----------------------------------------------------------

  screenState =
    STATE_HOME;

  touchActivity();

  drawHome();

  Serial.println();
  Serial.println("LimeTools ready");

  Serial.print("IP: ");
  Serial.println(
    WiFi.localIP()
  );

  Serial.println(
    "http://lime.local"
  );
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  // Web.
  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {
    server.handleClient();
  }

  // Button.
  processButton();

  processSinglePress();

  // Tools.
  updateBreathing();

  updatePinger();

  // Network.
  monitorWiFi();

  // ----------------------------------------------------------
  // TIMER FINISH
  // ----------------------------------------------------------

  if (
    timerState ==
    TIMER_RUNNING
  ) {

    if (
      millis() -
      timerStartMillis >=
      timerDuration
    ) {

      timerRemaining =
        0;

      timerState =
        TIMER_FINISHED;

      if (
        screenState ==
        STATE_TOOL &&
        currentTool ==
        TOOL_TIMER
      ) {

        drawTimer();
      }
    }
  }

  // ----------------------------------------------------------
  // PERIODIC OLED UPDATE
  // ----------------------------------------------------------

  static unsigned long lastDraw =
    0;

  if (
    millis() -
    lastDraw >=
    250
  ) {

    lastDraw =
      millis();

    if (
      screenState ==
      STATE_TOOL
    ) {

      if (
        currentTool ==
        TOOL_STOPWATCH ||
        currentTool ==
        TOOL_TIMER ||
        currentTool ==
        TOOL_CLOCK ||
        currentTool ==
        TOOL_DATE ||
        currentTool ==
        TOOL_BREATHING ||
        currentTool ==
        TOOL_PINGER
      ) {

        drawCurrentScreen();
      }
    }

    else if (
      screenState ==
      STATE_HOME
    ) {

      drawHome();
    }
  }

  // ----------------------------------------------------------
  // NTP CHECK
  // ----------------------------------------------------------

  if (
    millis() -
    lastNtpCheck >=
    60000
  ) {

    lastNtpCheck =
      millis();

    if (
      WiFi.status() ==
      WL_CONNECTED
    ) {

      struct tm timeinfo;

      if (
        !getLocalTime(
          &timeinfo,
          100
        )
      ) {

        configTzTime(
          TIMEZONE,
          NTP_SERVER
        );
      }
    }
  }

  // ----------------------------------------------------------
  // AUTO SLEEP
  // ----------------------------------------------------------

  processAutoSleep();

  // ----------------------------------------------------------
  // GLOBAL POWER OFF
  // ----------------------------------------------------------

  if (
    !powerOffState &&
    millis() -
    lastActivity >=
    POWER_OFF_TIME
  ) {

    powerOffDevice();
  }

  delay(5);
}
