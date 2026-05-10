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
  void setDigit(uint8_t digit);

  void setCGRAM(uint8_t slot, uint8_t *columns);
  void initFramebuffer();

  // --- NEW: Animation Engine Interface ---
  void setAnimStyle(uint8_t style);
  void animateTo(const char* targetText, bool slideUp = false);
  void updateAnimation();
  bool isAnimating() const { return _isAnimating; }

private:
  uint8_t _cs{}, _clk{}, _data{};
  static constexpr uint32_t default_brightness{120}; 
  static constexpr uint32_t max_brightness{240};     

  static constexpr uint8_t cmd_set_dimming{0xE4};
  static constexpr uint8_t cmd_set_digit{0xE0};
  static constexpr uint8_t cmd_display_on{0xE8};

  void sendByte(uint8_t data);

  // --- NEW: Animation Engine Variables ---
  // Expanded to 13 to include the new Dash character
  static const uint8_t font5x7[13][5]; 
  
  uint8_t charToFontIdx(char c);
  uint8_t _animStyle{0}; // 0 = Drop, 1 = Fade

  bool _isAnimating{false};
  int _animStep{0};
  unsigned long _lastAnimTime{0};
  bool _animDirectionUp{false};
  char _currentText[9]{"        "};
  char _targetText[9]{"        "};
};

#endif
