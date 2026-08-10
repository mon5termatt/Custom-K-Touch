/*
 * Prusa-Touch — the one and only string table.
 *
 * Full translator guide: docs/TRANSLATING.md (how to add a language, fonts, testing).
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
#include <strings.h>   /* strcasecmp */

/* [language][key] -> string, or NULL to fall back to English. */
static const char *const STR[LANG_COUNT][STR_COUNT] = {
    [LANG_EN] = {
        [STR_READY]            = "READY",
        [STR_OFFLINE]          = "OFFLINE",
        [STR_LOCKED]           = "LOCKED",
        [STR_ATTENTION]        = "Attention",
        [STR_PRINTING_PAREN]   = "(printing)",
        [STR_PAUSE]            = "Pause",
        [STR_RESUME]           = "Resume",
        [STR_PREHEAT]          = "Preheat",
        [STR_MOVE]             = "Move",
        [STR_PROGRESS]         = "PROGRESS",
        [STR_WEBCAM]           = "Webcam",
        [STR_AFC]              = "AFC",
        [STR_AFC_UNLOAD]       = "Unload",
        [STR_AFC_LANE_FMT]     = "AFC: %s",
        [STR_AFC_LOAD]         = "Load",
        [STR_AFC_EJECT]        = "Eject",
        [STR_AFC_EMPTY]        = "Empty",
        [STR_AFC_READY]        = "Ready",
        [STR_AFC_LOADED_FMT]   = "Loaded: %s",
        [STR_AFC_PREP]         = "Prep",
        [STR_AFC_CLEAR]        = "Clear",
        [STR_AFC_MOVE_LANE]    = "Lane Move",
        [STR_TOOLS]            = "Tools",
        [STR_COMING_SOON]      = "Coming soon",
        [STR_CONSOLE]          = "Console",
        [STR_MACROS]           = "Macros",
        [STR_TUNE]             = "Tune",
        [STR_CALIBRATION]      = "Calibration",
        [STR_TEMPERATURE]      = "Temperature",
        [STR_ESTOP]            = "E-STOP",
        [STR_UNLOCK_MOTORS]    = "Unlock",
        [STR_EXTRUDE]          = "Extrude",
        [STR_FAN]              = "Fan",
        [STR_SPEED_FACTOR]     = "Speed %",
        [STR_FLOW_FACTOR]      = "Flow %",
        [STR_ENDSTOPS]         = "Endstops",
        [STR_AUTO_PID]         = "Auto PID",
        [STR_Z_OFFSET]         = "Z-Offset",
        [STR_BED_MESH]         = "Bed Mesh",
        [STR_SAVE_CONFIG]      = "Save Config",
        [STR_KLIPPER_RESTART]  = "Restart",
        [STR_FW_RESTART]       = "FW Restart",
        [STR_PRINTER]          = "Printer",
        [STR_USB]              = "USB",
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
        [STR_TAP_LOAD_CAM]     = "Tap Load, then tap the image for fullscreen",
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
        [STR_NOZZLE]           = "NOZZLE",
        [STR_BED]              = "BED",
        [STR_SPEED]            = "SPEED",
        [STR_Z_AXIS]           = "Z AXIS",
        [STR_ETA_FMT]          = "ETA %s",
        [STR_FLEET]            = "Fleet",
        [STR_NAV_PRINTER]      = "Printer",
        [STR_FILES]            = "Files",
        [STR_SETTINGS]         = "Settings",
        [STR_ST_IDLE]          = "IDLE",
        [STR_ST_PRINTING]      = "PRINTING",
        [STR_ST_PAUSED]        = "PAUSED",
        [STR_ST_FINISHED]      = "FINISHED",
        [STR_ST_STOPPED]       = "STOPPED",
        [STR_ST_ERROR]         = "ERROR",
        [STR_ST_ATTENTION]     = "ATTENTION",
        [STR_ST_BUSY]          = "BUSY",
        [STR_ST_PREPARING]     = "PREPARING",
        [STR_CHECK_UPDATES]    = "Check for updates",
        [STR_CHECKING]         = "Checking for updates\xE2\x80\xA6",
        [STR_UPDATE_AVAILABLE] = "Update available",
        [STR_UP_TO_DATE]       = "Up to date",
        [STR_UPDATE_NOW]       = "Update now",
        [STR_LATER]            = "Later",
        [STR_OK]               = "OK",
        [STR_UPDATE_FAILED]    = "Couldn't check for updates",
        [STR_UPDATING]         = "Updating\xE2\x80\xA6 the device will restart.",
        [STR_UPTODATE_FMT]     = "You're on the latest version (%s).",
        [STR_NEWVER_FMT]       = "Version %s is available.\nYou're on %s.",
        [STR_UPDFAIL_MSG]      = "Couldn't reach the update server. Check your Wi-Fi.",
        [STR_DEVICE]           = "DEVICE",
        [STR_PREFERENCES]      = "Preferences",
        [STR_WIFI_SETUP]       = "Wi-Fi setup",
        [STR_PRINTERS_HDR]     = "PRINTERS",
        [STR_ADD_MANAGE_PRINTERS] = "Add or manage printers",
        [STR_CONFIGURED_FMT]   = "%d configured  -  in a browser: http://%s/",
        [STR_MORE]             = "MORE",
        [STR_ABOUT]            = "About / License",
    },

    /* ---- Translator scaffold. Fill in and regenerate fonts (see header). ----
     * Entries left out fall back to English, so a partial translation is fine. */
    [LANG_CS] = {
        [STR_READY]            = "PŘIPRAVENO",
        [STR_OFFLINE]          = "OFFLINE",
        [STR_LOCKED]           = "ZAMČENO",
        [STR_ATTENTION]        = "Pozor",
        [STR_PRINTING_PAREN]   = "(tisk)",
        [STR_PAUSE]            = "Pozastavit",
        [STR_RESUME]           = "Pokračovat",
        [STR_PREHEAT]          = "Předehřev",
        [STR_MOVE]             = "Pohyb",
        [STR_PROGRESS]         = "PRŮBĚH",
        [STR_WEBCAM]           = "Kamera",
        [STR_PRINTER]          = "Tiskárna",
        [STR_USB]              = "USB",
        [STR_FILE]             = "Soubor",
        [STR_CONNECT]          = "Připojit",
        [STR_THEME]            = "Motiv",
        [STR_SCREEN_ORIENTATION] = "Orientace obrazovky",
        [STR_AUTO_FW_UPDATES]  = "Automatické aktualizace firmwaru",
        [STR_LANGUAGE]         = "Jazyk",
        [STR_FW_FMT]           = "Firmware: %s",
        [STR_NETWORK_FMT]      = "Síť: %s",
        [STR_NETWORK_SCAN]     = "Síť: (klepněte na Hledat)",
        [STR_FILES_ON_FMT]     = "Soubory na  %s",
        [STR_FILES_ON_2_FMT]   = "Soubory na  %s   -   %s",
        [STR_FILES_ON_THIS]    = "Soubory v této tiskárně",
        [STR_LOCAL_FILES_USB]  = "Místní soubory na USB disku",
        [STR_NO_PRINTABLE]     = "V této tiskárně nejsou žádné tisknutelné soubory",
        [STR_NO_ACTIVE_PRINT]  = "Žádný aktivní tisk",
        [STR_NO_PREVIEW]       = "Bez náhledu",
        [STR_PREVIEW_UNAVAIL]  = "Náhled není k dispozici",
        [STR_LOADING_PREVIEW]  = "Načítání náhledu...",
        [STR_LOADING]          = "Načítání…",
        [STR_LOADING_WEBCAM]   = "Načítání kamery…",
        [STR_LOADING_FARM]     = "Načítání Prusa Farm...",
        [STR_NO_CAMERA]        = "Žádná kamera / žádný snímek",
        [STR_SNAPSHOT_UNREADABLE] = "Snímek nelze načíst",
        [STR_TAP_LOAD_CAM]     = "Klepněte na Načíst, pak na obrázek pro celou obrazovku",
        [STR_STARTING]         = "Spouštění...",
        [STR_NO_PRINTERS]      = "Zatím žádné tiskárny — přidejte ji v Nastavení.",
        [STR_NO_MATCH]         = "Žádné tiskárny neodpovídají filtru.",
        [STR_ENTER_PIN]        = "Zadejte PIN pro odemčení",
        [STR_WRONG_PIN]        = "Špatný PIN, zkuste to znovu",
        [STR_ADD_MANAGE]       = "Přidejte nebo spravujte tiskárny z telefonu či počítače",
        [STR_SCAN_GITHUB]      = "Naskenujte projekt na GitHubu",
        [STR_SCAN_ADDRESS]     = "Naskenujte kód nebo otevřete tuto adresu v libovolném prohlížeči ve stejné Wi-Fi. "
                                 "Webová stránka zpracuje API klíče a přihlášení k Prusa Connect / Klipper / Bambu, "
                                 "což na dotykové obrazovce nejde dobře.",
        [STR_CONNECT_EXPIRED]  = "  Přihlášení k Prusa Connect vypršelo. ",
        [STR_FARM_UNAVAIL]     = "Prusa Farm není k dispozici. Nastavte ID organizace ve webovém rozhraní (záložka Farm) a otevřete znovu.",
        [STR_FARM_ORDER_FMT]   = "%s   -   hotovo %d/%d%s",
        [STR_NOZZLE]           = "TRYSKA",
        [STR_BED]              = "PODLOŽKA",
        [STR_SPEED]            = "RYCHLOST",
        [STR_Z_AXIS]           = "OSA Z",
        [STR_ETA_FMT]          = "ZBÝVÁ %s",
        [STR_FLEET]            = "Flotila",
        [STR_NAV_PRINTER]      = "Tiskárna",
        [STR_FILES]            = "Soubory",
        [STR_SETTINGS]         = "Nastavení",
        [STR_ST_IDLE]          = "NEČINNÝ",
        [STR_ST_PRINTING]      = "TISK",
        [STR_ST_PAUSED]        = "POZASTAVENO",
        [STR_ST_FINISHED]      = "DOKONČENO",
        [STR_ST_STOPPED]       = "ZASTAVENO",
        [STR_ST_ERROR]         = "CHYBA",
        [STR_ST_ATTENTION]     = "POZOR",
        [STR_ST_BUSY]          = "ZANEPRÁZDNĚNO",
        [STR_ST_PREPARING]     = "PŘÍPRAVA",
        [STR_CHECK_UPDATES]    = "Zkontrolovat aktualizace",
        [STR_CHECKING]         = "Kontrola aktualizací\xE2\x80\xA6",
        [STR_UPDATE_AVAILABLE] = "Dostupná aktualizace",
        [STR_UP_TO_DATE]       = "Aktuální",
        [STR_UPDATE_NOW]       = "Aktualizovat",
        [STR_LATER]            = "Později",
        [STR_OK]               = "OK",
        [STR_UPDATE_FAILED]    = "Kontrola se nezdařila",
        [STR_UPDATING]         = "Aktualizace\xE2\x80\xA6 zařízení se restartuje.",
        [STR_UPTODATE_FMT]     = "Máte nejnovější verzi (%s).",
        [STR_NEWVER_FMT]       = "Je dostupná verze %s.\nMáte %s.",
        [STR_UPDFAIL_MSG]      = "Nelze se připojit k serveru aktualizací. Zkontrolujte Wi-Fi.",
        [STR_DEVICE]           = "ZAŘÍZENÍ",
        [STR_PREFERENCES]      = "Předvolby",
        [STR_WIFI_SETUP]       = "Nastavení Wi-Fi",
        [STR_PRINTERS_HDR]     = "TISKÁRNY",
        [STR_ADD_MANAGE_PRINTERS] = "Přidat nebo spravovat tiskárny",
        [STR_CONFIGURED_FMT]   = "%d nakonfigurováno  -  v prohlížeči: http://%s/",
        [STR_MORE]             = "VÍCE",
        [STR_ABOUT]            = "O aplikaci / Licence",
        [STR_AFC]              = "AFC",
        [STR_AFC_UNLOAD]       = "Vysunout",
        [STR_AFC_LANE_FMT]     = "AFC: %s",
        [STR_AFC_LOAD]         = "Zavest",
        [STR_AFC_EJECT]        = "Vyjmout",
        [STR_AFC_EMPTY]        = "Prazdne",
        [STR_AFC_READY]        = "Pripraveno",
        [STR_AFC_LOADED_FMT]   = "Nacteno: %s",
        [STR_AFC_PREP]         = "Prep",
        [STR_AFC_CLEAR]        = "Vymazat",
        [STR_AFC_MOVE_LANE]    = "Posun",
    },
    [LANG_DE] = { 0 },
    [LANG_FR] = { 0 },
    [LANG_ES] = { 0 },
    [LANG_IT] = {
        [STR_READY]            = "PRONTO",
        [STR_OFFLINE]          = "OFFLINE",
        [STR_LOCKED]           = "BLOCCATO",
        [STR_ATTENTION]        = "Attenzione",
        [STR_PRINTING_PAREN]   = "(stampa)",
        [STR_PAUSE]            = "Pausa",
        [STR_RESUME]           = "Riprendi",
        [STR_PREHEAT]          = "Preriscalda",
        [STR_MOVE]             = "Muovi",
        [STR_PROGRESS]         = "AVANZAMENTO",
        [STR_WEBCAM]           = "Webcam",
        [STR_PRINTER]          = "Stampante",
        [STR_USB]              = "USB",
        [STR_FILE]             = "File",
        [STR_CONNECT]          = "Connetti",
        [STR_THEME]            = "Tema",
        [STR_SCREEN_ORIENTATION] = "Orientamento schermo",
        [STR_AUTO_FW_UPDATES]  = "Aggiornamenti firmware automatici",
        [STR_LANGUAGE]         = "Lingua",
        [STR_FW_FMT]           = "Firmware: %s",
        [STR_NETWORK_FMT]      = "Rete: %s",
        [STR_NETWORK_SCAN]     = "Rete: (tocca Cerca)",
        [STR_FILES_ON_FMT]     = "File su  %s",
        [STR_FILES_ON_2_FMT]   = "File su  %s   -   %s",
        [STR_FILES_ON_THIS]    = "File su questa stampante",
        [STR_LOCAL_FILES_USB]  = "File locali su unità USB",
        [STR_NO_PRINTABLE]     = "Nessun file stampabile su questa stampante",
        [STR_NO_ACTIVE_PRINT]  = "Nessuna stampa attiva",
        [STR_NO_PREVIEW]       = "Nessuna anteprima",
        [STR_PREVIEW_UNAVAIL]  = "Anteprima non disponibile",
        [STR_LOADING_PREVIEW]  = "Caricamento anteprima...",
        [STR_LOADING]          = "Caricamento…",
        [STR_LOADING_WEBCAM]   = "Caricamento webcam…",
        [STR_LOADING_FARM]     = "Caricamento Prusa Farm...",
        [STR_NO_CAMERA]        = "Nessuna camera / nessun fotogramma",
        [STR_SNAPSHOT_UNREADABLE] = "Istantanea illeggibile",
        [STR_TAP_LOAD_CAM]     = "Tocca Carica, poi l'immagine per schermo intero",
        [STR_STARTING]         = "Avvio...",
        [STR_NO_PRINTERS]      = "Nessuna stampante — aggiungine una in Impostazioni.",
        [STR_NO_MATCH]         = "Nessuna stampante corrisponde al filtro.",
        [STR_ENTER_PIN]        = "Inserisci il PIN per sbloccare",
        [STR_WRONG_PIN]        = "PIN errato, riprova",
        [STR_ADD_MANAGE]       = "Aggiungi o gestisci le stampanti dal telefono o computer",
        [STR_SCAN_GITHUB]      = "Scansiona il progetto su GitHub",
        [STR_SCAN_ADDRESS]     = "Scansiona il codice o apri questo indirizzo in un browser sulla stessa Wi-Fi. "
                                 "La pagina web gestisce le chiavi API e l'accesso a Prusa Connect / Klipper / Bambu, "
                                 "difficile da fare su un touchscreen.",
        [STR_CONNECT_EXPIRED]  = "  Accesso a Prusa Connect scaduto. ",
        [STR_FARM_UNAVAIL]     = "Prusa Farm non disponibile. Imposta l'ID organizzazione nell'interfaccia web (scheda Farm), poi riapri.",
        [STR_FARM_ORDER_FMT]   = "%s   -   fatto %d/%d%s",
        [STR_NOZZLE]           = "UGELLO",
        [STR_BED]              = "PIANO",
        [STR_SPEED]            = "VELOCITÀ",
        [STR_Z_AXIS]           = "ASSE Z",
        [STR_ETA_FMT]          = "STIMA %s",
        [STR_FLEET]            = "Flotta",
        [STR_NAV_PRINTER]      = "Stampante",
        [STR_FILES]            = "File",
        [STR_SETTINGS]         = "Impostazioni",
        [STR_ST_IDLE]          = "INATTIVO",
        [STR_ST_PRINTING]      = "IN STAMPA",
        [STR_ST_PAUSED]        = "IN PAUSA",
        [STR_ST_FINISHED]      = "COMPLETATO",
        [STR_ST_STOPPED]       = "ARRESTATO",
        [STR_ST_ERROR]         = "ERRORE",
        [STR_ST_ATTENTION]     = "ATTENZIONE",
        [STR_ST_BUSY]          = "OCCUPATO",
        [STR_ST_PREPARING]     = "PREPARAZIONE",
        [STR_CHECK_UPDATES]    = "Controlla aggiornamenti",
        [STR_CHECKING]         = "Controllo aggiornamenti\xE2\x80\xA6",
        [STR_UPDATE_AVAILABLE] = "Aggiornamento disponibile",
        [STR_UP_TO_DATE]       = "Aggiornato",
        [STR_UPDATE_NOW]       = "Aggiorna ora",
        [STR_LATER]            = "Più tardi",
        [STR_OK]               = "OK",
        [STR_UPDATE_FAILED]    = "Controllo non riuscito",
        [STR_UPDATING]         = "Aggiornamento\xE2\x80\xA6 il dispositivo si riavvierà.",
        [STR_UPTODATE_FMT]     = "Hai l'ultima versione (%s).",
        [STR_NEWVER_FMT]       = "È disponibile la versione %s.\nHai la %s.",
        [STR_UPDFAIL_MSG]      = "Impossibile raggiungere il server. Controlla il Wi-Fi.",
        [STR_DEVICE]           = "DISPOSITIVO",
        [STR_PREFERENCES]      = "Preferenze",
        [STR_WIFI_SETUP]       = "Configurazione Wi-Fi",
        [STR_PRINTERS_HDR]     = "STAMPANTI",
        [STR_ADD_MANAGE_PRINTERS] = "Aggiungi o gestisci stampanti",
        [STR_CONFIGURED_FMT]   = "%d configurate  -  nel browser: http://%s/",
        [STR_MORE]             = "ALTRO",
        [STR_ABOUT]            = "Informazioni / Licenza",
        [STR_AFC]              = "AFC",
        [STR_AFC_UNLOAD]       = "Scarica",
        [STR_AFC_LANE_FMT]     = "AFC: %s",
        [STR_AFC_LOAD]         = "Carica",
        [STR_AFC_EJECT]        = "Espelli",
        [STR_AFC_EMPTY]        = "Vuoto",
        [STR_AFC_READY]        = "Pronto",
        [STR_AFC_LOADED_FMT]   = "Caricato: %s",
        [STR_AFC_PREP]         = "Prep",
        [STR_AFC_CLEAR]        = "Pulisci",
        [STR_AFC_MOVE_LANE]    = "Muovi",
    },
    [LANG_PL] = { 0 },

    /* ---- Joke languages. Best-effort fun, not certified by the KLI or the Tolkien
     * Estate. Both use Latin transliteration (the native pIqaD/Tengwar scripts aren't
     * in the font). Untranslated entries fall back to English. ---- */
    [LANG_TLH] = {   /* tlhIngan Hol — Klingon. Mind the case: D H I Q S are capitals. */
        [STR_READY]            = "ghuS",
        [STR_OFFLINE]          = "QumHa'",
        [STR_ATTENTION]        = "qIm",
        [STR_PAUSE]            = "yev",
        [STR_RESUME]           = "ruch",
        [STR_PREHEAT]          = "tujmoH",
        [STR_MOVE]             = "vIH",
        [STR_PROGRESS]         = "QaptaH",
        [STR_WEBCAM]           = "mIllogh",
        [STR_FILE]             = "ghItlh",
        [STR_CONNECT]          = "rar",
        [STR_THEME]            = "rItlh",
        [STR_LANGUAGE]         = "Hol",
        [STR_SCREEN_ORIENTATION] = "HaSta jIH",
        [STR_NO_ACTIVE_PRINT]  = "ghItlhtaH pagh",
        [STR_NO_PRINTERS]      = "ghItlhwI' tu'lu'be'.",
        [STR_STARTING]         = "taghtaH\xE2\x80\xA6",
        [STR_LOADING]          = "Suq\xE2\x80\xA6",
        [STR_LOADING_PREVIEW]  = "Suq\xE2\x80\xA6",
        [STR_NOZZLE]           = "tujwI'",
        [STR_BED]              = "QongDaq",
        [STR_SPEED]            = "Do",
        [STR_Z_AXIS]           = "Z tlhegh",
        [STR_FLEET]            = "Dujmey",
        [STR_NAV_PRINTER]      = "ghItlhwI'",
        [STR_FILES]            = "ghItlhmey",
        [STR_SETTINGS]         = "wIvmey",
        [STR_ST_IDLE]          = "Qong",
        [STR_ST_PRINTING]      = "ghItlhtaH",
        [STR_ST_PAUSED]        = "yevtaH",
        [STR_ST_FINISHED]      = "rIntaH",
        [STR_ST_STOPPED]       = "mevta'",
        [STR_ST_ERROR]         = "Qagh",
        [STR_ST_ATTENTION]     = "yIqIm",
        [STR_ST_BUSY]          = "vumtaH",
        [STR_ST_PREPARING]     = "ghuStaH",
        [STR_CHECK_UPDATES]    = "chu'mey yISam",
        [STR_UPDATE_AVAILABLE] = "chu' tu'lu'",
        [STR_UP_TO_DATE]       = "chu'qu'",
        [STR_UPDATE_NOW]       = "DaH yIchu'",
        [STR_LATER]            = "tugh",
        [STR_OK]               = "luq",
        [STR_DEVICE]           = "jan",
        [STR_PREFERENCES]      = "DuHmey",
        [STR_PRINTERS_HDR]     = "ghItlhwI'mey",
        [STR_MORE]             = "latlh",
        [STR_ABOUT]            = "ngoDmey",
    },
    [LANG_QYA] = {   /* Quenya — Tolkien's High-Elven, romanized. */
        [STR_READY]            = "manwa",
        [STR_OFFLINE]          = "vanwa",
        [STR_ATTENTION]        = "tira",
        [STR_PAUSE]            = "hauta",
        [STR_RESUME]           = "lelya",
        [STR_PREHEAT]          = "urya",
        [STR_MOVE]             = "leva",
        [STR_PROGRESS]         = "túlë",
        [STR_WEBCAM]           = "cenda",
        [STR_FILE]             = "parma",
        [STR_CONNECT]          = "nuta",
        [STR_THEME]            = "canta",
        [STR_LANGUAGE]         = "lambë",
        [STR_SCREEN_ORIENTATION] = "henet tië",
        [STR_NO_ACTIVE_PRINT]  = "úmë tecië",
        [STR_NO_PRINTERS]      = "úmë tecindo.",
        [STR_STARTING]         = "yesta\xE2\x80\xA6",
        [STR_LOADING]          = "túla\xE2\x80\xA6",
        [STR_LOADING_PREVIEW]  = "túla\xE2\x80\xA6",
        [STR_NOZZLE]           = "anto",
        [STR_BED]              = "caima",
        [STR_SPEED]            = "lintië",
        [STR_Z_AXIS]           = "Z tië",
        [STR_FLEET]            = "ciryali",
        [STR_NAV_PRINTER]      = "tecindo",
        [STR_FILES]            = "parmar",
        [STR_SETTINGS]         = "panyalë",
        [STR_ST_IDLE]          = "sérëa",
        [STR_ST_PRINTING]      = "técala",
        [STR_ST_PAUSED]        = "hautaina",
        [STR_ST_FINISHED]      = "telyaina",
        [STR_ST_STOPPED]       = "pustaina",
        [STR_ST_ERROR]         = "úcarë",
        [STR_ST_ATTENTION]     = "tira!",
        [STR_ST_BUSY]          = "mótala",
        [STR_ST_PREPARING]     = "manwëa",
        [STR_CHECK_UPDATES]    = "Cesta envinyar",
        [STR_UPDATE_AVAILABLE] = "Envinya",
        [STR_UP_TO_DATE]       = "Vinya",
        [STR_UPDATE_NOW]       = "Envinyata sí",
        [STR_LATER]            = "epë",
        [STR_OK]               = "Mára",
        [STR_DEVICE]           = "Carma",
        [STR_PREFERENCES]      = "Cilmë",
        [STR_PRINTERS_HDR]     = "Tecindor",
        [STR_MORE]             = "Ambë",
        [STR_ABOUT]            = "Pá",
    },
};

static pp_lang_t s_lang = LANG_EN;

const char *tr(pp_str_t id)
{
    if (id <= STR_NONE || id >= STR_COUNT) return "";
    const char *s = STR[s_lang][id];
    if (!s || !s[0]) s = STR[LANG_EN][id];   /* fall back to English */
    return s ? s : "";
}

const char *tr_state(const char *s)
{
    if (!s || !s[0]) return "";
    static const struct { const char *k; pp_str_t id; } map[] = {
        { "IDLE", STR_ST_IDLE }, { "READY", STR_READY }, { "PRINTING", STR_ST_PRINTING },
        { "PAUSED", STR_ST_PAUSED }, { "FINISHED", STR_ST_FINISHED }, { "STOPPED", STR_ST_STOPPED },
        { "ERROR", STR_ST_ERROR }, { "ATTENTION", STR_ST_ATTENTION }, { "BUSY", STR_ST_BUSY },
        { "PREPARING", STR_ST_PREPARING }, { "OFFLINE", STR_OFFLINE },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcasecmp(s, map[i].k) == 0) return tr(map[i].id);
    return s;   /* unknown state -> show the backend's word as-is */
}

void      i18n_set_lang(pp_lang_t l) { if ((unsigned)l < LANG_COUNT) s_lang = l; }
pp_lang_t i18n_lang(void)            { return s_lang; }

/* True if this language has any translations (English is always complete). Used to
 * hide empty stub languages from the picker. */
bool i18n_lang_has_table(pp_lang_t l)
{
    if (l == LANG_EN) return true;
    if ((unsigned)l >= LANG_COUNT) return false;
    for (int k = STR_NONE + 1; k < STR_COUNT; k++)
        if (STR[l][k]) return true;
    return false;
}

const char *i18n_lang_label(pp_lang_t l)
{
    /* Endonyms — shown only in the web picker (a browser, full UTF-8). */
    switch (l) {
        case LANG_EN: return "English";
        case LANG_CS: return "Čeština";
        case LANG_DE: return "Deutsch";
        case LANG_FR: return "Français";
        case LANG_ES: return "Español";
        case LANG_IT: return "Italiano";
        case LANG_PL: return "Polski";
        case LANG_TLH:return "tlhIngan Hol (Klingon)";
        case LANG_QYA:return "Quenya (Elvish)";
        default:      return "?";
    }
}

const char *i18n_lang_code(pp_lang_t l)
{
    switch (l) {
        case LANG_EN: return "en";
        case LANG_CS: return "cs";
        case LANG_DE: return "de";
        case LANG_FR: return "fr";
        case LANG_ES: return "es";
        case LANG_IT: return "it";
        case LANG_PL: return "pl";
        case LANG_TLH:return "tlh";
        case LANG_QYA:return "qya";
        default:      return "en";
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
