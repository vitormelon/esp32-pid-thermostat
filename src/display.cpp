#include "display.h"
#include "state.h"
#include "config.h"
#include "autotune.h"
#include "timer_ctrl.h"
#include "storage.h"
#include "control.h"
#include "safety.h"
#include <LiquidCrystal_I2C.h>
#include <stdarg.h>
#include <math.h>

static LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// ============================================================
// ENUMS & STATE
// ============================================================
enum Screen   { SCR_HOME, SCR_GRAPH, SCR_CONFIG, SCR_AUTOTUNE, SCR_PRESETS, SCR_COUNT };
#define SCR_NAV_COUNT 4
enum NavState { NAV_SCREEN, NAV_ITEM, NAV_EDIT,
                NAV_PRESET_ACTION, NAV_CHAR_EDIT,
                NAV_AUTOTUNE_RUN, NAV_AUTOTUNE_RESULT };

static Screen   curScreen  = SCR_HOME;
static NavState navState   = NAV_SCREEN;
static int      selItem    = 0;
static int      scrollOff  = 0;

static bool          blinkOn       = true;
static unsigned long lastBlinkMs   = 0;
static unsigned long lastUpdateMs  = 0;
static bool          backlightState = true;

// Timer edit
static int timerEditPart = 0;   // 0=hours, 1=minutes
static int editHours     = 0;
static int editMinutes   = 0;

// Autotune result choice
static bool atAccept = true;

// Graph
static float         graphHistory[GRAPH_MAX_HISTORY];
static int           graphHistCount = 0;
static int           graphHistHead  = 0;
static unsigned long lastGraphSampleMs = 0;
static int           graphScaleIdx = 0;
static bool          barCharsCreated = false;

// Preset editing
static int  presetSelIdx    = 0;   // index in presets[]
static int  presetActionIdx = 0;
static char editName[PRESET_NAME_MAX_LEN + 1];
static int  editCharPos     = 0;
static int  editPresetSlot  = -1;

// Config items
enum CfgItem {
    CFG_OFFSET, CFG_MODE, CFG_KP, CFG_KI, CFG_KD,
    CFG_WINDOW, CFG_THRESHOLD, CFG_BACKLIGHT, CFG_PRESETS, CFG_COUNT
};

// ============================================================
// CHARSET
// ============================================================
static const char CHARSET[] = CHARSET_STR;

// ============================================================
// UTILITIES
// ============================================================
static void lcdFlush(int row, char* buf) {
    if (lcdFlipped) {
        row = LCD_ROWS - 1 - row;
        for (int i = 0; i < LCD_COLS / 2; i++) {
            char t = buf[i]; buf[i] = buf[LCD_COLS - 1 - i]; buf[LCD_COLS - 1 - i] = t;
        }
    }
    lcd.setCursor(0, row);
    lcd.print(buf);
}

static void lcdFlushBytes(int row, uint8_t* data, int len) {
    int physRow = lcdFlipped ? (LCD_ROWS - 1 - row) : row;
    lcd.setCursor(lcdFlipped ? (LCD_COLS - len) : 0, physRow);
    if (lcdFlipped) {
        for (int i = len - 1; i >= 0; i--) lcd.write(data[i]);
    } else {
        for (int i = 0; i < len; i++) lcd.write(data[i]);
    }
}

static void lcdLine(int row, const char* fmt, ...) {
    char buf[LCD_COLS + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int len = strlen(buf);
    while (len < LCD_COLS) buf[len++] = ' ';
    buf[LCD_COLS] = '\0';
    lcdFlush(row, buf);
}

static void lcdLR(int row, const char* left, const char* right) {
    char buf[LCD_COLS + 1];
    int ll = strlen(left), rl = strlen(right);
    int pad = LCD_COLS - ll - rl;
    if (pad < 0) pad = 0;
    memcpy(buf, left, ll);
    memset(buf + ll, ' ', pad);
    memcpy(buf + ll + pad, right, rl);
    buf[LCD_COLS] = '\0';
    lcdFlush(row, buf);
}

static const char* modeName(int m) {
    switch (m) {
        case MODE_HYSTERESIS: return "Histerese";
        case MODE_PID_ONOFF:  return "PID On/Of";
        case MODE_PID_WINDOW: return "PID Janel";
        default:              return "???";
    }
}

static void formatTimer(char* buf, int sz) {
    if (timerSetMinutes == 0) {
        snprintf(buf, sz, "--:--");
    } else if (timerRunning) {
        unsigned long s = timerRemainingMs / 1000;
        snprintf(buf, sz, "%02lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
    } else {
        snprintf(buf, sz, "%02d:%02d", timerSetMinutes / 60, timerSetMinutes % 60);
    }
}

// ============================================================
// CUSTOM BAR CHARS (graph)
// ============================================================
static void createBarChars() {
    for (int level = 1; level <= 7; level++) {
        byte data[8];
        for (int r = 0; r < 8; r++) {
            if (lcdFlipped)
                data[r] = (r < level) ? 0x1F : 0x00;
            else
                data[r] = (r >= (8 - level)) ? 0x1F : 0x00;
        }
        lcd.createChar(level - 1, data);
    }
    barCharsCreated = true;
}

// ============================================================
// GRAPH DATA
// ============================================================
void displayGraphSample() {
    if (millis() - lastGraphSampleMs < GRAPH_SAMPLE_INTERVAL) return;
    lastGraphSampleMs = millis();
    if (!firstValidReading) return;

    int idx;
    if (graphHistCount < GRAPH_MAX_HISTORY) {
        idx = graphHistCount++;
    } else {
        idx = graphHistHead;
        graphHistHead = (graphHistHead + 1) % GRAPH_MAX_HISTORY;
    }
    graphHistory[idx] = currentTemp;
}

static void getGraphData(float* data, int cols) {
    unsigned long scaleSecs = GRAPH_SCALE_SECS[graphScaleIdx];
    int sampleIntS = GRAPH_SAMPLE_INTERVAL / 1000;
    int samplesPerCol = max(1, (int)(scaleSecs / sampleIntS / cols));

    for (int c = 0; c < cols; c++) {
        float sum = 0;
        int cnt = 0;
        for (int s = 0; s < samplesPerCol; s++) {
            int fromNew = (cols - 1 - c) * samplesPerCol + s;
            if (fromNew >= graphHistCount) continue;
            int bi = (graphHistHead + graphHistCount - 1 - fromNew + GRAPH_MAX_HISTORY) % GRAPH_MAX_HISTORY;
            sum += graphHistory[bi];
            cnt++;
        }
        data[c] = cnt > 0 ? sum / cnt : NAN;
    }
}

// ============================================================
// CONFIG HELPERS
// ============================================================
static bool cfgVisible(int i) {
    bool pid = (controlMode == MODE_PID_ONOFF || controlMode == MODE_PID_WINDOW);
    switch (i) {
        case CFG_KP: case CFG_KI: case CFG_KD: return pid;
        case CFG_WINDOW:    return controlMode == MODE_PID_WINDOW;
        case CFG_THRESHOLD: return controlMode == MODE_PID_ONOFF;
        case CFG_PRESETS:   return true;
        default: return true;
    }
}

static int cfgVisibleList[CFG_COUNT];
static int cfgVisibleN = 0;

static void cfgBuildList() {
    cfgVisibleN = 0;
    for (int i = 0; i < CFG_COUNT; i++)
        if (cfgVisible(i)) cfgVisibleList[cfgVisibleN++] = i;
}

static void cfgRenderItem(int item, char* buf, int sz, bool editing) {
    switch (item) {
        case CFG_OFFSET:
            if (editing) snprintf(buf, sz, "Offset:[%.1f]%cC", offset, (char)0xDF);
            else         snprintf(buf, sz, "Offset: %.1f%cC", offset, (char)0xDF);
            break;
        case CFG_MODE:
            if (editing) snprintf(buf, sz, "Modo:[%s]", modeName(controlMode));
            else         snprintf(buf, sz, "Modo: %s", modeName(controlMode));
            break;
        case CFG_KP:
            if (editing) snprintf(buf, sz, "Kp:[%.3f]", pidKp);
            else         snprintf(buf, sz, "Kp: %.3f", pidKp);
            break;
        case CFG_KI:
            if (editing) snprintf(buf, sz, "Ki:[%.4f]", pidKi);
            else         snprintf(buf, sz, "Ki: %.4f", pidKi);
            break;
        case CFG_KD:
            if (editing) snprintf(buf, sz, "Kd:[%.1f]", pidKd);
            else         snprintf(buf, sz, "Kd: %.1f", pidKd);
            break;
        case CFG_WINDOW:
            if (editing) snprintf(buf, sz, "Janela:[%d]s", (int)(pidWindowSize / 1000));
            else         snprintf(buf, sz, "Janela: %ds", (int)(pidWindowSize / 1000));
            break;
        case CFG_THRESHOLD:
            if (editing) snprintf(buf, sz, "Threshold:[%.0f]%%", pidThreshold);
            else         snprintf(buf, sz, "Threshold: %.0f%%", pidThreshold);
            break;
        case CFG_BACKLIGHT:
            if (editing) snprintf(buf, sz, "BL:[%s]", BL_TIMEOUT_NAMES[backlightTimeoutIndex]);
            else         snprintf(buf, sz, "Backlight: %s", BL_TIMEOUT_NAMES[backlightTimeoutIndex]);
            break;
        case CFG_PRESETS:
            snprintf(buf, sz, ">> Presets <<");
            break;
    }
}

// ============================================================
// PRESET HELPERS
// ============================================================
static int presetUsedCount() {
    int n = 0;
    for (int i = 0; i < MAX_PRESETS; i++) if (presets[i].used) n++;
    return n;
}

static int presetNthUsedIdx(int n) {
    int c = 0;
    for (int i = 0; i < MAX_PRESETS; i++) {
        if (presets[i].used) {
            if (c == n) return i;
            c++;
        }
    }
    return -1;
}

static int presetFirstFreeSlot() {
    for (int i = 0; i < MAX_PRESETS; i++) if (!presets[i].used) return i;
    return -1;
}

// ============================================================
// SCREEN RENDERERS
// ============================================================

static void renderHome() {
    // Line 0: temperature + uptime + relay
    char left0[15];
    snprintf(left0, sizeof(left0), "%5.1f%cC", currentTemp, (char)0xDF);
    char right0[12];
    if (systemActive) {
        unsigned long el = (millis() - systemStartMs) / 1000;
        int h = el / 3600, m = (el % 3600) / 60, s = el % 60;
        snprintf(right0, sizeof(right0), "%02d:%02d:%02d %s", h, m, s, relayState ? "ON" : "OF");
    } else {
        snprintf(right0, sizeof(right0), "---");
    }
    lcdLR(0, left0, right0);

    // Line 1: setpoint + mode
    char sp[12], mode[11];
    bool editSP = (navState == NAV_EDIT && selItem == 0);
    if (editSP) {
        snprintf(sp, sizeof(sp), "SP[%5.1f]", setPoint);
    } else {
        snprintf(sp, sizeof(sp), "SP:%5.1f", setPoint);
    }
    if (controlMode == MODE_HYSTERESIS) {
        snprintf(mode, sizeof(mode), "Histerese");
    } else {
        snprintf(mode, sizeof(mode), "PID  %3.0f%%", pidOutput);
    }
    char cursor1 = (navState >= NAV_ITEM && selItem == 0) ? '>' : ' ';
    char ln1[21];
    snprintf(ln1, 21, "%c%-9s %9s", cursor1, sp, mode);
    lcdLine(1, "%s", ln1);

    // Line 2: timer
    char tmr[12];
    bool editTmr = (navState == NAV_EDIT && selItem == 1);
    if (editTmr) {
        if (timerEditPart == 0) {
            snprintf(tmr, sizeof(tmr), "[%02d]:%02d", editHours, editMinutes);
        } else {
            snprintf(tmr, sizeof(tmr), "%02d:[%02d]", editHours, editMinutes);
        }
    } else {
        formatTimer(tmr, sizeof(tmr));
    }
    char cursor2 = (navState >= NAV_ITEM && selItem == 1) ? '>' : ' ';
    lcdLine(2, "%cTimer: %-12s", cursor2, tmr);

    // Line 3: start/stop + preset
    char cursor3 = (navState >= NAV_ITEM && selItem == 2) ? '>' : ' ';
    const char* action = systemActive ? "[Parar]" : "[Iniciar]";
    char presetName[PRESET_NAME_MAX_LEN + 1] = "";
    if (activePresetIndex >= 0 && activePresetIndex < MAX_PRESETS && presets[activePresetIndex].used) {
        strncpy(presetName, presets[activePresetIndex].name, PRESET_NAME_MAX_LEN);
    }
    char ln3[21];
    snprintf(ln3, 21, "%c%-9s %9s", cursor3, action, presetName);
    lcdLine(3, "%s", ln3);
}

static void renderGraph() {
    if (!barCharsCreated) createBarChars();

    float data[GRAPH_DATA_COLS];
    getGraphData(data, GRAPH_DATA_COLS);

    float rMin = setPoint, rMax = setPoint;
    for (int i = 0; i < GRAPH_DATA_COLS; i++) {
        if (!isnan(data[i])) {
            if (data[i] < rMin) rMin = data[i];
            if (data[i] > rMax) rMax = data[i];
        }
    }
    float pad = max(2.0f, (rMax - rMin) * 0.15f);
    rMin -= pad;
    rMax += pad;
    float range = rMax - rMin;
    if (range < 5.0f) { rMin -= 2.5f; rMax += 2.5f; range = rMax - rMin; }

    int spPx = constrain((int)((setPoint - rMin) / range * 24.0f), 0, 23);
    int spRow = 2 - (spPx / 8);
    int spLocalPx = spPx % 8;
    int spCharRow = 7 - spLocalPx;

    byte spDash[8] = {0};
    spDash[spCharRow] = 0x1F;
    lcd.createChar(7, spDash);

    float scaleVals[3] = {rMax, (rMax + rMin) / 2.0f, rMin};

    for (int row = 0; row < 3; row++) {
        uint8_t rowBuf[LCD_COLS];
        memset(rowBuf, ' ', LCD_COLS);

        char sc[5];
        char ind = (row == spRow) ? '>' : ' ';
        snprintf(sc, 5, "%c%3.0f", ind, scaleVals[row]);
        for (int i = 0; i < 4 && sc[i]; i++) rowBuf[i] = (uint8_t)sc[i];

        // Primeira coluna: marcador do SP
        rowBuf[4] = (row == spRow) ? 7 : ' ';

        // Colunas de dados (5-19)
        for (int c = 0; c < GRAPH_DATA_COLS - 1; c++) {
            if (isnan(data[c])) continue;
            int barPx = constrain((int)((data[c] - rMin) / range * 24.0f), 0, 24);
            int rowBot = (2 - row) * 8;
            int fill = barPx - rowBot;
            if (fill <= 0)      rowBuf[5 + c] = ' ';
            else if (fill >= 8) rowBuf[5 + c] = 0xFF;
            else                rowBuf[5 + c] = (uint8_t)(fill - 1);
        }

        lcdFlushBytes(row, rowBuf, LCD_COLS);
    }

    // Line 3: time axis
    const char* sn = GRAPH_SCALE_NAMES[graphScaleIdx];
    bool hl = (navState == NAV_ITEM || (navState == NAV_EDIT && blinkOn));
    if (navState >= NAV_ITEM) {
        lcdLine(3, "    0         [%3s] ", sn);
    } else {
        lcdLine(3, "    0          %4s  ", sn);
    }
}

static void renderConfig() {
    cfgBuildList();
    lcdLine(0, "  Configuracoes     ");

    for (int line = 1; line <= 3; line++) {
        int idx = scrollOff + (line - 1);
        if (idx >= cfgVisibleN) {
            lcdLine(line, "");
            continue;
        }
        int item = cfgVisibleList[idx];
        bool isSel = (navState >= NAV_ITEM && selItem == idx);
        bool isEdit = (navState == NAV_EDIT && selItem == idx);
        char content[20];
        cfgRenderItem(item, content, sizeof(content), isEdit);
        char cursor = isSel ? '>' : ' ';
        lcdLine(line, "%c%s", cursor, content);
    }
}

static void renderPresets() {
    if (navState == NAV_PRESET_ACTION) {
        // Action submenu
        const char* actions[] = {"Carregar", "Salvar Aqui", "Renomear", "Excluir"};
        for (int i = 0; i < 4; i++) {
            char c = (presetActionIdx == i) ? '>' : ' ';
            lcdLine(i, "%c%s", c, actions[i]);
        }
        return;
    }

    if (navState == NAV_CHAR_EDIT) {
        lcdLine(0, "  Nome do Preset    ");
        // Name with blinking char
        char nameBuf[21];
        char display[PRESET_NAME_MAX_LEN + 1];
        memcpy(display, editName, PRESET_NAME_MAX_LEN + 1);
        if (editCharPos < PRESET_NAME_MAX_LEN && !blinkOn) {
            display[editCharPos] = '_';
        }
        // Pad with underscores for unused positions
        for (int i = strlen(editName); i < PRESET_NAME_MAX_LEN; i++) {
            if (i != editCharPos || blinkOn) display[i] = '_';
        }
        display[PRESET_NAME_MAX_LEN] = '\0';
        bool okSel = (editCharPos >= PRESET_NAME_MAX_LEN);
        snprintf(nameBuf, 21, " %s %s", display, okSel ? "[OK]" : " OK ");
        lcdLine(2, "%s", nameBuf);
        // Cursor line
        char curLine[21];
        memset(curLine, ' ', 20);
        curLine[20] = '\0';
        int curCol = (editCharPos < PRESET_NAME_MAX_LEN) ? (editCharPos + 1) : 15;
        curLine[curCol] = '^';
        lcdLine(3, "%s", curLine);
        lcdLine(1, "");
        return;
    }

    lcdLine(0, "  Presets            ");
    int usedN = presetUsedCount();
    int totalItems = usedN + 1; // +1 for [+Novo]

    for (int line = 1; line <= 3; line++) {
        int idx = scrollOff + (line - 1);
        if (idx >= totalItems) { lcdLine(line, ""); continue; }

        char c = (navState >= NAV_ITEM && selItem == idx) ? '>' : ' ';
        if (idx < usedN) {
            int pi = presetNthUsedIdx(idx);
            const char* mark = (pi == activePresetIndex) ? " [*]" : "";
            lcdLine(line, "%c%s%s", c, presets[pi].name, mark);
        } else {
            lcdLine(line, "%c[+Novo]", c);
        }
    }
}

static void renderAutotuneRun() {
    int cycle = autotuneGetCycle();
    int total = autotuneGetTotalCycles();
    unsigned long eta   = autotuneGetEtaMs();
    unsigned long last  = autotuneGetLastCycleDurationMs();
    unsigned long cur   = autotuneGetCurrentCycleElapsedMs();
    unsigned long tot   = autotuneGetTotalElapsedMs();

    lcdLine(0, "T:%.1f SP:%.1f %d/%d", currentTemp, setPoint, cycle, total);

    if (eta > 0) {
        int em = (eta/1000)/60, es = (eta/1000)%60;
        lcdLine(1, "ETA: %02d:%02d", em, es);
    } else {
        lcdLine(1, "ETA: --:--");
    }

    int lm=(last/1000)/60, ls=(last/1000)%60;
    int cm=(cur/1000)/60,  cs=(cur/1000)%60;
    if (last > 0) {
        lcdLine(2, "Ant:%02d:%02d Atu:%02d:%02d", lm, ls, cm, cs);
    } else {
        lcdLine(2, "Atu: %02d:%02d", cm, cs);
    }

    int tm=(tot/1000)/60, ts=(tot/1000)%60;
    lcdLine(3, "Total: %02d:%02d", tm, ts);
}

static void renderAutotuneResult() {
    lcdLine(0, " AUTO-TUNE concluido");
    lcdLine(1, " Kp=%-7.3f Ki=%.4f", autotuneGetSuggestedKp(), autotuneGetSuggestedKi());
    lcdLine(2, " Kd=%-7.1f", autotuneGetSuggestedKd());
    if (atAccept) {
        lcdLine(3, " [Aceitar]  Rejeitar");
    } else {
        lcdLine(3, "  Aceitar  [Rejeitar]");
    }
}

static void renderAutotuneScreen() {
    bool pid = (controlMode == MODE_PID_ONOFF || controlMode == MODE_PID_WINDOW);
    lcdLine(0, "    Auto-Tune       ");
    if (!pid) {
        lcdLine(1, " Disponivel apenas  ");
        lcdLine(2, " no modo PID.       ");
        lcdLine(3, "");
    } else {
        lcdLine(1, " SP:%.1f%cC Kp=%.2f", setPoint, (char)0xDF, pidKp);
        lcdLine(2, " Ki=%.4f Kd=%.1f", pidKi, pidKd);
        char c = (navState == NAV_ITEM) ? '>' : ' ';
        lcdLine(3, "%c[Iniciar]", c);
    }
}

// ============================================================
// INPUT HANDLERS
// ============================================================

static void handleAutotuneScreenInput(EncoderInput in) {
    bool pid = (controlMode == MODE_PID_ONOFF || controlMode == MODE_PID_WINDOW);
    if (!pid) return;

    if (navState == NAV_ITEM && in.pressed) {
        navState = NAV_AUTOTUNE_RUN;
        autotuneStart();
        systemActive = true;
        systemStartMs = millis();
    }
}

static void handleHomeInput(EncoderInput in) {
    const int ITEMS = 3;
    if (navState == NAV_ITEM) {
        if (in.delta) selItem = (selItem + (in.delta > 0 ? 1 : -1) + ITEMS) % ITEMS;
        if (in.pressed) {
            if (selItem == 2) {
                systemActive = !systemActive;
                if (systemActive) {
                    systemStartMs = millis();
                    controlReset();
                    if (timerSetMinutes > 0) timerStart();
                } else {
                    setRelay(false);
                    timerStop();
                }
                storageSaveRecoveryState();
            } else {
                navState = NAV_EDIT;
                if (selItem == 1) {
                    editHours = timerSetMinutes / 60;
                    editMinutes = timerSetMinutes % 60;
                    timerEditPart = 0;
                }
            }
        }
    } else if (navState == NAV_EDIT) {
        if (selItem == 0) {
            if (in.delta) {
                setPoint = constrain(setPoint + in.delta * SP_STEP, SP_MIN, SP_MAX);
            }
            if (in.pressed) {
                storageSaveSetPoint();
                navState = NAV_ITEM;
            }
        } else if (selItem == 1) {
            if (timerEditPart == 0) {
                if (in.delta) editHours = constrain(editHours + in.delta, 0, 24);
                if (in.pressed) timerEditPart = 1;
            } else {
                if (in.delta) {
                    editMinutes += in.delta;
                    if (editMinutes < 0) editMinutes = 59;
                    if (editMinutes > 59) editMinutes = 0;
                }
                if (in.pressed) {
                    timerSetMinutes = editHours * 60 + editMinutes;
                    if (timerSetMinutes > MAX_TIMER_MINUTES) timerSetMinutes = MAX_TIMER_MINUTES;
                    storageSaveTimerMinutes();
                    navState = NAV_ITEM;
                }
            }
        }
    }
}

static void handleGraphInput(EncoderInput in) {
    if (navState == NAV_ITEM || navState == NAV_EDIT) {
        if (in.delta) {
            graphScaleIdx = (graphScaleIdx + (in.delta > 0 ? 1 : -1) + GRAPH_SCALE_COUNT) % GRAPH_SCALE_COUNT;
            storageSaveGraphScale(graphScaleIdx);
        }
        if (in.pressed) navState = (navState == NAV_ITEM) ? NAV_EDIT : NAV_ITEM;
    }
}

static void handleConfigInput(EncoderInput in) {
    cfgBuildList();

    if (navState == NAV_ITEM) {
        if (in.delta) {
            selItem = (selItem + (in.delta > 0 ? 1 : -1) + cfgVisibleN) % cfgVisibleN;
            if (selItem < scrollOff) scrollOff = selItem;
            if (selItem > scrollOff + 2) scrollOff = selItem - 2;
        }
        if (in.pressed) {
            int item = cfgVisibleList[selItem];
            if (item == CFG_PRESETS) {
                curScreen = SCR_PRESETS;
                navState  = NAV_ITEM;
                selItem   = 0;
                scrollOff = 0;
            } else {
                navState = NAV_EDIT;
            }
        }
    } else if (navState == NAV_EDIT) {
        int item = cfgVisibleList[selItem];
        int d = in.delta;
        if (d) {
            switch (item) {
                case CFG_OFFSET:
                    offset = constrain(offset + d * OFFSET_STEP, OFFSET_MIN, OFFSET_MAX);
                    break;
                case CFG_MODE:
                    controlMode = ((controlMode + (d > 0 ? 1 : -1)) + NUM_MODES) % NUM_MODES;
                    controlReset();
                    cfgBuildList();
                    break;
                case CFG_KP:
                    pidKp = constrain(pidKp + d * PID_KP_STEP, PID_KP_MIN, PID_KP_MAX);
                    break;
                case CFG_KI:
                    pidKi = constrain(pidKi + d * PID_KI_STEP, PID_KI_MIN, PID_KI_MAX);
                    break;
                case CFG_KD:
                    pidKd = constrain(pidKd + d * PID_KD_STEP, PID_KD_MIN, PID_KD_MAX);
                    break;
                case CFG_WINDOW: {
                    long ws = (long)(pidWindowSize / 1000) + d * PID_WINDOW_STEP_S;
                    ws = constrain(ws, (long)PID_WINDOW_MIN_S, (long)PID_WINDOW_MAX_S);
                    pidWindowSize = (unsigned long)ws * 1000UL;
                    break;
                }
                case CFG_THRESHOLD:
                    pidThreshold = constrain(pidThreshold + d * PID_THRESHOLD_STEP, PID_THRESHOLD_MIN, PID_THRESHOLD_MAX);
                    break;
                case CFG_BACKLIGHT:
                    backlightTimeoutIndex = (backlightTimeoutIndex + (d > 0 ? 1 : -1) + BL_TIMEOUT_COUNT) % BL_TIMEOUT_COUNT;
                    break;
            }
        }
        if (in.pressed) {
            int item2 = cfgVisibleList[selItem];
            switch (item2) {
                case CFG_OFFSET:    storageSaveOffset(); break;
                case CFG_MODE:      storageSaveControlMode(); break;
                case CFG_KP:        storageSavePidKp(); controlReset(); break;
                case CFG_KI:        storageSavePidKi(); controlReset(); break;
                case CFG_KD:        storageSavePidKd(); controlReset(); break;
                case CFG_WINDOW:    storageSavePidWindow(); controlReset(); break;
                case CFG_THRESHOLD: storageSavePidThreshold(); break;
                case CFG_BACKLIGHT: storageSaveBacklightTimeout(); break;
            }
            navState = NAV_ITEM;
        }
    }
}

static void handlePresetsInput(EncoderInput in) {
    int usedN = presetUsedCount();
    int totalItems = usedN + 1;

    if (navState == NAV_PRESET_ACTION) {
        if (in.delta) presetActionIdx = (presetActionIdx + (in.delta > 0 ? 1 : -1) + 4) % 4;
        if (in.pressed) {
            int pi = presetNthUsedIdx(presetSelIdx);
            switch (presetActionIdx) {
                case 0: // Carregar
                    if (pi >= 0) {
                        pidKp = presets[pi].kp;
                        pidKi = presets[pi].ki;
                        pidKd = presets[pi].kd;
                        pidWindowSize = presets[pi].windowMs;
                        activePresetIndex = pi;
                        controlReset();
                        storageSavePidKp();
                        storageSavePidKi();
                        storageSavePidKd();
                        storageSavePidWindow();
                        Serial.printf("[PRESET] Carregado: '%s'\n", presets[pi].name);
                    }
                    navState = NAV_ITEM;
                    break;
                case 1: // Salvar Aqui
                    if (pi >= 0) {
                        presets[pi].kp = pidKp;
                        presets[pi].ki = pidKi;
                        presets[pi].kd = pidKd;
                        presets[pi].windowMs = pidWindowSize;
                        storageSavePreset(pi);
                    }
                    navState = NAV_ITEM;
                    break;
                case 2: // Renomear
                    if (pi >= 0) {
                        editPresetSlot = pi;
                        memset(editName, ' ', PRESET_NAME_MAX_LEN);
                        strncpy(editName, presets[pi].name, PRESET_NAME_MAX_LEN);
                        editName[PRESET_NAME_MAX_LEN] = '\0';
                        editCharPos = 0;
                        navState = NAV_CHAR_EDIT;
                    }
                    break;
                case 3: // Excluir
                    if (pi >= 0) storageDeletePreset(pi);
                    navState = NAV_ITEM;
                    selItem = 0;
                    scrollOff = 0;
                    break;
            }
        }
        if (in.longPress) navState = NAV_ITEM;
        return;
    }

    if (navState == NAV_CHAR_EDIT) {
        if (in.delta && editCharPos < PRESET_NAME_MAX_LEN) {
            // Cycle character
            const char* cp = strchr(CHARSET, editName[editCharPos]);
            int ci = cp ? (int)(cp - CHARSET) : 0;
            ci = (ci + (in.delta > 0 ? 1 : -1) + CHARSET_LEN) % CHARSET_LEN;
            editName[editCharPos] = CHARSET[ci];
        }
        if (in.pressed) {
            editCharPos++;
            if (editCharPos > PRESET_NAME_MAX_LEN) {
                // Confirm name
                // Trim trailing spaces
                int len = PRESET_NAME_MAX_LEN;
                while (len > 0 && editName[len - 1] == ' ') len--;
                editName[len] = '\0';
                if (len > 0 && editPresetSlot >= 0) {
                    strncpy(presets[editPresetSlot].name, editName, PRESET_NAME_MAX_LEN);
                    presets[editPresetSlot].name[PRESET_NAME_MAX_LEN] = '\0';
                    storageSavePreset(editPresetSlot);
                }
                navState = NAV_ITEM;
            }
        }
        if (in.longPress) {
            // Cancel
            navState = NAV_ITEM;
        }
        return;
    }

    if (navState == NAV_ITEM) {
        if (in.delta) {
            selItem = (selItem + (in.delta > 0 ? 1 : -1) + totalItems) % totalItems;
            if (selItem < scrollOff) scrollOff = selItem;
            if (selItem > scrollOff + 2) scrollOff = selItem - 2;
        }
        if (in.pressed) {
            if (selItem < usedN) {
                presetSelIdx = selItem;
                presetActionIdx = 0;
                navState = NAV_PRESET_ACTION;
            } else {
                // [+Novo]
                int slot = presetFirstFreeSlot();
                if (slot >= 0) {
                    presets[slot].used = true;
                    presets[slot].kp = pidKp;
                    presets[slot].ki = pidKi;
                    presets[slot].kd = pidKd;
                    presets[slot].windowMs = pidWindowSize;
                    memset(presets[slot].name, ' ', PRESET_NAME_MAX_LEN);
                    presets[slot].name[PRESET_NAME_MAX_LEN] = '\0';
                    editPresetSlot = slot;
                    memset(editName, ' ', PRESET_NAME_MAX_LEN);
                    editName[PRESET_NAME_MAX_LEN] = '\0';
                    editCharPos = 0;
                    navState = NAV_CHAR_EDIT;
                }
            }
        }
    }
}

static void handleAutotuneInput(EncoderInput in) {
    if (navState == NAV_AUTOTUNE_RUN) {
        // Check if autotune finished
        if (autotuneGetState() == AT_DONE) {
            navState = NAV_AUTOTUNE_RESULT;
            atAccept = true;
        }
    } else if (navState == NAV_AUTOTUNE_RESULT) {
        if (in.delta) atAccept = !atAccept;
        if (in.pressed) {
            if (atAccept) {
                pidKp = autotuneGetSuggestedKp();
                pidKi = autotuneGetSuggestedKi();
                pidKd = autotuneGetSuggestedKd();
                storageSavePidKp();
                storageSavePidKi();
                storageSavePidKd();
                controlReset();
                Serial.println("[AUTOTUNE] Parametros aceitos");
            } else {
                Serial.println("[AUTOTUNE] Parametros rejeitados");
            }
            systemActive = false;
            autotuneReset();
            navState  = NAV_SCREEN;
            curScreen = SCR_AUTOTUNE;
        }
    }
}

// ============================================================
// RECOVERY SCREEN (não bloqueante)
// ============================================================

static void renderRecovery() {
    long elapsed = (long)(millis() - recoveryStartMs);
    int rem = (int)((RECOVERY_TIMEOUT_MS - elapsed) / 1000);
    if (rem < 0) rem = 0;

    lcdLine(0, "Ciclo interrompido!");
    lcdLine(1, "Continuar?     %2ds", rem);
    lcdLine(2, "");
    if (recoveryChoice)
        lcdLine(3, "  [SIM]       NAO   ");
    else
        lcdLine(3, "   SIM       [NAO]  ");
}

// ============================================================
// SAFETY ERROR SCREEN
// ============================================================

static void renderSafetyError() {
    bool safetyBlink = ((millis() / 1000) % 2) == 0;
    displaySetBacklight(safetyBlink);

    switch (safetyError) {
        case SAFETY_OVERTEMP:
            lcdLine(0, "!! SOBRETEMPERATURA!");
            break;
        case SAFETY_SENSOR_FAIL:
            lcdLine(0, "!! FALHA NO SENSOR!");
            break;
        case SAFETY_RELAY_STUCK:
            lcdLine(0, "!! RELE TRAVADO !!");
            break;
        default:
            lcdLine(0, "!! ERRO !!");
            break;
    }

    if (safetyError == SAFETY_SENSOR_FAIL) {
        lcdLine(1, " Sem leitura >5s");
        lcdLine(2, " Ultima: %.1f%cC", safetyTriggerTemp, (char)0xDF);
    } else {
        lcdLine(1, " Disparou: %.1f%cC", safetyTriggerTemp, (char)0xDF);
        lcdLine(2, " Atual:    %.1f%cC", currentTemp, (char)0xDF);
    }

    lcdLine(3, " [Click para limpar]");
}

bool displayIsSafetyScreen() {
    return safetyError != SAFETY_OK;
}

// ============================================================
// PUBLIC API
// ============================================================

void displayInit() {
    lcd.init();
    lcd.backlight();
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
    backlightState = true;

    curScreen     = SCR_HOME;
    navState      = NAV_SCREEN;
    selItem       = 0;
    graphScaleIdx = storageLoadGraphScale();

    lcd.clear();
    lcdLine(0, "  PID THERMOSTAT    ");
    lcdLine(1, "   Iniciando...     ");
    lcdLine(2, "");
    lcdLine(3, "");
    Serial.println("[LCD] Display inicializado");
}

void displayUpdate() {
    if (millis() - lastUpdateMs < DISPLAY_UPDATE_MS) return;
    lastUpdateMs = millis();

    if (millis() - lastBlinkMs >= BLINK_INTERVAL_MS) {
        blinkOn = !blinkOn;
        lastBlinkMs = millis();
    }

    if (safetyError != SAFETY_OK) {
        renderSafetyError();
        return;
    }

    if (recoveryPending) {
        renderRecovery();
        return;
    }

    if (navState == NAV_AUTOTUNE_RUN) {
        AutotuneState atSt = autotuneGetState();
        if (atSt == AT_DONE) {
            navState = NAV_AUTOTUNE_RESULT;
            atAccept = true;
        } else if (atSt == AT_CANCELLED || atSt == AT_IDLE) {
            navState  = NAV_SCREEN;
            curScreen = SCR_AUTOTUNE;
            return;
        }
        renderAutotuneRun();
        return;
    }
    if (navState == NAV_AUTOTUNE_RESULT) {
        renderAutotuneResult();
        return;
    }

    switch (curScreen) {
        case SCR_HOME:     renderHome();          break;
        case SCR_GRAPH:    renderGraph();          break;
        case SCR_CONFIG:   renderConfig();         break;
        case SCR_AUTOTUNE: renderAutotuneScreen(); break;
        case SCR_PRESETS:  renderPresets();         break;
        default: break;
    }
}

void displayResetNavToScreen() {
    if (navState != NAV_AUTOTUNE_RUN && navState != NAV_AUTOTUNE_RESULT) {
        navState  = NAV_SCREEN;
        selItem   = 0;
        scrollOff = 0;
    }
}

void displayResetAutotuneUI() {
    if (navState == NAV_AUTOTUNE_RUN || navState == NAV_AUTOTUNE_RESULT) {
        navState  = NAV_SCREEN;
        curScreen = SCR_AUTOTUNE;
        selItem   = 0;
        scrollOff = 0;
    }
}

void displayHandleInput(EncoderInput in) {
    // Safety error: click ou long press limpa o erro
    if (safetyError != SAFETY_OK) {
        if (in.pressed || in.longPress) {
            safetyClear();
            displaySetBacklight(true);
            curScreen = SCR_HOME;
            navState  = NAV_SCREEN;
            selItem   = 0;
        }
        return;
    }

    // Recovery pendente: encoder escolhe SIM/NAO, click confirma
    if (recoveryPending) {
        if (in.delta) recoveryChoice = !recoveryChoice;
        if (in.pressed) {
            recoveryDecisionResume = recoveryChoice;
            recoveryDecisionMade   = true;
        }
        return;
    }

    if (in.longPress) {
        if (navState == NAV_AUTOTUNE_RUN || navState == NAV_AUTOTUNE_RESULT) {
            if (navState == NAV_AUTOTUNE_RUN) autotuneCancel();
            else autotuneReset();
            systemActive = false;
            navState  = NAV_SCREEN;
            curScreen = SCR_AUTOTUNE;
            return;
        }
        if (curScreen == SCR_PRESETS) {
            curScreen  = SCR_CONFIG;
            navState   = NAV_ITEM;
            selItem    = 0;
            scrollOff  = 0;
            return;
        }
        if (navState == NAV_SCREEN) {
            curScreen = SCR_HOME;
            return;
        }
        navState  = NAV_SCREEN;
        selItem   = 0;
        scrollOff = 0;
        return;
    }

    if (navState == NAV_AUTOTUNE_RUN || navState == NAV_AUTOTUNE_RESULT) {
        handleAutotuneInput(in);
        return;
    }

    if (navState == NAV_SCREEN) {
        if (in.delta) {
            curScreen = (Screen)(((int)curScreen + (in.delta > 0 ? 1 : -1) + SCR_NAV_COUNT) % SCR_NAV_COUNT);
            selItem   = 0;
            scrollOff = 0;
            barCharsCreated = false;
        }
        if (in.pressed) {
            navState  = NAV_ITEM;
            scrollOff = 0;
            if (curScreen == SCR_HOME) {
                selItem = 2;
            } else {
                selItem = 0;
            }
        }
        return;
    }

    switch (curScreen) {
        case SCR_HOME:     handleHomeInput(in);           break;
        case SCR_GRAPH:    handleGraphInput(in);           break;
        case SCR_CONFIG:   handleConfigInput(in);          break;
        case SCR_AUTOTUNE: handleAutotuneScreenInput(in);  break;
        case SCR_PRESETS:  handlePresetsInput(in);         break;
        default: break;
    }
}

void displaySetBacklight(bool on) {
    backlightState = on;
    digitalWrite(LCD_BACKLIGHT_PIN, on ? HIGH : LOW);
    if (on) lcd.backlight(); else lcd.noBacklight();
}

bool displayIsBacklightOn() {
    return backlightState;
}

