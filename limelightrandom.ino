/*
  ============================================================
  LIMELIGHT RANDOM
  ============================================================

  ESP32 DevKit
  SSD1306 128x64

  OLED:
    VCC -> 3V3
    GND -> GND
    SDA -> GPIO 21
    SCL -> GPIO 22

  BUTTON:
    GPIO 0 -> onboard BOOT button

  WIFI:
    CAPCOMLow
    WowzerMowzer1!

  NTP:
    192.168.0.152

  MDNS:
    http://lime.local

  ============================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID = "CAPCOMLow";
const char* WIFI_PASSWORD = "WowzerMowzer1!";

// ============================================================
// NTP
// ============================================================

const char* NTP_SERVER = "192.168.0.152";

// UK timezone:
// GMT in winter
// BST in summer
const char* TIMEZONE = "GMT0BST,M3.5.0/1,M10.5.0/2";

// ============================================================
// OLED
// ============================================================

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

// ============================================================
// BUTTON
// ============================================================

#define BUTTON_PIN 0

const unsigned long BUTTON_DEBOUNCE = 50;

// ============================================================
// TIMINGS
// ============================================================

const unsigned long BOOT_MINIMUM = 3000;
const unsigned long WIFI_TIMEOUT = 10000;

const unsigned long DOUBLE_PRESS_TIME = 2000;

const unsigned long MENU_TIMEOUT = 30000;

const unsigned long RESULT_TIME = 3000;

// ============================================================
// WEB
// ============================================================

WebServer server(80);

// ============================================================
// DEVICE STATES
// ============================================================

enum DeviceState {
  STATE_SLEEP,
  STATE_MENU,
  STATE_RESULT
};

DeviceState deviceState = STATE_SLEEP;

// ============================================================
// TOOLS
// ============================================================

enum ToolType {
  TOOL_DICE,
  TOOL_COIN,
  TOOL_NUMBER
};

ToolType selectedTool = TOOL_DICE;

// ============================================================
// RESULTS
// ============================================================

int diceResult = 1;
bool coinHeads = true;
int numberResult = 1;

// ============================================================
// BUTTON STATE
// ============================================================

bool buttonReading = HIGH;
bool buttonStableState = HIGH;

unsigned long buttonLastChange = 0;

// Used so GPIO 0 being held while booting
// doesn't immediately activate the device.
bool buttonReady = false;

// ============================================================
// DOUBLE PRESS
// ============================================================

bool waitingForSecondPress = false;
unsigned long firstPressTime = 0;

// ============================================================
// ACTIVITY
// ============================================================

unsigned long lastActivity = 0;

// ============================================================
// RESULT
// ============================================================

unsigned long resultStarted = 0;

// ============================================================
// BOOT SPINNER
// ============================================================

int spinnerFrame = 0;

unsigned long lastSpinnerUpdate = 0;

const char* spinnerFrames[] = {
  "|",
  "/",
  "-",
  "\\"
};

// ============================================================
// NTP STATUS
// ============================================================

bool timeValid = false;

// ============================================================
// GET TIME
// ============================================================

String getTimeString() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 100)) {
    return "--:--";
  }

  // Make sure the year is actually valid.
  if (timeinfo.tm_year < 120) {
    return "--:--";
  }

  char buffer[8];

  strftime(
    buffer,
    sizeof(buffer),
    "%H:%M",
    &timeinfo
  );

  return String(buffer);
}

// ============================================================
// CHECK NTP
// ============================================================

bool checkTimeValid() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 100)) {
    return false;
  }

  return timeinfo.tm_year >= 120;
}

// ============================================================
// CENTRED TEXT
// ============================================================

void drawCentered(
  const String& text,
  int y,
  int size
) {

  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1;
  int16_t y1;

  uint16_t w;
  uint16_t h;

  display.getTextBounds(
    text,
    0,
    y,
    &x1,
    &y1,
    &w,
    &h
  );

  int x = (SCREEN_WIDTH - w) / 2;

  display.setCursor(x, y);
  display.print(text);
}

// ============================================================
// BOOT SCREEN
// ============================================================

void drawBootScreen() {

  display.clearDisplay();

  drawCentered(
    "LimeLight",
    15,
    2
  );

  drawCentered(
    spinnerFrames[spinnerFrame],
    42,
    2
  );

  display.display();
}

// ============================================================
// SLEEP SCREEN
// ============================================================

void drawSleepScreen() {

  display.clearDisplay();

  drawCentered(
    "LimeLight",
    15,
    2
  );

  drawCentered(
    getTimeString(),
    42,
    2
  );

  display.display();
}

// ============================================================
// MENU
// ============================================================

void drawMenuScreen() {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(8, 4);
  display.print("LimeLight Random");

  const char* names[] = {
    "Dice",
    "Coin",
    "Number"
  };

  for (int i = 0; i < 3; i++) {

    int y = 20 + (i * 13);

    display.setCursor(8, y);

    if (i == selectedTool) {
      display.print(">");
    }
    else {
      display.print(" ");
    }

    display.setCursor(20, y);
    display.print(names[i]);
  }

  display.display();
}

// ============================================================
// DICE DOT
// ============================================================

void drawDiceDot(
  int x,
  int y
) {

  display.fillCircle(
    x,
    y,
    3,
    SSD1306_WHITE
  );
}

// ============================================================
// DRAW DICE
// ============================================================

void drawDice(
  int value
) {

  int left = 39;
  int top = 8;

  int width = 50;
  int height = 50;

  display.drawRoundRect(
    left,
    top,
    width,
    height,
    6,
    SSD1306_WHITE
  );

  int leftX = 51;
  int centreX = 64;
  int rightX = 77;

  int topY = 20;
  int centreY = 33;
  int bottomY = 46;

  switch (value) {

    case 1:

      drawDiceDot(
        centreX,
        centreY
      );

      break;

    case 2:

      drawDiceDot(
        leftX,
        topY
      );

      drawDiceDot(
        rightX,
        bottomY
      );

      break;

    case 3:

      drawDiceDot(
        leftX,
        topY
      );

      drawDiceDot(
        centreX,
        centreY
      );

      drawDiceDot(
        rightX,
        bottomY
      );

      break;

    case 4:

      drawDiceDot(
        leftX,
        topY
      );

      drawDiceDot(
        rightX,
        topY
      );

      drawDiceDot(
        leftX,
        bottomY
      );

      drawDiceDot(
        rightX,
        bottomY
      );

      break;

    case 5:

      drawDiceDot(
        leftX,
        topY
      );

      drawDiceDot(
        rightX,
        topY
      );

      drawDiceDot(
        centreX,
        centreY
      );

      drawDiceDot(
        leftX,
        bottomY
      );

      drawDiceDot(
        rightX,
        bottomY
      );

      break;

    case 6:

      drawDiceDot(
        leftX,
        topY
      );

      drawDiceDot(
        leftX,
        centreY
      );

      drawDiceDot(
        leftX,
        bottomY
      );

      drawDiceDot(
        rightX,
        topY
      );

      drawDiceDot(
        rightX,
        centreY
      );

      drawDiceDot(
        rightX,
        bottomY
      );

      break;
  }
}

// ============================================================
// RESULT SCREEN
// ============================================================

void drawResultScreen() {

  display.clearDisplay();

  if (selectedTool == TOOL_DICE) {

    drawDice(
      diceResult
    );
  }

  else if (selectedTool == TOOL_COIN) {

    drawCentered(
      coinHeads ? "HEADS" : "TAILS",
      25,
      2
    );
  }

  else {

    drawCentered(
      String(numberResult),
      20,
      3
    );
  }

  display.display();
}

// ============================================================
// ACTIVATE TOOL
// ============================================================

void activateTool() {

  if (selectedTool == TOOL_DICE) {

    diceResult = random(
      1,
      7
    );
  }

  else if (selectedTool == TOOL_COIN) {

    coinHeads = random(
      0,
      2
    );
  }

  else {

    numberResult = random(
      1,
      101
    );
  }

  deviceState = STATE_RESULT;

  resultStarted = millis();

  lastActivity = millis();

  drawResultScreen();
}

// ============================================================
// SLEEP
// ============================================================

void enterSleep() {

  deviceState = STATE_SLEEP;

  waitingForSecondPress = false;

  drawSleepScreen();
}

// ============================================================
// WAKE
// ============================================================

void wakeDevice() {

  deviceState = STATE_MENU;

  selectedTool = TOOL_DICE;

  waitingForSecondPress = false;

  lastActivity = millis();

  drawMenuScreen();
}

// ============================================================
// BUTTON PRESS EVENT
// ============================================================

void buttonPressed() {

  unsigned long now = millis();

  // ----------------------------------------------------------
  // SLEEP
  // ----------------------------------------------------------

  if (deviceState == STATE_SLEEP) {

    wakeDevice();

    return;
  }

  // ----------------------------------------------------------
  // RESULT
  // ----------------------------------------------------------

  if (deviceState == STATE_RESULT) {

    return;
  }

  // ----------------------------------------------------------
  // MENU
  // ----------------------------------------------------------

  if (deviceState == STATE_MENU) {

    lastActivity = now;

    if (waitingForSecondPress) {

      if (
        now - firstPressTime <=
        DOUBLE_PRESS_TIME
      ) {

        waitingForSecondPress = false;

        activateTool();

        return;
      }

      waitingForSecondPress = false;
    }

    waitingForSecondPress = true;

    firstPressTime = now;
  }
}

// ============================================================
// BUTTON HANDLER
// ============================================================

void processButton() {

  bool reading = digitalRead(
    BUTTON_PIN
  );

  // Detect any physical change.
  if (reading != buttonReading) {

    buttonReading = reading;

    buttonLastChange = millis();
  }

  // Wait until the signal has remained stable.
  if (
    millis() - buttonLastChange >=
    BUTTON_DEBOUNCE
  ) {

    if (
      buttonStableState !=
      buttonReading
    ) {

      buttonStableState =
        buttonReading;

      // Button is active LOW.
      if (
        buttonStableState == LOW &&
        buttonReady
      ) {

        buttonPressed();
      }
    }
  }

  // Once the button is released after boot,
  // the button becomes usable.
  if (
    !buttonReady &&
    buttonStableState == HIGH
  ) {

    buttonReady = true;
  }
}

// ============================================================
// SINGLE PRESS PROCESSING
// ============================================================

void processPendingSinglePress() {

  if (!waitingForSecondPress) {
    return;
  }

  if (
    millis() - firstPressTime >=
    DOUBLE_PRESS_TIME
  ) {

    waitingForSecondPress = false;

    if (deviceState == STATE_MENU) {

      if (selectedTool == TOOL_DICE) {

        selectedTool = TOOL_COIN;
      }

      else if (selectedTool == TOOL_COIN) {

        selectedTool = TOOL_NUMBER;
      }

      else {

        selectedTool = TOOL_DICE;
      }

      lastActivity = millis();

      drawMenuScreen();
    }
  }
}

// ============================================================
// WIFI
// ============================================================

bool connectWiFi() {

  WiFi.mode(WIFI_STA);

  WiFi.setAutoReconnect(true);

  WiFi.persistent(false);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long started =
    millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - started < WIFI_TIMEOUT
  ) {

    if (
      millis() -
      lastSpinnerUpdate >=
      200
    ) {

      lastSpinnerUpdate =
        millis();

      spinnerFrame++;

      if (spinnerFrame >= 4) {
        spinnerFrame = 0;
      }

      drawBootScreen();
    }

    delay(10);
  }

  return (
    WiFi.status() ==
    WL_CONNECTED
  );
}

// ============================================================
// WIFI FAILURE
// ============================================================

void showWiFiFailure() {

  display.clearDisplay();

  drawCentered(
    "LimeLight",
    5,
    1
  );

  drawCentered(
    "Failed To",
    21,
    1
  );

  drawCentered(
    "Connect To",
    34,
    1
  );

  drawCentered(
    "WIFI",
    47,
    1
  );

  display.display();

  while (true) {
    delay(1000);
  }
}

// ============================================================
// NTP
// ============================================================

void setupTime() {

  // Clear any previous time state.
  timeValid = false;

  /*
    configTzTime handles:
      - NTP server
      - timezone
      - daylight saving
  */

  configTzTime(
    TIMEZONE,
    NTP_SERVER
  );

  Serial.println();
  Serial.println("NTP server:");
  Serial.println(NTP_SERVER);

  Serial.println("Waiting for NTP...");

  unsigned long started =
    millis();

  struct tm timeinfo;

  while (
    millis() - started < 8000
  ) {

    if (
      getLocalTime(
        &timeinfo,
        250
      )
    ) {

      if (
        timeinfo.tm_year >= 120
      ) {

        timeValid = true;

        Serial.print(
          "NTP time: "
        );

        Serial.println(
          getTimeString()
        );

        return;
      }
    }

    delay(100);
  }

  Serial.println(
    "NTP sync timed out"
  );

  timeValid = false;
}

// ============================================================
// WEB PAGE
// ============================================================

String generateWebPage() {

  String html;

  html += "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";

  html +=
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1'>";

  html +=
    "<meta http-equiv='refresh' content='1'>";

  html +=
    "<title>LimeLight</title>";

  html += "<style>";

  html +=
    "body{"
    "background:#000;"
    "color:#fff;"
    "font-family:Arial;"
    "text-align:center;"
    "padding-top:40px;"
    "}";

  html +=
    ".title{"
    "font-size:32px;"
    "font-weight:bold;"
    "}";

  html +=
    ".time{"
    "font-size:28px;"
    "margin-top:20px;"
    "}";

  html +=
    ".menu{"
    "font-size:26px;"
    "line-height:1.8;"
    "margin-top:25px;"
    "}";

  html +=
    ".output{"
    "font-size:40px;"
    "font-weight:bold;"
    "margin-top:30px;"
    "}";

  html += "</style>";

  html += "</head>";
  html += "<body>";

  // ----------------------------------------------------------
  // SLEEP
  // ----------------------------------------------------------

  if (
    deviceState == STATE_SLEEP
  ) {

    html +=
      "<div class='title'>"
      "LimeLight"
      "</div>";

    html +=
      "<div class='time'>";

    html += getTimeString();

    html +=
      "</div>";
  }

  // ----------------------------------------------------------
  // MENU
  // ----------------------------------------------------------

  else if (
    deviceState == STATE_MENU
  ) {

    html +=
      "<div class='title'>"
      "LimeLight Random"
      "</div>";

    html +=
      "<div class='menu'>";

    if (
      selectedTool == TOOL_DICE
    )
      html += "&gt; Dice<br>";
    else
      html += "Dice<br>";

    if (
      selectedTool == TOOL_COIN
    )
      html += "&gt; Coin<br>";
    else
      html += "Coin<br>";

    if (
      selectedTool == TOOL_NUMBER
    )
      html += "&gt; Number<br>";
    else
      html += "Number<br>";

    html +=
      "</div>";
  }

  // ----------------------------------------------------------
  // RESULT
  // ----------------------------------------------------------

  else {

    html +=
      "<div class='title'>"
      "LimeLight Random"
      "</div>";

    html +=
      "<div class='output'>";

    if (
      selectedTool == TOOL_DICE
    ) {

      html +=
        "Dice<br><br>";

      html +=
        "<div style='font-size:24px'>";

      switch (diceResult) {

        case 1:
          html += "       <br>";
          html += "    o  <br>";
          html += "       <br>";
          break;

        case 2:
          html += "o      <br>";
          html += "       <br>";
          html += "      o<br>";
          break;

        case 3:
          html += "o      <br>";
          html += "   o   <br>";
          html += "      o<br>";
          break;

        case 4:
          html += "o   o  <br>";
          html += "       <br>";
          html += "o   o  <br>";
          break;

        case 5:
          html += "o   o  <br>";
          html += "   o   <br>";
          html += "o   o  <br>";
          break;

        case 6:
          html += "o   o  <br>";
          html += "o   o  <br>";
          html += "o   o  <br>";
          break;
      }

      html +=
        "</div>";
    }

    else if (
      selectedTool == TOOL_COIN
    ) {

      html +=
        coinHeads
        ? "HEADS"
        : "TAILS";
    }

    else {

      html +=
        String(numberResult);
    }

    html +=
      "</div>";
  }

  html += "</body>";
  html += "</html>";

  return html;
}

// ============================================================
// WEB ROOT
// ============================================================

void handleRoot() {

  server.send(
    200,
    "text/html",
    generateWebPage()
  );
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(
    115200
  );

  delay(100);

  // ==========================================================
  // BUTTON
  // ==========================================================

  pinMode(
    BUTTON_PIN,
    INPUT_PULLUP
  );

  // Read initial state.
  buttonReading =
    digitalRead(BUTTON_PIN);

  buttonStableState =
    buttonReading;

  buttonLastChange =
    millis();

  /*
    buttonReady remains false until
    GPIO 0 has been released.
  */

  // ==========================================================
  // OLED
  // ==========================================================

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
      delay(1000);
    }
  }

  display.clearDisplay();
  display.display();

  // ==========================================================
  // RANDOM
  // ==========================================================

  randomSeed(
    micros()
  );

  // ==========================================================
  // BOOT
  // ==========================================================

  unsigned long bootStarted =
    millis();

  drawBootScreen();

  // ==========================================================
  // WIFI
  // ==========================================================

  bool wifiConnected =
    connectWiFi();

  // ==========================================================
  // GUARANTEE 3 SECOND BOOT
  // ==========================================================

  while (
    millis() - bootStarted <
    BOOT_MINIMUM
  ) {

    if (
      millis() -
      lastSpinnerUpdate >=
      200
    ) {

      lastSpinnerUpdate =
        millis();

      spinnerFrame++;

      if (spinnerFrame >= 4) {
        spinnerFrame = 0;
      }

      drawBootScreen();
    }

    delay(10);
  }

  // ==========================================================
  // WIFI FAILED
  // ==========================================================

  if (!wifiConnected) {

    showWiFiFailure();
  }

  // ==========================================================
  // NTP
  // ==========================================================

  setupTime();

  // ==========================================================
  // MDNS
  // ==========================================================

  if (
    MDNS.begin("lime")
  ) {

    Serial.println(
      "mDNS available at:"
    );

    Serial.println(
      "http://lime.local"
    );
  }

  // ==========================================================
  // WEB SERVER
  // ==========================================================

  server.on(
    "/",
    handleRoot
  );

  server.begin();

  Serial.println(
    "LimeLight Random ready"
  );

  Serial.print(
    "IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  // ==========================================================
  // WAIT FOR BUTTON RELEASE
  // ==========================================================

  /*
    This is important because GPIO 0 is the ESP32 BOOT button.

    If the button was held while the ESP32 reset,
    we don't want that same press to immediately
    activate LimeLight.
  */

  while (
    digitalRead(BUTTON_PIN) == LOW
  ) {

    delay(10);
  }

  buttonReading = HIGH;
  buttonStableState = HIGH;

  buttonLastChange = millis();

  buttonReady = true;

  // ==========================================================
  // START SLEEP SCREEN
  // ==========================================================

  enterSleep();
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  // ==========================================================
  // WEB
  // ==========================================================

  server.handleClient();

  // ==========================================================
  // BUTTON
  // ==========================================================

  processButton();

  processPendingSinglePress();

  // ==========================================================
  // MENU TIMEOUT
  // ==========================================================

  if (
    deviceState == STATE_MENU
  ) {

    if (
      millis() - lastActivity >=
      MENU_TIMEOUT
    ) {

      enterSleep();
    }
  }

  // ==========================================================
  // RESULT TIMEOUT
  // ==========================================================

  if (
    deviceState == STATE_RESULT
  ) {

    if (
      millis() - resultStarted >=
      RESULT_TIME
    ) {

      enterSleep();
    }
  }

  // ==========================================================
  // CLOCK UPDATE
  // ==========================================================

  static unsigned long lastClockUpdate =
    0;

  if (
    deviceState == STATE_SLEEP &&
    millis() - lastClockUpdate >= 1000
  ) {

    lastClockUpdate =
      millis();

    drawSleepScreen();
  }

  // ==========================================================
  // PERIODIC NTP CHECK
  // ==========================================================

  static unsigned long lastNtpCheck =
    0;

  if (
    millis() - lastNtpCheck >=
    60000
  ) {

    lastNtpCheck =
      millis();

    timeValid =
      checkTimeValid();

    if (!timeValid) {

      Serial.println(
        "NTP time invalid, requesting resync"
      );

      configTzTime(
        TIMEZONE,
        NTP_SERVER
      );
    }
  }

  delay(5);
}
