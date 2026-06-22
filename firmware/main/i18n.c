/*
 * Prusa-Touch — the one and only string table.
 *
 * ===================  HOW TO TRANSLATE  ===================
 * 1. Copy the whole [LANG_EN] = { ... } block below.
 * 2. Paste it, rename to your language (e.g. [LANG_DE]).
 * 3. Translate the right-hand side of each "[STR_x] = ...".
 *    - Keep printf specifiers (%s, %d, %%) intact; you may reorder words
 *      around them but not change the specifiers.
 *    - Keep any leading space / icon spacing where present.
 *    - Leave an entry out (or "") to fall back to English for that one string.
 * 4. Make sure your language has a row in LANG enum (i18n.h) and a label in
 *    i18n_lang_label() below.
 *
 * ===================  FONT / GLYPHS  ======================
 * The bundled Inter fonts (inter_*.c) are generated for ASCII only
 * (lv_font_conv "-r 0x20-0x7F" + a few symbols). Languages needing accented
 * Latin (cs/de/fr/es/it/pl) or other scripts will show blank boxes until the
 * fonts are rebuilt with a wider range, e.g. add to each font's -r list:
 *     0xA0-0x17F        (Latin-1 Supplement + Latin Extended-A: European Latin)
 * Then regenerate with lv_font_conv. Until then, only English is correct on
 * screen; non-English tables are provided as a translator scaffold.
 */
#include "i18n.h"
#include <stddef.h>

/* [language][key] -> string, or NULL to fall back to English. */
static const char *const STR[LANG_COUNT][STR_COUNT] = {
    [LANG_EN] = {
        [STR_READY]            = "READY",
        [STR_OFFLINE]          = "OFFLINE",
        [STR_LOCKED]           = " LOCKED",
        [STR_ATTENTION]        = "Attention",
        [STR_PRINTING_PAREN]   = "(printing)",
        [STR_PAUSE]            = "PAUSE",
        [STR_RESUME]           = "RESUME",
        [STR_PREHEAT]          = "PREHEAT",
        [STR_MOVE]             = "MOVE",
        [STR_PROGRESS]         = "PROGRESS",
        [STR_WEBCAM]           = "WEBCAM",
        [STR_PRINTER]          = " Printer",
        [STR_USB]              = " USB",
        [STR_FILE]             = "File",
        [STR_CONNECT]          = "Connect",
        [STR_THEME]            = "Theme",
        [STR_SCREEN_ORIENTATION] = "Screen orientation",
        [STR_AUTO_FW_UPDATES]  = "Automatic firmware updates",
        [STR_LANGUAGE]         = "Language",
        [STR_FW_FMT]           = "Firmware: %s",
        [STR_NETWORK_FMT]      = "Network: %s",
        [STR_NETWORK_SCAN]     = "Network: (tap Scan)",
        [STR_FILES_ON_FMT]     = "Files on  %s",
        [STR_FILES_ON_2_FMT]   = "Files on  %s   -   %s",
        [STR_FILES_ON_THIS]    = "Files on this printer",
        [STR_LOCAL_FILES_USB]  = "Local files on USB drive",
        [STR_NO_PRINTABLE]     = "No printable files on this printer",
        [STR_NO_ACTIVE_PRINT]  = "No active print",
        [STR_NO_PREVIEW]       = "No preview",
        [STR_PREVIEW_UNAVAIL]  = "Preview unavailable",
        [STR_LOADING_PREVIEW]  = "Loading preview...",
        [STR_LOADING]          = "Loading\xE2\x80\xA6",          /* "Loading…" */
        [STR_LOADING_WEBCAM]   = "Loading webcam\xE2\x80\xA6",   /* "Loading webcam…" */
        [STR_LOADING_FARM]     = "Loading Prusa Farm...",
        [STR_NO_CAMERA]        = "No camera / no recent frame",
        [STR_SNAPSHOT_UNREADABLE] = "Snapshot unreadable",
        [STR_TAP_LOAD_CAM]     = "Tap Load for the live camera",
        [STR_STARTING]         = "Starting...",
        [STR_NO_PRINTERS]      = "No printers yet \xE2\x80\x94 add one in Settings.",
        [STR_NO_MATCH]         = "No printers match the current filter.",
        [STR_ENTER_PIN]        = "Enter PIN to unlock",
        [STR_WRONG_PIN]        = "Wrong PIN, try again",
        [STR_ADD_MANAGE]       = "Add or manage printers from your phone or computer",
        [STR_SCAN_GITHUB]      = "Scan for the project on GitHub",
        [STR_SCAN_ADDRESS]     = "Scan the code, or open this address in any browser on the same Wi-Fi. "
                                 "The web page handles API keys and Prusa Connect / Klipper / Bambu sign-in, "
                                 "which a touchscreen can't do well.",
        [STR_CONNECT_EXPIRED]  = "  Prusa Connect sign-in expired. ",
        [STR_FARM_UNAVAIL]     = "Prusa Farm unavailable. Set your Organization ID in the web UI (Farm tab), then reopen.",
        [STR_FARM_ORDER_FMT]   = "%s   -   done %d/%d%s",
    },

    /* ---- Translator scaffold. Fill in and regenerate fonts (see header). ----
     * Entries left out fall back to English, so a partial translation is fine. */
    [LANG_CS] = { 0 },
    [LANG_DE] = { 0 },
    [LANG_FR] = { 0 },
    [LANG_ES] = { 0 },
    [LANG_IT] = { 0 },
    [LANG_PL] = { 0 },
};

static pp_lang_t s_lang = LANG_EN;

const char *tr(pp_str_t id)
{
    if (id <= STR_NONE || id >= STR_COUNT) return "";
    const char *s = STR[s_lang][id];
    if (!s || !s[0]) s = STR[LANG_EN][id];   /* fall back to English */
    return s ? s : "";
}

void      i18n_set_lang(pp_lang_t l) { if ((unsigned)l < LANG_COUNT) s_lang = l; }
pp_lang_t i18n_lang(void)            { return s_lang; }

const char *i18n_lang_label(pp_lang_t l)
{
    /* ASCII-safe endonyms so the picker is legible with the stock ASCII font. */
    switch (l) {
        case LANG_EN: return "English";
        case LANG_CS: return "Cestina (Czech)";
        case LANG_DE: return "Deutsch (German)";
        case LANG_FR: return "Francais (French)";
        case LANG_ES: return "Espanol (Spanish)";
        case LANG_IT: return "Italiano (Italian)";
        case LANG_PL: return "Polski (Polish)";
        default:      return "?";
    }
}

#ifdef PP_I18N_SELFTEST
/* ponytail: build-time-ish self check — compile this TU with -DPP_I18N_SELFTEST
 * and call i18n_selftest() once to assert every key has an English string and
 * that fallback works. Keeps the enum and the table from drifting. */
#include <assert.h>
void i18n_selftest(void)
{
    for (int k = STR_NONE + 1; k < STR_COUNT; k++) {
        assert(STR[LANG_EN][k] && STR[LANG_EN][k][0] && "missing English string");
    }
    i18n_set_lang(LANG_CS);
    assert(tr(STR_READY) == STR[LANG_EN][STR_READY]); /* empty CS -> EN fallback */
    i18n_set_lang(LANG_EN);
    assert(tr(STR_COUNT)[0] == '\0' && tr(STR_NONE)[0] == '\0'); /* out of range -> "" */
}
#endif
