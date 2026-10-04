#include <Arduino.h>
#include <ArduinoJson.h>
#include <AyresWiFiManager.h>
#include <LittleFS.h>
#include <VFD_Driver.h>
#include <coredecls.h> // settimeofday_cb()
#include <time.h>

// GPIO mapping
#define CS_PIN 13
#define CLK_PIN 12
#define DATA_PIN 14
#define BUTTON_PIN 0
#define LED_PIN 2 // ESP-12F blue LED (active-low)

// Button timing (ms)
const unsigned long DEBOUNCE_MS = 50;
const unsigned long LONG_PRESS_MS = 800;
const unsigned long DATE_SHOW_MS = 3000;

// Time sync timing
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
const unsigned long NTP_TIMEOUT_MS = 10000;
const unsigned long RETRY_INTERVAL_MS = 60000; // Wait between failed attempts
const uint8_t FAILS_BEFORE_ERROR = 3;          // Show "NO WIFI" after this many
const int DAILY_SYNC_HOUR = 2;

VFD_Driver vfd(CS_PIN, CLK_PIN, DATA_PIN);
AyresWiFiManager wm;

// --- User settings (persisted in LittleFS) ---
bool dstEnabled = false;
bool use12HourMode = false;
uint8_t animStyle = 0; // 0 = Drop, 1 = Fade
uint8_t dimLevel = 4;  // 1..8, index into dimValues
const uint8_t dimValues[8] = {30, 60, 90, 120, 150, 180, 210, 240};

// --- Menu State Machine ---
enum ClockState { MODE_CLOCK, MODE_DATE, MODE_MENU, MODE_DIMMER, MODE_ANIM };
ClockState currentState = MODE_CLOCK;

enum MenuOption {
  MENU_EXIT,
  MENU_TOGGLE_DST,
  MENU_TOGGLE_12_24,
  MENU_ANIM,
  MENU_DIMMER,
  MENU_RESET_WIFI,
  MENU_COUNT
};
MenuOption currentMenu = MENU_EXIT;

bool redrawClock = true;          // Set when returning from a text screen
unsigned long dateShownAt = 0;

// --- Time sync state ---
enum SyncState { SYNC_IDLE, SYNC_CONNECTING, SYNC_WAITING_NTP };
SyncState syncState = SYNC_IDLE;
unsigned long syncStepStart = 0;
unsigned long lastSyncEnd = 0;
uint8_t syncFailures = 0;
volatile bool ntpReceived = false; // Set by the SNTP callback
String wifiSsid, wifiPass;

// ============================================================
//  Settings
// ============================================================

int loadSetting(const char *path, int fallback) {
  File f = LittleFS.open(path, "r");
  if (!f) return fallback;
  int value = f.readString().toInt();
  f.close();
  return value;
}

void saveSetting(const char *path, int value) {
  File f = LittleFS.open(path, "w");
  if (f) {
    f.print(value);
    f.close();
  }
}

// The WiFi manager keeps its credentials private, so read them ourselves
// for the background sync.
void loadWifiCredentials() {
  File f = LittleFS.open("/wifi.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (!deserializeJson(doc, f)) {
    wifiSsid = doc["ssid"].as<String>();
    wifiPass = doc["password"].as<String>();
  }
  f.close();
}

bool hasWifiCredentials() { return wifiSsid.length() > 0; }

// ============================================================
//  Time helpers
// ============================================================

bool timeIsValid() { return time(nullptr) > 100000; }

// Local time with the DST offset applied
struct tm localNow() {
  time_t t = time(nullptr) + (dstEnabled ? 3600 : 0);
  return *localtime(&t);
}

void formatTime(char *buf, const struct tm &t) {
  if (use12HourMode) {
    strftime(buf, 9, "%I:%M:%S", &t);
    if (buf[0] == '0') buf[0] = ' '; // Clean leading zero
  } else {
    strftime(buf, 9, "%H:%M:%S", &t);
  }
}

// ============================================================
//  WiFi / NTP sync (non-blocking)
//
//  The radio is only on while syncing. If the time has never been set
//  (e.g. the router is still booting after a power cut) we keep retrying
//  every RETRY_INTERVAL_MS until it works.
// ============================================================

void startSync() {
  syncState = SYNC_CONNECTING;
  syncStepStart = millis();
  ntpReceived = false;
  digitalWrite(LED_PIN, LOW);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  }
}

void finishSync(bool success) {
  // Kill the RF hardware to save heat
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  digitalWrite(LED_PIN, HIGH);
  syncState = SYNC_IDLE;
  lastSyncEnd = millis();
  syncFailures = success ? 0 : syncFailures + 1;
}

void updateSync() {
  unsigned long elapsed = millis() - syncStepStart;

  switch (syncState) {
  case SYNC_IDLE:
    if (!hasWifiCredentials() || wm.isPortalActive()) return;
    if (!timeIsValid()) {
      if (millis() - lastSyncEnd > RETRY_INTERVAL_MS) startSync();
    } else {
      static int lastSyncDay = -1;
      struct tm now = localNow();
      if (now.tm_hour == DAILY_SYNC_HOUR && now.tm_yday != lastSyncDay) {
        lastSyncDay = now.tm_yday;
        startSync();
      }
    }
    break;

  case SYNC_CONNECTING:
    if (WiFi.status() == WL_CONNECTED) {
      configTime(0, 0, "pool.ntp.org");
      syncState = SYNC_WAITING_NTP;
      syncStepStart = millis();
    } else if (elapsed > WIFI_CONNECT_TIMEOUT_MS) {
      finishSync(false);
    }
    break;

  case SYNC_WAITING_NTP:
    if (ntpReceived) finishSync(true);
    else if (elapsed > NTP_TIMEOUT_MS) finishSync(false);
    break;
  }
}

// ============================================================
//  Menu / UI
// ============================================================

void showMessage(const char *msg) {
  vfd.print(msg);
  delay(1000);
}

void returnToClock() {
  currentState = MODE_CLOCK;
  redrawClock = true;
}

void saveAndExit(const char *path, int value) {
  saveSetting(path, value);
  showMessage("SAVED   ");
  returnToClock();
}

void showMenuItem() {
  switch (currentMenu) {
  case MENU_EXIT:         vfd.print("MENU:EXT"); break;
  case MENU_TOGGLE_DST:   vfd.print(dstEnabled ? "DST->OFF" : "DST->ON "); break;
  case MENU_TOGGLE_12_24: vfd.print(use12HourMode ? "12H->24H" : "24H->12H"); break;
  case MENU_ANIM:         vfd.print("ANIM FX "); break;
  case MENU_DIMMER:       vfd.print("DIMMER  "); break;
  case MENU_RESET_WIFI:   vfd.print("RST WIFI"); break;
  default: break;
  }
}

void showAnimStyle() { vfd.print(animStyle == 0 ? "FX: DROP" : "FX: FADE"); }

void showDimLevel() {
  char buf[9];
  snprintf(buf, sizeof(buf), "LEVEL: %c", '0' + dimLevel);
  vfd.print(buf);
}

void resetWiFiAndReboot() {
  vfd.print("ERASING ");
  wm.eraseCredentials();
  delay(2000); // Give the user time to read the screen
  showMessage("REBOOT  ");
  ESP.restart();
}

void selectMenuItem() {
  switch (currentMenu) {
  case MENU_EXIT:
    showMessage("EXITING ");
    returnToClock();
    break;
  case MENU_TOGGLE_DST:
    dstEnabled = !dstEnabled;
    saveAndExit("/dst.txt", dstEnabled);
    break;
  case MENU_TOGGLE_12_24:
    use12HourMode = !use12HourMode;
    saveAndExit("/12hr.txt", use12HourMode);
    break;
  case MENU_ANIM:
    currentState = MODE_ANIM;
    showAnimStyle();
    break;
  case MENU_DIMMER:
    currentState = MODE_DIMMER;
    showDimLevel();
    break;
  case MENU_RESET_WIFI:
    resetWiFiAndReboot();
    break;
  default: break;
  }
}

void onLongPress() {
  switch (currentState) {
  case MODE_CLOCK:
  case MODE_DATE:
    currentState = MODE_MENU;
    currentMenu = MENU_EXIT;
    showMenuItem();
    break;
  case MODE_MENU:
    selectMenuItem();
    break;
  case MODE_ANIM:
    vfd.setAnimStyle(animStyle);
    saveAndExit("/anim.txt", animStyle);
    break;
  case MODE_DIMMER:
    saveAndExit("/dim.txt", dimLevel);
    break;
  }
}

void onShortPress() {
  switch (currentState) {
  case MODE_CLOCK:
    if (timeIsValid()) {
      currentState = MODE_DATE; // Date peek
      dateShownAt = millis();
    } else if (syncState == SYNC_IDLE && hasWifiCredentials() && !wm.isPortalActive()) {
      syncFailures = 0; // No time yet: tap to retry WiFi now
      startSync();
    }
    break;
  case MODE_DATE:
    currentState = MODE_CLOCK; // Tap again to dismiss early
    break;
  case MODE_MENU:
    currentMenu = MenuOption((currentMenu + 1) % MENU_COUNT);
    showMenuItem();
    break;
  case MODE_ANIM:
    animStyle = !animStyle;
    showAnimStyle();
    break;
  case MODE_DIMMER:
    dimLevel = dimLevel % 8 + 1; // 1..8, wrapping
    vfd.setBrightness(dimValues[dimLevel - 1]);
    showDimLevel();
    break;
  }
}

void readButton() {
  static bool isPressed = false;
  static bool longPressFired = false;
  static unsigned long pressStart = 0;

  bool down = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long held = millis() - pressStart;

  if (down && !isPressed) {
    isPressed = true;
    longPressFired = false;
    pressStart = millis();
  } else if (down && held >= LONG_PRESS_MS && !longPressFired) {
    longPressFired = true; // Fires once, while still holding
    onLongPress();
  } else if (!down && isPressed) {
    isPressed = false;
    if (held > DEBOUNCE_MS && !longPressFired) onShortPress();
  }
}

// ============================================================
//  Display
// ============================================================

// Shown in place of the clock until the time has been set
const char *statusText() {
  static char buf[9];
  if (wm.isPortalActive()) return "SET WIFI";
  if (!hasWifiCredentials() || syncFailures >= FAILS_BEFORE_ERROR) return "NO WIFI ";
  if (syncState == SYNC_WAITING_NTP) return "SYNCING ";
  if (syncFailures == 0) return "WIFI... ";
  snprintf(buf, sizeof(buf), "RETRY %c", '0' + syncFailures);
  return buf;
}

void updateDisplay() {
  if (currentState != MODE_CLOCK && currentState != MODE_DATE) return;

  if (!timeIsValid()) {
    static char shown[9] = "";
    const char *msg = statusText();
    if (redrawClock || strcmp(msg, shown) != 0) {
      strlcpy(shown, msg, sizeof(shown));
      vfd.print(msg);
      redrawClock = true; // Re-attach the framebuffer once the time arrives
    }
    return;
  }

  if (redrawClock) {
    vfd.initFramebuffer();
    redrawClock = false;
  }

  struct tm now = localNow();
  char text[9];
  bool slideUp = false;

  if (currentState == MODE_DATE && millis() - dateShownAt > DATE_SHOW_MS) {
    currentState = MODE_CLOCK;
    slideUp = true; // Date slides up back to the time
  }

  if (currentState == MODE_DATE) strftime(text, sizeof(text), "%d-%m-%y", &now);
  else formatTime(text, now);

  vfd.animateTo(text, slideUp); // No-op if unchanged or mid-animation
  vfd.updateAnimation();
}

// ============================================================

void setup() {
  vfd.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!LittleFS.begin()) {
    vfd.print("FS ERR");
    return;
  }

  // 1. Load User Preferences
  dstEnabled = loadSetting("/dst.txt", 0) == 1;
  use12HourMode = loadSetting("/12hr.txt", 0) == 1;
  dimLevel = loadSetting("/dim.txt", 4);
  if (dimLevel < 1 || dimLevel > 8) dimLevel = 4;
  animStyle = loadSetting("/anim.txt", 0);
  if (animStyle > 1) animStyle = 0;

  vfd.setBrightness(dimValues[dimLevel - 1]);
  vfd.setAnimStyle(animStyle);

  // 2. WiFi manager: opens the setup portal if there are no credentials
  vfd.print("WIFI... ");

  wm.setHostname("VFDClock");
  wm.setAPCredentials("VFD-Clock", "12345678");
  wm.setPortalTimeout(300);
  wm.setAPClientCheck(true);
  wm.setWebClientCheck(true);
  wm.begin();
  wm.run();

  // 3. Fetch NTP time, then turn the radio off. If WiFi isn't up yet,
  // updateSync() keeps retrying from loop().
  pinMode(LED_PIN, OUTPUT);
  settimeofday_cb([](bool fromSntp) {
    if (fromSntp) ntpReceived = true;
  });
  loadWifiCredentials();
  if (hasWifiCredentials() && !wm.isPortalActive()) startSync();
}

void loop() {
  if (wm.isPortalActive()) wm.update();
  updateSync();
  readButton();
  updateDisplay();
}
