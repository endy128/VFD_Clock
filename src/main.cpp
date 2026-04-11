#include <Arduino.h>
#include <AyresWiFiManager.h>
#include <LittleFS.h>
#include <VFD_Driver.h>
#include <time.h>

// GPIO mapping
#define CS_PIN 13
#define CLK_PIN 12
#define DATA_PIN 14

VFD_Driver vfd(CS_PIN, CLK_PIN, DATA_PIN);
AyresWiFiManager wm;

void setup() {
  vfd.begin();
  vfd.print("STARTING");

  // Initialize Filesystem for WiFi Credentials
  if (!LittleFS.begin()) {
    vfd.print("FS ERR");
    return;
  }

  // Start WiFi and NTP Sync
  // Default NTP server is pool.ntp.org
  wm.setHostname("VFDClock");
  wm.setAPCredentials("VFD CLock", "12345678");
  wm.setPortalTimeout(300);   // 5 min of inactivity
  wm.setAPClientCheck(true);  // don't close if clients connected
  wm.setWebClientCheck(true); // each HTTP request resets the timer
  wm.begin();
  wm.run();

  vfd.print("SET WIFI");
}

void loop() {
  wm.update();
  static unsigned long lastUpdate = 0;

  // Update the display every second
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();

    time_t now = time(nullptr);
    struct tm *timeInfo = localtime(&now);

    if (now > 100000) { // Check if time is actually synced
      char timeStr[9];
      // Format 24H clock: HH:MM:SS (8 characters for this 8-digit VFD)
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeInfo);
      vfd.print(timeStr);
    }
  }
}
