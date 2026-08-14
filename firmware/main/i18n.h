#pragma once
/*
 * Prusa-Touch — localization (i18n).
 *
 * This header only declares the string *keys* and the API. ALL human-readable
 * text — every language, English included — lives in one place: i18n.c.
 * To translate, open i18n.c, copy the [LANG_EN] block, and translate the
 * right-hand sides. Empty/missing entries fall back to English automatically.
 *
 * NOTE on glyphs: the bundled Inter fonts are built for ASCII (0x20-0x7F) only,
 * so accented/Cyrillic/CJK text renders as blank boxes until the fonts are
 * regenerated with a wider range. See the comment at the top of i18n.c.
 */
#include <stdint.h>
#include <stdbool.h>

/* String keys. Order here is irrelevant to the tables (designated initializers),
 * so you may regroup freely. STR_COUNT must stay last. */
typedef enum {
    STR_NONE = 0,
    /* status strip + action buttons */
    STR_READY, STR_OFFLINE, STR_LOCKED, STR_ATTENTION, STR_PRINTING_PAREN,
    STR_PAUSE, STR_RESUME, STR_PREHEAT, STR_MOVE, STR_PROGRESS, STR_WEBCAM,
    STR_AFC, STR_AFC_UNLOAD, STR_AFC_LANE_FMT,
    STR_AFC_LOAD, STR_AFC_EJECT, STR_AFC_EMPTY, STR_AFC_READY,
    STR_AFC_LOADED_FMT, STR_AFC_PREP, STR_AFC_CLEAR, STR_AFC_MOVE_LANE,
    STR_TOOLS, STR_COMING_SOON, STR_CONSOLE, STR_MACROS, STR_TUNE, STR_CALIBRATION,
    STR_TEMPERATURE, STR_ESTOP, STR_UNLOCK_MOTORS, STR_EXTRUDE, STR_FAN,
    STR_SPEED_FACTOR, STR_FLOW_FACTOR, STR_ENDSTOPS, STR_AUTO_PID, STR_Z_OFFSET,
    STR_BED_MESH, STR_SAVE_CONFIG, STR_KLIPPER_RESTART, STR_FW_RESTART,
    /* nav / sources */
    STR_PRINTER, STR_USB, STR_FILE, STR_CONNECT,
    /* settings labels */
    STR_THEME, STR_SCREEN_ORIENTATION, STR_AUTO_FW_UPDATES, STR_LANGUAGE,
    STR_FW_FMT, STR_NETWORK_FMT, STR_NETWORK_SCAN,
    /* files screen */
    STR_FILES_ON_FMT, STR_FILES_ON_2_FMT, STR_FILES_ON_THIS,
    STR_LOCAL_FILES_USB, STR_NO_PRINTABLE,
    /* empty / loading / placeholder states */
    STR_NO_ACTIVE_PRINT, STR_NO_PREVIEW, STR_PREVIEW_UNAVAIL, STR_LOADING_PREVIEW,
    STR_LOADING, STR_LOADING_WEBCAM, STR_LOADING_FARM, STR_NO_CAMERA,
    STR_SNAPSHOT_UNREADABLE, STR_TAP_LOAD_CAM, STR_STARTING, STR_NO_PRINTERS, STR_NO_MATCH,
    /* PIN / lock */
    STR_ENTER_PIN, STR_WRONG_PIN,
    /* onboarding / messages */
    STR_ADD_MANAGE, STR_SCAN_GITHUB, STR_SCAN_ADDRESS, STR_CONNECT_EXPIRED,
    STR_FARM_UNAVAIL, STR_FARM_ORDER_FMT,
    /* dashboard / fleet card field labels + sidebar nav + headers */
    STR_NOZZLE, STR_BED, STR_SPEED, STR_Z_AXIS, STR_ETA_FMT,
    STR_UV_LED, STR_LAYER, STR_FILM, STR_ETA,
    STR_FLEET, STR_NAV_PRINTER, STR_FILES, STR_SETTINGS,
    /* printer state words (mapped from the backend's English state by tr_state) */
    STR_ST_IDLE, STR_ST_PRINTING, STR_ST_PAUSED, STR_ST_FINISHED, STR_ST_STOPPED,
    STR_ST_ERROR, STR_ST_ATTENTION, STR_ST_BUSY, STR_ST_PREPARING,
    /* firmware update check (on-device) */
    STR_CHECK_UPDATES, STR_CHECKING, STR_UPDATE_AVAILABLE, STR_UP_TO_DATE,
    STR_UPDATE_NOW, STR_LATER, STR_OK, STR_UPDATE_FAILED, STR_UPDATING,
    STR_UPTODATE_FMT, STR_NEWVER_FMT, STR_UPDFAIL_MSG,
    /* on-device Settings list */
    STR_DEVICE, STR_PREFERENCES, STR_WIFI_SETUP, STR_PRINTERS_HDR,
    STR_ADD_MANAGE_PRINTERS, STR_CONFIGURED_FMT, STR_MORE, STR_ABOUT,

    STR_COUNT
} pp_str_t;

/* Supported languages. Add one by appending here and adding a table in i18n.c. */
typedef enum {
    LANG_EN = 0,   /* English (base — always complete) */
    LANG_CS,       /* Čeština  */
    LANG_DE,       /* Deutsch  */
    LANG_FR,       /* Français */
    LANG_ES,       /* Español  */
    LANG_IT,       /* Italiano */
    LANG_PL,       /* Polski   */
    LANG_TLH,      /* tlhIngan Hol (Klingon) — for fun */
    LANG_QYA,      /* Quenya (Elvish) — for fun       */
    LANG_COUNT
} pp_lang_t;

/* Translate a key into the active language, falling back to English. Never NULL. */
const char *tr(pp_str_t id);

/* Translate a backend printer-state word (e.g. "PRINTING") for display. Returns the
 * localized label, or the original string unchanged if the state isn't recognized.
 * Display only — callers must keep keying tint/logic on the raw backend state. */
const char *tr_state(const char *backend_state);

/* Active language (defaults to English until i18n_set_lang). */
void      i18n_set_lang(pp_lang_t l);
pp_lang_t i18n_lang(void);

/* Endonyms for the web picker (browser-rendered, so full UTF-8). */
const char *i18n_lang_label(pp_lang_t l);

/* Short code (en, cs, it, tlh, qya, …) — used by the web UI to pick its JS dictionary. */
const char *i18n_lang_code(pp_lang_t l);

/* True if the language has translations (English always true). Empty stub
 * languages return false so the picker can hide them. */
bool i18n_lang_has_table(pp_lang_t l);
