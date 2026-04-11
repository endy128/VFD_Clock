#include "VFD_Driver.h"

VFD_Driver::VFD_Driver(uint8_t cs, uint8_t clk, uint8_t data)
    : _cs(cs), _clk(clk), _data(data) {}

void VFD_Driver::setDigit(uint8_t digit) {
    digit--;
    writeCommand(cmd_set_digit, digit);
}

void VFD_Driver::begin() {
  pinMode(_cs, OUTPUT);
  pinMode(_clk, OUTPUT);
  pinMode(_data, OUTPUT);
  digitalWrite(_cs, HIGH);

  // Default Init Sequence
  setDigit(8);
  setBrightness(default_brightness);
  writeCommand(cmd_display_on);       // Display ON
}

void VFD_Driver::sendByte(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(_data, (data >> i) & 0x01);
    digitalWrite(_clk, HIGH);
    delayMicroseconds(2);
    digitalWrite(_clk, LOW);
    delayMicroseconds(2);
  }
}

void VFD_Driver::writeCommand(uint8_t cmd, uint8_t data) {
    digitalWrite(_cs, LOW);
    sendByte(cmd);
    if (data != 0xFF) sendByte(data);
    digitalWrite(_cs, HIGH);
}

void VFD_Driver::print(const char* msg) {
    digitalWrite(_cs, LOW);
    sendByte(0x20); // Address 0
    for (int i = 0; i < 8; i++) {
        sendByte(msg[i] ? msg[i] : ' ');
    }
    digitalWrite(_cs, HIGH);
}

void VFD_Driver::setBrightness(uint8_t level) {
    level = constrain(level, 0, max_brightness);
    writeCommand(cmd_set_dimming, level);
}

// Define the font map (0-9, Space, Colon)
const uint8_t VFD_Driver::font5x7[12][5] = {
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
  {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 10 (Space)
  {0x00, 0x36, 0x36, 0x00, 0x00}  // 11 (Colon)
};

// Writes 5 bytes of pixel data to a specific CGRAM slot (0-7)
void VFD_Driver::setCGRAM(uint8_t slot, uint8_t* columns) {
    digitalWrite(_cs, LOW);
    sendByte(0x40 + (slot & 0x07)); // 0x40 is the base CGRAM write command
    for(int i = 0; i < 5; i++) {
        sendByte(columns[i]);
    }
    digitalWrite(_cs, HIGH);
}

// Fills the screen with CGRAM slots 0-7 permanently
void VFD_Driver::initFramebuffer() {
    digitalWrite(_cs, LOW);
    sendByte(0x20); // DCRAM Address 0
    for (uint8_t i = 0; i < 8; i++) {
        sendByte(i); // Write char codes 0x00 to 0x07 (the CGRAM addresses)
    }
    digitalWrite(_cs, HIGH);
}
