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
enum ClockState { MODE_CLOCK, MODE_MENU, MODE_DIMMER };
ClockState currentState = MODE_CLOCK;

enum MenuOption { MENU_EXIT, MENU_TOGGLE_DST, MENU_DIMMER, MENU_RESET_WIFI };
MenuOption currentMenu = MENU_EXIT;

// Button tracking
unsigned long buttonPressTime = 0;
bool buttonIsPressed = false;

// for Daylight Saving Time
bool dstEnabled = false;

void setup() {
  vfd.begin();

  // Set button with internal pullup (reads LOW when pressed)
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!LittleFS.begin()) {
    vfd.print("FS ERR");
    return;
  }

  // Load DST preference from memory
  if (LittleFS.exists("/dst.txt")) {
    File f = LittleFS.open("/dst.txt", "r");
    if (f) {
      dstEnabled = (f.readString() == "1");
      f.close();
    }
  }

  // Load Dimmer preference from memory
  if (LittleFS.exists("/dim.txt")) {
    File f = LittleFS.open("/dim.txt", "r");
    if (f) {
      currentDimLevel = f.readString().toInt();
      // Sanity check in case the file gets corrupted
      if (currentDimLevel < 1 || currentDimLevel > 8)
        currentDimLevel = 4;
      f.close();
    }
  }

  // Apply the loaded brightness
  vfd.setBrightness(dimValues[currentDimLevel - 1]);

  vfd.initFramebuffer();
  vfd.print("HELLO :)");
  WiFi.disconnect();

  wm.setHostname("VFDClock");
  wm.setAPCredentials("VFD-Clock", "12345678");
  wm.setPortalTimeout(300);
  wm.setAPClientCheck(true);
  wm.setWebClientCheck(true);

  wm.begin();
  wm.run();

  if (WiFi.status() != WL_CONNECTED) {
    vfd.print("SET WIFI");
  } else {
    vfd.print("SYNCING ");
  }
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
  wm.update();

  // --- Button Reading Logic ---
  bool currentReading = (digitalRead(BUTTON_PIN) == LOW);

  if (currentReading && !buttonIsPressed) {
    // Button just went down
    buttonPressTime = millis();
    buttonIsPressed = true;
  } else if (!currentReading && buttonIsPressed) {
    // Button just went up
    unsigned long pressDuration = millis() - buttonPressTime;
    buttonIsPressed = false;

    // 1. SHORT PRESS: Cycle through menus
    if (pressDuration > 50 && pressDuration < 800) {
      if (currentState == MODE_CLOCK) {
        currentState = MODE_MENU;
        currentMenu = MENU_EXIT;
        vfd.print("MENU:EXT");
      } else if (currentState == MODE_MENU) {
        // Main Menu Cycle
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
      }
      // --- THE NEW SUB-MENU CYCLE ---
      else if (currentState == MODE_DIMMER) {
        currentDimLevel++;
        if (currentDimLevel > 8)
          currentDimLevel = 1; // Wrap around

        // Live Preview: Apply the brightness immediately!
        vfd.setBrightness(dimValues[currentDimLevel - 1]);

        // Print the level to the screen
        char lvlStr[9];
        snprintf(lvlStr, sizeof(lvlStr), "LEVEL: %d", currentDimLevel);
        vfd.print(lvlStr);
      }
    }

    // 2. LONG PRESS: Select the current option
    else if (pressDuration >= 800) {
      if (currentState == MODE_MENU) {
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
        }
        // --- DIVE INTO SUB-MENU ---
        else if (currentMenu == MENU_DIMMER) {
          currentState = MODE_DIMMER; // Switch to dimmer state

          char lvlStr[9];
          snprintf(lvlStr, sizeof(lvlStr), "LEVEL: %d", currentDimLevel);
          vfd.print(lvlStr);
        }
      }
      // --- SAVE AND EXIT SUB-MENU ---
      else if (currentState == MODE_DIMMER) {
        // Save the chosen level to LittleFS
        File f = LittleFS.open("/dim.txt", "w");
        if (f) {
          f.print(currentDimLevel);
          f.close();
        }

        vfd.print("SAVED   ");
        delay(1000);

        // Return all the way to the clock
        currentState = MODE_CLOCK;
        vfd.initFramebuffer();
      }
    }
  }

  // --- Clock Display Logic ---
  if (currentState == MODE_CLOCK) {
    static int lastSecond = -1;
    static char currentText[9] = "        ";
    static char targetText[9] = "        ";

    static bool isAnimating = false;
    static int animStep = 0;
    static unsigned long lastAnimTime = 0;

    time_t raw_now = time(nullptr);

    // Only process if NTP has synced
    if (raw_now > 100000) {

      // Apply DST offset (+3600 seconds)
      time_t displayTime = raw_now + (dstEnabled ? 3600 : 0);
      struct tm *timeInfo = localtime(&displayTime);

      // Check if the second changed
      if (timeInfo->tm_sec != lastSecond) {
        if (lastSecond == -1) {
          vfd.initFramebuffer();
        }
        lastSecond = timeInfo->tm_sec;

        // Update target text using the adjusted displayTime
        strftime(targetText, sizeof(targetText), "%H:%M:%S", timeInfo);

        // Start animation if it's not already running
        if (!isAnimating) {
          isAnimating = true;
          animStep = 1;
        }
      }
    }

    // 2. Handle the Animation Frames (Non-blocking)
    if (isAnimating && (millis() - lastAnimTime > 25)) { // 25ms per frame
      lastAnimTime = millis();

      // Update all 8 characters
      for (int pos = 0; pos < 8; pos++) {
        uint8_t blendedCols[5];

        // Convert ASCII characters to our font array indices
        int oldFontIdx =
            (currentText[pos] == ':')
                ? 11
                : (currentText[pos] == ' ' ? 10 : currentText[pos] - '0');
        int newFontIdx =
            (targetText[pos] == ':')
                ? 11
                : (targetText[pos] == ' ' ? 10 : targetText[pos] - '0');

        // If the character isn't changing, just keep it static
        if (oldFontIdx == newFontIdx) {
          vfd.setCGRAM(pos, (uint8_t *)vfd.font5x7[oldFontIdx]);
        }
        // If it IS changing, calculate the blend
        else {
          for (int c = 0; c < 5; c++) {
            uint8_t oldCol = vfd.font5x7[oldFontIdx][c];
            uint8_t newCol = vfd.font5x7[newFontIdx][c];

            // The magical drop-down math
            blendedCols[c] = ((oldCol << animStep) & 0x7F) |
                             ((newCol >> (7 - animStep)) & 0x7F);
          }
          vfd.setCGRAM(pos, blendedCols);
        }
      }

      animStep++;

      // When animation finishes, lock the new text in
      if (animStep > 7) {
        isAnimating = false;
        strcpy(currentText, targetText); // The target is now the current
      }
    }
  }
}
