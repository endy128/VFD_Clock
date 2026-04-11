#include <Arduino.h>
#include <AyresWiFiManager.h>
#include <LittleFS.h>
#include <time.h>

// Your verified GPIO mapping
#define VFD_CS 13
#define VFD_CLK 12
#define VFD_DATA 14

AyresWiFiManager wm;

// --- Manual VFD Driver Functions ---
void sendVFD(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(VFD_DATA, (data >> i) & 0x01);
    digitalWrite(VFD_CLK, HIGH);
    delayMicroseconds(2);
    digitalWrite(VFD_CLK, LOW);
    delayMicroseconds(2);
  }
}

void writeCommand(uint8_t cmd, uint8_t data = 0xFF) {
  digitalWrite(VFD_CS, LOW);
  sendVFD(cmd);
  if (data != 0xFF)
    sendVFD(data);
  digitalWrite(VFD_CS, HIGH);
}

void printVFD(const char *msg) {
  digitalWrite(VFD_CS, LOW);
  sendVFD(0x20); // Start at Address 0
  for (int i = 0; i < 8; i++) {
    sendVFD(msg[i] ? msg[i] : ' '); // Send character or space
  }
  digitalWrite(VFD_CS, HIGH);
}

void setup() {
  pinMode(VFD_CS, OUTPUT);
  pinMode(VFD_CLK, OUTPUT);
  pinMode(VFD_DATA, OUTPUT);

  // Initial VFD Wake-up
  writeCommand(0xE0, 0x07);
  writeCommand(0xE4, 0x90);
  writeCommand(0xE8);
  printVFD("SYNCING ");
  
  // Initialize Filesystem for WiFi Credentials
  if (!LittleFS.begin()) {
    printVFD("FS ERR");
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
  
  printVFD("SET WIFI");
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
      // Format: HH-MM-SS (8 characters for your 8-digit VFD)
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeInfo);
      printVFD(timeStr);
    }
  }
}
