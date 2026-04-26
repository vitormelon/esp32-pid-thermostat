#include "LiquidCrystal_I2C.h"
#include "Wire.h"

unsigned long _mockWireClock = 100000UL;
TwoWire       Wire;

char _mockLcdScreen[MOCK_LCD_MAX_ROWS][MOCK_LCD_MAX_COLS];
int  _mockLcdSetCursorCount   = 0;
int  _mockLcdWriteCharCount   = 0;
int  _mockLcdCreateCharCount  = 0;
int  _mockLcdCursorCol        = 0;
int  _mockLcdCursorRow        = 0;
bool _mockLcdBacklight        = true;

void mockLcdReset() {
    for (int r = 0; r < MOCK_LCD_MAX_ROWS; r++)
        for (int c = 0; c < MOCK_LCD_MAX_COLS; c++)
            _mockLcdScreen[r][c] = ' ';
    _mockLcdSetCursorCount  = 0;
    _mockLcdWriteCharCount  = 0;
    _mockLcdCreateCharCount = 0;
    _mockLcdCursorCol       = 0;
    _mockLcdCursorRow       = 0;
    _mockLcdBacklight       = true;
    _mockWireClock          = 100000UL;
}
