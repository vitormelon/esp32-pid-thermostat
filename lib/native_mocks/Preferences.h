// Mock de ESP32 Preferences (NVS) para testes nativos.
// Armazena valores em memória, com contadores de read/write para inspeção.
#pragma once

#include "Arduino.h"

#define MOCK_PREF_MAX_ENTRIES 256
#define MOCK_PREF_NS_LEN      16
#define MOCK_PREF_KEY_LEN     16
#define MOCK_PREF_STR_LEN     64

enum MockPrefType {
    MPT_NONE = 0,
    MPT_BOOL,
    MPT_INT,
    MPT_UINT,
    MPT_ULONG,
    MPT_FLOAT,
    MPT_STRING
};

struct MockPrefEntry {
    char         ns[MOCK_PREF_NS_LEN];
    char         key[MOCK_PREF_KEY_LEN];
    MockPrefType type;
    union {
        bool          b;
        int32_t       i;
        uint32_t      u;
        unsigned long ul;
        float         f;
    } v;
    char str[MOCK_PREF_STR_LEN];
};

extern MockPrefEntry _mockPrefStore[MOCK_PREF_MAX_ENTRIES];
extern int           _mockPrefCount;
extern int           _mockPrefWriteCount;
extern int           _mockPrefReadCount;

void mockPrefsReset();

class Preferences {
public:
    bool   begin(const char* ns, bool readOnly = false);
    void   end();

    bool   isKey(const char* key);

    bool          getBool(const char* key, bool def = false);
    int32_t       getInt(const char* key, int32_t def = 0);
    uint32_t      getUInt(const char* key, uint32_t def = 0);
    unsigned long getULong(const char* key, unsigned long def = 0);
    float         getFloat(const char* key, float def = 0.0f);
    String        getString(const char* key, const char* def = "");

    size_t putBool  (const char* key, bool v);
    size_t putInt   (const char* key, int32_t v);
    size_t putUInt  (const char* key, uint32_t v);
    size_t putULong (const char* key, unsigned long v);
    size_t putFloat (const char* key, float v);
    size_t putString(const char* key, const char* v);

private:
    char _ns[MOCK_PREF_NS_LEN] = {0};
    bool _open = false;

    MockPrefEntry* find(const char* key);
    MockPrefEntry* findOrCreate(const char* key);
};
