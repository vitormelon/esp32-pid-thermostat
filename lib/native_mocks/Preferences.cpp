#include "Preferences.h"

MockPrefEntry _mockPrefStore[MOCK_PREF_MAX_ENTRIES];
int           _mockPrefCount      = 0;
int           _mockPrefWriteCount = 0;
int           _mockPrefReadCount  = 0;

void mockPrefsReset() {
    for (int i = 0; i < MOCK_PREF_MAX_ENTRIES; i++) {
        _mockPrefStore[i].type = MPT_NONE;
        _mockPrefStore[i].ns[0]  = '\0';
        _mockPrefStore[i].key[0] = '\0';
    }
    _mockPrefCount      = 0;
    _mockPrefWriteCount = 0;
    _mockPrefReadCount  = 0;
}

bool Preferences::begin(const char* ns, bool /*readOnly*/) {
    strncpy(_ns, ns, MOCK_PREF_NS_LEN - 1);
    _ns[MOCK_PREF_NS_LEN - 1] = '\0';
    _open = true;
    return true;
}

void Preferences::end() { _open = false; _ns[0] = '\0'; }

MockPrefEntry* Preferences::find(const char* key) {
    for (int i = 0; i < _mockPrefCount; i++) {
        if (_mockPrefStore[i].type != MPT_NONE
            && strcmp(_mockPrefStore[i].ns, _ns) == 0
            && strcmp(_mockPrefStore[i].key, key) == 0) {
            return &_mockPrefStore[i];
        }
    }
    return nullptr;
}

MockPrefEntry* Preferences::findOrCreate(const char* key) {
    MockPrefEntry* e = find(key);
    if (e) return e;
    if (_mockPrefCount >= MOCK_PREF_MAX_ENTRIES) return nullptr;
    e = &_mockPrefStore[_mockPrefCount++];
    strncpy(e->ns, _ns, MOCK_PREF_NS_LEN - 1);
    e->ns[MOCK_PREF_NS_LEN - 1] = '\0';
    strncpy(e->key, key, MOCK_PREF_KEY_LEN - 1);
    e->key[MOCK_PREF_KEY_LEN - 1] = '\0';
    e->type = MPT_NONE;
    return e;
}

bool Preferences::isKey(const char* key) {
    _mockPrefReadCount++;
    return find(key) != nullptr;
}

bool Preferences::getBool(const char* key, bool def) {
    _mockPrefReadCount++;
    MockPrefEntry* e = find(key);
    return (e && e->type == MPT_BOOL) ? e->v.b : def;
}
int32_t Preferences::getInt(const char* key, int32_t def) {
    _mockPrefReadCount++;
    MockPrefEntry* e = find(key);
    return (e && e->type == MPT_INT) ? e->v.i : def;
}
uint32_t Preferences::getUInt(const char* key, uint32_t def) {
    _mockPrefReadCount++;
    MockPrefEntry* e = find(key);
    return (e && e->type == MPT_UINT) ? e->v.u : def;
}
unsigned long Preferences::getULong(const char* key, unsigned long def) {
    _mockPrefReadCount++;
    MockPrefEntry* e = find(key);
    return (e && e->type == MPT_ULONG) ? e->v.ul : def;
}
float Preferences::getFloat(const char* key, float def) {
    _mockPrefReadCount++;
    MockPrefEntry* e = find(key);
    return (e && e->type == MPT_FLOAT) ? e->v.f : def;
}
String Preferences::getString(const char* key, const char* def) {
    _mockPrefReadCount++;
    MockPrefEntry* e = find(key);
    return (e && e->type == MPT_STRING) ? String(e->str) : String(def);
}

size_t Preferences::putBool(const char* key, bool v) {
    _mockPrefWriteCount++;
    MockPrefEntry* e = findOrCreate(key);
    if (!e) return 0;
    e->type = MPT_BOOL; e->v.b = v;
    return 1;
}
size_t Preferences::putInt(const char* key, int32_t v) {
    _mockPrefWriteCount++;
    MockPrefEntry* e = findOrCreate(key);
    if (!e) return 0;
    e->type = MPT_INT; e->v.i = v;
    return 4;
}
size_t Preferences::putUInt(const char* key, uint32_t v) {
    _mockPrefWriteCount++;
    MockPrefEntry* e = findOrCreate(key);
    if (!e) return 0;
    e->type = MPT_UINT; e->v.u = v;
    return 4;
}
size_t Preferences::putULong(const char* key, unsigned long v) {
    _mockPrefWriteCount++;
    MockPrefEntry* e = findOrCreate(key);
    if (!e) return 0;
    e->type = MPT_ULONG; e->v.ul = v;
    return 4;
}
size_t Preferences::putFloat(const char* key, float v) {
    _mockPrefWriteCount++;
    MockPrefEntry* e = findOrCreate(key);
    if (!e) return 0;
    e->type = MPT_FLOAT; e->v.f = v;
    return 4;
}
size_t Preferences::putString(const char* key, const char* v) {
    _mockPrefWriteCount++;
    MockPrefEntry* e = findOrCreate(key);
    if (!e) return 0;
    e->type = MPT_STRING;
    strncpy(e->str, v ? v : "", MOCK_PREF_STR_LEN - 1);
    e->str[MOCK_PREF_STR_LEN - 1] = '\0';
    return strlen(e->str);
}
