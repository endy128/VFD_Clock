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

  setDigit(8);
  setBrightness(default_brightness);
  writeCommand(cmd_display_on);
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
    sendByte(0x20); 
    // Pad short strings with spaces (don't read past the terminator)
    for (int i = 0; i < 8; i++) {
        sendByte(*msg ? *msg++ : ' ');
    }
    digitalWrite(_cs, HIGH);
}

void VFD_Driver::setBrightness(uint8_t level) {
    level = constrain(level, 0, max_brightness);
    writeCommand(cmd_set_dimming, level);
}

void VFD_Driver::setCGRAM(uint8_t slot, uint8_t* columns) {
    digitalWrite(_cs, LOW);
    sendByte(0x40 + (slot & 0x07)); 
    for(int i = 0; i < 5; i++) {
        sendByte(columns[i]);
    }
    digitalWrite(_cs, HIGH);
}

void VFD_Driver::initFramebuffer() {
    digitalWrite(_cs, LOW);
    sendByte(0x20); 
    for (uint8_t i = 0; i < 8; i++) {
        sendByte(i); 
    }
    digitalWrite(_cs, HIGH);
}

// --- NEW: Animation Engine Implementation ---

// Font map (0-9, Space, Colon, Dash)
const uint8_t VFD_Driver::font5x7[13][5] = {
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
  {0x00, 0x36, 0x36, 0x00, 0x00}, // 11 (Colon)
  {0x00, 0x08, 0x08, 0x08, 0x00}  // 12 (Dash)
};

uint8_t VFD_Driver::charToFontIdx(char c) {
  if (c == ':') return 11;
  if (c == '-') return 12;
  if (c == ' ') return 10;
  if (c >= '0' && c <= '9') return c - '0';
  return 10; // Fallback to space
}

void VFD_Driver::animateTo(const char* targetText, bool slideUp) {
    // Only start if text is different and not currently animating
    if (!_isAnimating && strncmp(_currentText, targetText, 8) != 0) {
        strncpy(_targetText, targetText, 8);
        _targetText[8] = '\0';
        _isAnimating = true;
        _animStep = 1;
        _animDirectionUp = slideUp;
    }
}

void VFD_Driver::setAnimStyle(uint8_t style) {
    _animStyle = style;
}

void VFD_Driver::updateAnimation() {
    if (!_isAnimating) return;

    // --- NEW: DYNAMIC SPEED ---
    // Drop is fast (25ms), Fade is slow and deliberate (60ms)
    unsigned long frameDelay = (_animStyle == 1) ? 60 : 25;

    if (millis() - _lastAnimTime > frameDelay) { 
        _lastAnimTime = millis();

        for (int pos = 0; pos < 8; pos++) {
            uint8_t blendedCols[5];
            uint8_t oldFontIdx = charToFontIdx(_currentText[pos]);
            uint8_t newFontIdx = charToFontIdx(_targetText[pos]);

            if (oldFontIdx == newFontIdx) {
                setCGRAM(pos, (uint8_t *)font5x7[oldFontIdx]);
            } else {
                for (int c = 0; c < 5; c++) {
                    uint8_t oldCol = font5x7[oldFontIdx][c];
                    uint8_t newCol = font5x7[newFontIdx][c];

                    // --- STYLE 0: THE DROP ---
                    if (_animStyle == 0) {
                        if (_animDirectionUp) {
                            blendedCols[c] = ((oldCol >> _animStep) & 0x7F) | ((newCol << (7 - _animStep)) & 0x7F);
                        } else {
                            blendedCols[c] = ((oldCol << _animStep) & 0x7F) | ((newCol >> (7 - _animStep)) & 0x7F);
                        }
                    } 
                    // --- STYLE 1: THE DISSOLVE FADE ---
                    else if (_animStyle == 1) {
                        uint8_t mask = 0;
                        bool evenCol = (c % 2 == 0);
                        
                        // A cumulative 8-step disintegration mask
                        switch (_animStep) {
                            case 0: mask = 0x00; break;                  // 0%
                            case 1: mask = evenCol ? 0x08 : 0x00; break; // Center dots appear
                            case 2: mask = evenCol ? 0x28 : 0x10; break; // Sparse static
                            case 3: mask = evenCol ? 0x2A : 0x54; break; // Dense static
                            case 4: mask = evenCol ? 0x55 : 0x2A; break; // 50% Perfect Checkerboard
                            case 5: mask = evenCol ? 0x75 : 0x6B; break; // Melting into new number
                            case 6: mask = evenCol ? 0x7D : 0x7F; break; // Almost solid
                            case 7: mask = 0x7F; break;                  // 100% New number
                            default: mask = 0x7F; break;
                        }
                        
                        blendedCols[c] = (oldCol & ~mask) | (newCol & mask);
                    }
                }
                setCGRAM(pos, blendedCols);
            }
        }

        _animStep++;

        if (_animStep > 7) {
            _isAnimating = false;
            strcpy(_currentText, _targetText);
        }
    }
}
