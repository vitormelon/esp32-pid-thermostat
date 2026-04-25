#pragma once
#include <Arduino.h>

// ============================================================
// PIN DEFINITIONS
// ============================================================
#define SENSOR_PIN          4
#define RELAY_PIN           23
#define LCD_BACKLIGHT_PIN   26
#define ENCODER_CLK_PIN     32
#define ENCODER_DT_PIN      33
#define ENCODER_SW_PIN      25
// I2C usa pinos padrão: SDA=21, SCL=22

// ============================================================
// LCD
// ============================================================
#define LCD_ADDR            0x3F
#define LCD_COLS            20
#define LCD_ROWS            4

// ============================================================
// TEMPERATURE
// ============================================================
#define TEMP_READ_INTERVAL      2000UL
#define TEMP_CONVERSION_DELAY   800UL
#define SENSOR_FAIL_TIMEOUT     5000UL
#define HARD_CUTOFF_TEMP        30.0f
#define CUTOFF_RECOVERY_TEMP    25.0f
#define OVERTEMP_DELAY_MS       60000UL
#define TEMP_MIN_VALID          -50.0f
#define TEMP_MAX_VALID          150.0f

// ============================================================
// CONTROL DEFAULTS & LIMITS
// ============================================================
#define DEFAULT_SETPOINT        80.0f
#define DEFAULT_OFFSET          2.0f
#define SP_MIN                  30.0f
#define SP_MAX                  100.0f
#define SP_STEP                 1.0f
#define OFFSET_MIN              0.5f
#define OFFSET_MAX              10.0f
#define OFFSET_STEP             0.5f

#define MODE_HYSTERESIS         0
#define MODE_PID_ONOFF          1
#define MODE_PID_WINDOW         2
#define NUM_MODES               3

// ============================================================
// PID DEFAULTS & LIMITS
// ============================================================
#define DEFAULT_PID_KP          2.0f
#define DEFAULT_PID_KI          0.01f
#define DEFAULT_PID_KD          50.0f
#define DEFAULT_PID_WINDOW_MS   10000UL
#define DEFAULT_PID_THRESHOLD   50.0f

#define PID_KP_MIN              0.0f
#define PID_KP_MAX              999.0f
#define PID_KP_STEP             0.1f
#define PID_KI_MIN              0.0f
#define PID_KI_MAX              999.0f
#define PID_KI_STEP             0.001f
#define PID_KD_MIN              0.0f
#define PID_KD_MAX              9999.0f
#define PID_KD_STEP             1.0f
#define PID_WINDOW_MIN_S        2
#define PID_WINDOW_MAX_S        300
#define PID_WINDOW_STEP_S       1
#define PID_THRESHOLD_MIN       10.0f
#define PID_THRESHOLD_MAX       90.0f
#define PID_THRESHOLD_STEP      5.0f

// ============================================================
// ENCODER
// ============================================================
#define LONG_PRESS_MS           1000UL
#define BL_WAKE_DELAY_MS        100UL
#define ENCODER_DEBOUNCE_MS     4
#define ENCODER_CLICK_MS        5
#define ENCODER_STEPS_PER_DETENT 4

// ============================================================
// BACKLIGHT
// ============================================================
#define BL_TIMEOUT_COUNT        4
static const unsigned long BL_TIMEOUTS[] = {0, 60000, 300000, 600000};
static const char* const BL_TIMEOUT_NAMES[] = {"Sempre", "1 min", "5 min", "10 min"};

// ============================================================
// WIFI
// ============================================================
#define WIFI_CONNECT_TIMEOUT    15000UL
#define WIFI_RETRY_PAUSE        5000UL
#define WIFI_RETRY_CYCLE_PAUSE  30000UL
#define WIFI_MAX_RETRIES        5
#define WIFI_OFFLINE_RESTART_TIME 300000UL

// ============================================================
// BLYNK
// ============================================================
#define BLYNK_SEND_INTERVAL     2000UL
// Tempo máximo que Blynk.connect() bloqueia o loop. Reduzido de 3s para
// 1s para minimizar latência percebida em outras tarefas (sensor, encoder,
// safety). Trade-off: conexões em redes lentas podem precisar de mais retries.
#define BLYNK_CONNECT_TIMEOUT_MS 1000UL
// Backoff exponencial entre tentativas: começa em 15s, dobra até 5min.
// Reseta para o mínimo quando reconecta.
#define BLYNK_RETRY_INTERVAL_MIN 15000UL
#define BLYNK_RETRY_INTERVAL_MAX 300000UL

// ============================================================
// WATCHDOG
// ============================================================
#define WDT_TIMEOUT_SEC         8

// ============================================================
// RECOVERY
// ============================================================
#define RECOVERY_TIMEOUT_MS     20000UL
#define RECOVERY_SAVE_INTERVAL  30000UL

// ============================================================
// PRESETS
// ============================================================
#define MAX_PRESETS              10
#define PRESET_NAME_MAX_LEN     12

// ============================================================
// GRAPH
// ============================================================
#define GRAPH_DATA_COLS         16
#define GRAPH_DATA_ROWS         3
#define GRAPH_MAX_HISTORY       1440
#define GRAPH_SAMPLE_INTERVAL   30000UL
#define GRAPH_SCALE_COUNT       7
static const unsigned long GRAPH_SCALE_SECS[] = {600, 1800, 3600, 7200, 14400, 28800, 43200};
static const char* const GRAPH_SCALE_NAMES[] = {"10m","30m","1h","2h","4h","8h","12h"};

// ============================================================
// TIMER
// ============================================================
#define MAX_TIMER_MINUTES       1440

// ============================================================
// AUTOTUNE
// ============================================================
#define AUTOTUNE_CYCLES         5

// ============================================================
// SAFETY
// ============================================================
#define SAFETY_STUCK_THRESHOLD  10.0f
#define SAFETY_STUCK_DELAY_MS   30000UL
#define SAFETY_UNSTICK_CYCLES   10

// ============================================================
// DISPLAY
// ============================================================
#define DISPLAY_UPDATE_MS       50UL
#define BLINK_INTERVAL_MS       500UL

// Charset para edição de nomes de presets
#define CHARSET_STR "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
#define CHARSET_LEN 37
