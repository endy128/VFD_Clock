#include <Arduino.h>
#include <AyresWiFiManager.h>
#include <LittleFS.h>
#include <VFD_Driver.h>
#include <time.h>

// GPIO mapping
#define CS_PIN 13
#define CLK_PIN 12
#define DATA_PIN 14
#define BUTTON_PIN 0

VFD_Driver vfd(CS_PIN, CLK_PIN, DATA_PIN);
AyresWiFiManager wm;

// --- Dimmer Globals ---
uint8_t currentDimLevel = 4; // Default to Level 4 (120)
const uint8_t dimValues[8] = {30, 60, 90, 120, 150, 180, 210, 240};

// --- Menu State Machine ---
enum ClockState { MODE_CLOCK, MODE_DATE, MODE_MENU, MODE_DIMMER };
ClockState currentState = MODE_CLOCK;

enum MenuOption { MENU_EXIT, MENU_TOGGLE_DST, MENU_DIMMER, MENU_RESET_WIFI };
MenuOption currentMenu = MENU_EXIT;

// --- Button tracking ---
unsigned long buttonPressTime = 0;
bool buttonIsPressed = false;
// Tracks if we already fired the long press
bool longPressExecuted = false;

// for Daylight Saving Time
bool dstEnabled = false;

// Tells the loop whether to update the portal
bool wifiManagerActive = false;

void setup() {
  vfd.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!LittleFS.begin()) {
    vfd.print("FS ERR");
    return;
  }

  // 1. Load User Preferences (DST and Dimmer)
  if (LittleFS.exists("/dst.txt")) {
    File f = LittleFS.open("/dst.txt", "r");
    if (f) {
      dstEnabled = (f.readString() == "1");
      f.close();
    }
  }

  if (LittleFS.exists("/dim.txt")) {
    File f = LittleFS.open("/dim.txt", "r");
    if (f) {
      currentDimLevel = f.readString().toInt();
      if (currentDimLevel < 1 || currentDimLevel > 8)
        currentDimLevel = 4;
      f.close();
    }
  }
  vfd.setBrightness(dimValues[currentDimLevel - 1]);

  // 2. Let the Library handle the Fast Connect
  vfd.print("WIFI... ");

  wm.setHostname("VFDClock");
  wm.setAPCredentials("VFD-Clock", "12345678");
  wm.setPortalTimeout(300);
  wm.setAPClientCheck(true);
  wm.setWebClientCheck(true);

  wm.begin();
  wm.run(); // This will auto-connect instantly if it has the JSON keys!

  // 3. The "Stealth" Takeover
  if (WiFi.status() == WL_CONNECTED) {
    vfd.print("SYNCING ");
    configTime(0, 0, "pool.ntp.org");

    // Wait for the atomic time to arrive
    int retries = 0;
    time_t now = time(nullptr);
    while (now < 100000 && retries < 20) {
      delay(500);
      now = time(nullptr);
      retries++;
    }

    if (now > 100000) {
      vfd.print("RADIOOFF");
      delay(1000);

      // We have the time. Kill the RF hardware to save heat!
      WiFi.disconnect();
      WiFi.mode(WIFI_OFF);
      wifiManagerActive = false; // Tell the loop to ignore wm.update()
      // Manually turn off the ESP-12F Blue LED (Active-Low)
      pinMode(2, OUTPUT);
      digitalWrite(2, HIGH);
    } else {
      // Failed to get NTP time, keep manager alive just in case
      wifiManagerActive = true;
    }
  } else {
    // No credentials or bad password. Portal is running.
    vfd.print("SET WIFI");
    wifiManagerActive = true;
  }

  // Switch to animation mode
  vfd.initFramebuffer();
}

// Helper function to wipe the credentials
void resetWiFiAndReboot() {
  vfd.print("ERASING ");

  // Use the WiFi manager to delete the credentials
  wm.eraseCredentials();

  delay(2000); // Give the user time to read the screen
  vfd.print("REBOOT  ");
  delay(1000);
  ESP.restart(); // Hard reboot the ESP8266
}

void loop() {
  // Only update the portal if the Fast Boot failed
  if (wifiManagerActive) {
    wm.update();
  }

  // --- STEALTH SYNC VARIABLES ---
  static bool isStealthSyncing = false;
  static unsigned long syncStartTime = 0;
  static unsigned long connectedTime = 0;
  static int lastSyncDay =
      -1; // Prevents it from syncing multiple times in the same minute

  // --- Button Reading Logic ---
  bool currentReading = (digitalRead(BUTTON_PIN) == LOW);

  if (currentReading) {
    // 1. Button just went down
    if (!buttonIsPressed) {
      buttonPressTime = millis();
      buttonIsPressed = true;
      longPressExecuted = false; // Reset the flag for this new press
    }
    // 2. Button is being held down
    else {
      unsigned long pressDuration = millis() - buttonPressTime;

      // --- LONG PRESS ACTION (Triggers immediately while holding) ---
      if (pressDuration >= 800 && !longPressExecuted) {
        longPressExecuted = true; // Lock it so it only fires once

        // --- NEW: LONG PRESS ENTERS MENU ---
        if (currentState == MODE_CLOCK || currentState == MODE_DATE) {
          currentState = MODE_MENU;
          currentMenu = MENU_EXIT;
          vfd.print("MENU:EXT");
        } else if (currentState == MODE_MENU) {
          if (currentMenu == MENU_EXIT) {
            currentState = MODE_CLOCK;
            vfd.print("EXITING ");
            delay(1000);
            vfd.initFramebuffer();
          } else if (currentMenu == MENU_RESET_WIFI) {
            resetWiFiAndReboot();
          } else if (currentMenu == MENU_TOGGLE_DST) {
            dstEnabled = !dstEnabled;
            File f = LittleFS.open("/dst.txt", "w");
            if (f) {
              f.print(dstEnabled ? "1" : "0");
              f.close();
            }
            vfd.print("SAVED   ");
            delay(1000);
            currentState = MODE_CLOCK;
            vfd.initFramebuffer();
          } else if (currentMenu == MENU_DIMMER) {
            currentState = MODE_DIMMER;
            char lvlStr[9];
            snprintf(lvlStr, sizeof(lvlStr), "LEVEL: %d", currentDimLevel);
            vfd.print(lvlStr);
          }
        } else if (currentState == MODE_DIMMER) {
          // Save Dimmer and Exit
          File f = LittleFS.open("/dim.txt", "w");
          if (f) {
            f.print(currentDimLevel);
            f.close();
          }
          vfd.print("SAVED   ");
          delay(1000);
          currentState = MODE_CLOCK;
          vfd.initFramebuffer();
        }
      }
    }
  }
  // 3. Button just went up
  else if (!currentReading && buttonIsPressed) {
    unsigned long pressDuration = millis() - buttonPressTime;
    buttonIsPressed = false;

    // --- SHORT PRESS ACTION (Only fires if we didn't just do a long press) ---
    if (pressDuration > 50 && !longPressExecuted) {

      // --- NEW: SHORT PRESS TRIGGERS DATE PEEK ---
      if (currentState == MODE_CLOCK) {
        currentState = MODE_DATE;
      } else if (currentState == MODE_DATE) {
        currentState = MODE_CLOCK; // Tap again to manually dismiss early
      } else if (currentState == MODE_MENU) {
        // Cycle Menu
        if (currentMenu == MENU_EXIT) {
          currentMenu = MENU_TOGGLE_DST;
          vfd.print(dstEnabled ? "DST->OFF" : "DST->ON ");
        } else if (currentMenu == MENU_TOGGLE_DST) {
          currentMenu = MENU_DIMMER;
          vfd.print("DIMMER  ");
        } else if (currentMenu == MENU_DIMMER) {
          currentMenu = MENU_RESET_WIFI;
          vfd.print("RST WIFI");
        } else {
          currentMenu = MENU_EXIT;
          vfd.print("MENU:EXT");
        }
      } else if (currentState == MODE_DIMMER) {
        // Cycle Brightness
        currentDimLevel++;
        if (currentDimLevel > 8)
          currentDimLevel = 1;
        vfd.setBrightness(dimValues[currentDimLevel - 1]);
        char lvlStr[9];
        snprintf(lvlStr, sizeof(lvlStr), "LEVEL: %d", currentDimLevel);
        vfd.print(lvlStr);
      }
    }
  }

// --- Clock & Date Display Logic ---
  if (currentState == MODE_CLOCK || currentState == MODE_DATE) {
    static int lastSecond = -1;
    static unsigned long dateShowTime = 0;
    static bool dateInitialized = false;

    time_t raw_now = time(nullptr);

    if (raw_now > 100000) {
      time_t displayTime = raw_now + (dstEnabled ? 3600 : 0);
      struct tm *timeInfo = localtime(&displayTime);

      // --- 2 AM STEALTH SYNC ENGINE ---
      if (timeInfo->tm_hour == 2 && timeInfo->tm_min == 0 && timeInfo->tm_sec == 0 && lastSyncDay != timeInfo->tm_yday) {
        isStealthSyncing = true;
        lastSyncDay = timeInfo->tm_yday;
        syncStartTime = millis();
        connectedTime = 0;
        WiFi.mode(WIFI_STA);
        WiFi.begin();
        digitalWrite(2, LOW);
      }

      if (isStealthSyncing) {
        if (millis() - syncStartTime > 15000) {
          isStealthSyncing = false;
          WiFi.disconnect();
          WiFi.mode(WIFI_OFF);
          pinMode(2, OUTPUT); digitalWrite(2, HIGH);
        } else if (WiFi.status() == WL_CONNECTED) {
          if (connectedTime == 0) {
            connectedTime = millis();
            configTime(0, 0, "pool.ntp.org");
          }
          if (millis() - connectedTime > 5000) {
            isStealthSyncing = false;
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
            pinMode(2, OUTPUT); digitalWrite(2, HIGH);
          }
        }
      }
      // --------------------------------

      char targetText[9] = "        ";

      // --- DATE MODE LOGIC ---
      if (currentState == MODE_DATE) {
        if (!dateInitialized) {
          dateInitialized = true;
          dateShowTime = millis();
          
          // Using the new dash formatting!
          strftime(targetText, sizeof(targetText), "%d-%m-%y", timeInfo);
          vfd.animateTo(targetText, false); // Slide down
        }

        // Revert to Clock Mode after 3 seconds
        if (millis() - dateShowTime > 3000) {
          currentState = MODE_CLOCK;
          dateInitialized = false;
          
          strftime(targetText, sizeof(targetText), "%H:%M:%S", timeInfo);
          vfd.animateTo(targetText, true); // Slide up!
        }
      }
      // --- CLOCK MODE LOGIC ---
      else {
        if (timeInfo->tm_sec != lastSecond) {
          if (lastSecond == -1) vfd.initFramebuffer(); 
          lastSecond = timeInfo->tm_sec;

          strftime(targetText, sizeof(targetText), "%H:%M:%S", timeInfo);
          vfd.animateTo(targetText, false); // Normal second tick slides down
        }
      }
    }

    // Tell the VFD driver to process any pending animation frames
    vfd.updateAnimation();
  }
} // End of loop()
