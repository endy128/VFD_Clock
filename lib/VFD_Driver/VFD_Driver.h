#ifndef VFD_DRIVER_H
#define VFD_DRIVER_H

#include <Arduino.h>

class VFD_Driver {
public:
  VFD_Driver(uint8_t cs, uint8_t clk, uint8_t data);

  void begin();
  void writeCommand(uint8_t cmd, uint8_t data = 0xFF);
  void print(const char *msg);
  void setBrightness(uint8_t level);
  // Set number of display digits
  void setDigit(uint8_t digit);

  void setCGRAM(uint8_t slot, uint8_t *columns);
  void initFramebuffer();

  // 5x7 Font Data for 0-9, Space, and Colon
  static const uint8_t font5x7[12][5];

private:
  uint8_t _cs{}, _clk{}, _data{};
  static constexpr uint32_t default_brightness{120}; // Default brightness value
  static constexpr uint32_t max_brightness{240};     // Maximum brightness value

  // Directly followed by brightness value (1 byte, max 240), total 2 bytes
  static constexpr uint8_t cmd_set_dimming{0xE4};

  // Directly followed by digit count (7 bits), total 2 bytes
  static constexpr uint8_t cmd_set_digit{0xE0};

  // Send directly, total 1 byte
  static constexpr uint8_t cmd_display_on{0xE8};

  void sendByte(uint8_t data);
};

#endif
