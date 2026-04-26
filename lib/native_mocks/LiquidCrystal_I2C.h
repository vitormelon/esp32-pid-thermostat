// Mock minimal de LiquidCrystal_I2C para testes nativos.
// Captura a "tela" renderizada e conta operações I2C para validar diff redraw.
#pragma once
#include "Arduino.h"

#define MOCK_LCD_MAX_COLS  20
#define MOCK_LCD_MAX_ROWS  4

extern char _mockLcdScreen[MOCK_LCD_MAX_ROWS][MOCK_LCD_MAX_COLS];  // estado visível
extern int  _mockLcdSetCursorCount;     // chamadas a setCursor
extern int  _mockLcdWriteCharCount;     // bytes efetivamente escritos
extern int  _mockLcdCreateCharCount;    // chamadas a createChar
extern int  _mockLcdCursorCol;
extern int  _mockLcdCursorRow;
extern bool _mockLcdBacklight;

void mockLcdReset();

class LiquidCrystal_I2C {
public:
    LiquidCrystal_I2C(uint8_t /*addr*/, uint8_t /*cols*/, uint8_t /*rows*/) {}
    void init()         {}
    void clear()        { mockLcdReset(); }
    void backlight()    { _mockLcdBacklight = true; }
    void noBacklight()  { _mockLcdBacklight = false; }

    void setCursor(uint8_t col, uint8_t row) {
        _mockLcdSetCursorCount++;
        _mockLcdCursorCol = col;
        _mockLcdCursorRow = row;
    }

    void print(const char* s) {
        while (*s) write((uint8_t)*s++);
    }

    void write(uint8_t c) {
        _mockLcdWriteCharCount++;
        if (_mockLcdCursorRow >= 0 && _mockLcdCursorRow < MOCK_LCD_MAX_ROWS
            && _mockLcdCursorCol >= 0 && _mockLcdCursorCol < MOCK_LCD_MAX_COLS) {
            _mockLcdScreen[_mockLcdCursorRow][_mockLcdCursorCol] = (char)c;
        }
        _mockLcdCursorCol++;
    }

    void createChar(uint8_t /*idx*/, uint8_t* /*data*/) {
        _mockLcdCreateCharCount++;
    }
};
