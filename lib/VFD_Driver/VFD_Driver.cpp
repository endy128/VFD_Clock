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
