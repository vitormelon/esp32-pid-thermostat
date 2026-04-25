// Mock mínimo de Arduino.h para testes nativos no host.
// Tempo (millis/micros) é controlado pelos helpers mockAdvanceMs/mockReset.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>

typedef uint8_t  byte;
typedef bool     boolean;

#define HIGH         1
#define LOW          0
#define OUTPUT       1
#define INPUT        0
#define INPUT_PULLUP 2

extern unsigned long _mockMillis;
extern unsigned long _mockMicros;
extern int           _mockGpioState[64];
extern int           _mockGpioWriteCount[64];   // quantas vezes digitalWrite foi chamado por pino

inline unsigned long millis() { return _mockMillis; }
inline unsigned long micros() { return _mockMicros; }
inline void delay(unsigned long ms) { _mockMillis += ms; _mockMicros += ms * 1000UL; }

inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int val) {
    if (pin >= 0 && pin < 64) {
        _mockGpioState[pin] = val;
        _mockGpioWriteCount[pin]++;
    }
}
inline int digitalRead(int pin) {
    return (pin >= 0 && pin < 64) ? _mockGpioState[pin] : 0;
}

template<typename T> inline T constrain(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// String mínimo (storage.cpp usa Arduino String)
class String {
public:
    String()                 { _set(""); }
    String(const char* s)    { _set(s ? s : ""); }
    String(const String& o)  { _set(o._s); }
    ~String()                { if (_s) free(_s); }
    String& operator=(const String& o) { _set(o._s); return *this; }
    bool        equals(const char* s) const { return strcmp(_s, s) == 0; }
    const char* c_str() const               { return _s; }
private:
    char* _s = nullptr;
    void  _set(const char* s) {
        if (_s) free(_s);
        size_t n = strlen(s);
        _s = (char*)malloc(n + 1);
        memcpy(_s, s, n + 1);
    }
};

class FakeSerial {
public:
    void begin(unsigned long) {}
    void println()                 {}
    void println(const char*)      {}
    void print(const char*)        {}
    void printf(const char*, ...)  {}
};
extern FakeSerial Serial;

// Helpers de teste
void mockReset();
void mockAdvanceMs(unsigned long ms);
