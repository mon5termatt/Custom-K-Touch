# Translating Klipper Touch

All on-screen text lives in **one file**: [`firmware/main/i18n.c`](../firmware/main/i18n.c).
To translate, you edit that file — copy the English block, rename it, and translate
the right-hand sides. No other code changes are needed for a language that already
appears in the picker.

The web UI (the browser settings page) is **not** translated — only the on-device
touchscreen.

---

## A. Translate a language that's already listed

`cs` (Čeština), `de`, `fr`, `es`, `it`, `pl` already have an enum entry, a picker
label, and a (possibly empty) table. To fill one in:

1. Open `firmware/main/i18n.c` and find the `STR[][]` table.
2. Copy the entire `[LANG_EN] = { ... }` block.
3. Paste it and rename the key to your language, e.g. `[LANG_DE]` (replacing the
   existing `[LANG_DE] = { 0 }` stub).
4. Translate the **right-hand side** of each entry. Leave the `[STR_…]` keys alone.

```c
[LANG_DE] = {
    [STR_READY]   = "BEREIT",
    [STR_PAUSE]   = "PAUSE",
    [STR_SETTINGS]= "Einstellungen",
    // …translate every line…
},
```

### Rules

- **Keep printf specifiers exactly:** `%s`, `%d`, `%%`. You may reorder words around
  them, but don't add, remove, or retype them. `"Firmware: %s"` → `"Firmware : %s"` ✅,
  `"Firmware:"` ❌ (dropped the `%s`).
- **Preserve any leading/trailing spaces** the English entry has — a few strings include
  intentional spacing where text is concatenated. (Spacing around on-screen icons is handled
  in the code, not the strings, so most entries have none.)
- **Partial is fine.** Any entry you leave out (or set to `""`) falls back to English
  automatically. A half-finished language still works.
- **Case matters for style.** Status/button strings are uppercase by convention
  (`"PAUSE"`, `"READY"`); headings and labels use sentence case (`"Settings"`).
- **`…` and `—`** are real UTF-8 characters in the source and render fine. Use them as-is.

That's it — the language already has a picker entry, so it appears under
**Settings → Screen & Language** on the device once its table has at least one entry.

---

## B. Add a brand-new language

If your language isn't in the list yet, three small additions in addition to the table:

1. **Enum** — add a row in `firmware/main/i18n.h` (before `LANG_COUNT`):
   ```c
   typedef enum {
       LANG_EN = 0,
       …
       LANG_SV,        // Svenska (Swedish)
       LANG_COUNT
   } pp_lang_t;
   ```
2. **Picker label** — add a `case` in `i18n_lang_label()` in `i18n.c`. Use the language's
   own name (endonym); it's shown in the browser, so full UTF-8 is fine:
   ```c
   case LANG_SV: return "Svenska";
   ```
3. **Table** — add a `[LANG_SV] = { … }` block in `STR[][]` (see section A).

The picker auto-hides languages with an empty table (`i18n_lang_has_table()`), so an
untranslated stub won't show up until you start filling it in.

---

## Printer state words

The status badges (PRINTING / IDLE / PAUSED / …) come from the printer in English and
are translated for display by `tr_state()` in `i18n.c`. Their keys are the
`STR_ST_*` entries (plus `STR_READY` / `STR_OFFLINE`) — translate those like any other
string. The raw English value still drives the badge colour, so only the visible text
changes.

---

## Fonts / glyphs — important

The bundled Inter fonts currently carry **ASCII + the full European-Latin range**
(`0x20–0x7F` and `0xA0–0x17F`). That covers cs, de, fr, es, it, pl, and most
Latin-script languages out of the box.

A language that needs glyphs **outside** that range — Cyrillic, Greek, Turkish dotted-İ,
Vietnamese, CJK, etc. — will show **blank boxes** until the fonts are regenerated with
those glyphs. If you're translating into such a language, open an issue or note it in
your PR so the fonts can be extended. Regeneration steps:

```bash
# 1. Get Inter-Medium.ttf and FLATTEN its composite accents (lv_font_conv can't
#    resolve composites; this is why plain Inter renders accents as boxes):
python - <<'PY'
from fontTools.ttLib import TTFont
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.recordingPen import DecomposingRecordingPen
f=TTFont('Inter-Medium.ttf'); glyf=f['glyf']; gs=f.getGlyphSet()
for n in list(glyf.keys()):
    if glyf[n].isComposite():
        p=DecomposingRecordingPen(gs); gs[n].draw(p)
        t=TTGlyphPen(gs); p.replay(t); glyf[n]=t.glyph()
f.save('Inter-Medium-flat.ttf')
PY

# 2. Regenerate each size, widening the -r text range (here adding Cyrillic 0x400-0x4FF).
#    Keep the FontAwesome icon -r list identical to the existing inter_*.c "Opts:" header.
npx lv_font_conv --bpp 4 --size 16 \
  --font Inter-Medium-flat.ttf -r 0x20-0x7F,0xA0-0x17F,0x400-0x4FF,0x2014,0x2026 \
  --font FontAwesome5-Solid+Brands+Regular.woff -r <icon list from inter_16.c> \
  --format lvgl -o firmware/main/inter_16.c --force-fast-kern-format
# repeat for sizes 12, 14, 16, 20, 28, 40
```

Non-English UI automatically uses the Inter fonts (the default Montserrat is ASCII-only);
that switch lives in `set_fonts()` in `firmware/main/skin.c`.

---

## The web UI

The on-device touchscreen and the browser settings page are translated by **separate**
mechanisms. The device screen uses the C string table above; the web page uses a JavaScript
dictionary embedded in `INDEX_HTML[]` in `firmware/main/web.c`.

That dictionary (`WD`) is keyed by **the English source string** (not an enum), one block per
language code (`cs`, `it`, `tlh`, `qya`, …). Values are backtick template literals, so
apostrophes and accents need no escaping:

```js
WD={ cs:{ 'Settings':`Nastavení`, 'Save':`Uložit`, 'NOZZLE':`TRYSKA`, … }, it:{…} }
```

How it applies:
- A load-time DOM walk (`wapply()`) translates every static text node + input placeholder,
  skipping `<script>`/`<style>`. It also strips a leading icon/emoji and retries, so
  `📶 Wi-Fi` matches the key `Wi-Fi`.
- Dynamically-built content (the fleet/status cards) re-runs `wapply()` after rendering, and
  one-off JS messages use `tr('English string')`.
- The active language follows the device setting — `/api/info` returns `langcode`. There is no
  separate web language switcher.

To add/extend a web translation: edit the matching language block in `WD`. Missing keys fall
back to English, exactly like the device table. Brand names (Prusa, Bambu), printer names, and
material codes (PLA/PETG/ASA) are intentionally left untranslated.

---

## Test your translation without a device

The desktop simulator renders the real screens to a BMP (in `sim/out/`) in seconds. Set
the `PT_LANG` environment variable to the language index — the `pp_lang_t` value: 0 =
English, 1 = Čeština, etc. — and pass a screen name + size:

```bash
cd firmware
PT_LANG=1 sim/run.sh dash 480 800     # dashboard, portrait, in language #1 -> sim/out/dash_480x800.bmp
PT_LANG=1 sim/run.sh status 480 800
PT_LANG=1 sim/run.sh printers 480 800 # the Settings screen
```

(`sim/run.sh` builds the simulator on first run; see [`sim/README.md`](../firmware/sim/README.md).)
Check every accented character renders (no boxes) and nothing overflows its cell.

On hardware: flash, then **Settings → Screen & Language**, pick your language, save
(the device reboots into it).

---

## Submitting

Open a pull request with your `i18n.c` (and `i18n.h` if you added a new language).
Mention which language and whether it needs font work (section above). Include a
simulator screenshot if you can — it makes review fast.
