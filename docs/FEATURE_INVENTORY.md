# Feature inventory — K-Touch parity vs this fork

Living checklist. Legend: **Have** / **Partial** / **Missing** / **Skip** / **Web-only** / **Deferred**

| Area | Status | Notes |
|------|--------|-------|
| Fleet dash / files / Wi-Fi / OTA | Have | Fleet is home; no bottom nav — Settings gear in header |
| Tools menu hub | Have | Replaces packed Control |
| Move / Temp / Webcam screens | Have | Migrated from Control |
| AFC change/unload | Have | Dedicated AFC screen |
| AFC eject/move/prep/resume/clear | Have | M2 filled |
| Console | Have | gcode_store + send |
| Macros (discover + pin) | Have | |
| Tune (M220/M221) | Have | |
| Fan / unlock / extrude / jog steps | Partial | Single `M106` only — see multi-fan backlog |
| Calibration (endstops/PID/Z/mesh) | Have | |
| E-stop chrome | Have | Moonraker |
| Klipper fault page | Have | RESTART / FIRMWARE_RESTART |
| Multi-cam / LED notes | Partial | Notes textarea remains; live LED control on touch + web |
| Multi-fan Tools | Missing | Single `M106` — fan backlog below |
| Multi-LED Tools | Have | Discover `led` / `neopixel` / `led_effect` / `AFC_led`; Lights screen + `/api/leds` |
| LAN Moonraker scan | Deferred | Manual host/port only |
| Prusa / Bambu backends | Have | Kept, demoted in Tools |
| Panda PWR / battery HW | Skip | |

## Backlog — multi-fan & multi-LED (COSMOS + SV08)

Today: one part-cooling fan via `M106`. LEDs: Tools → **Lights** plus web Advanced live controls (`SET_LED`). Notes textarea is optional labels only.

### Printer object map (from `/printer/objects/list`)

**COSMOS** (`10.0.0.165`, OpenCentauri / Moonraker `:80`)

| Fans | LEDs |
|------|------|
| `fan` (part) | `led case` |
| `fan_generic aux_fan` | `led hotend` |
| `fan_generic case_fan` | |
| `heater_fan extruder` | |
| `temperature_fan mainboard` | |

**SV08** (`10.0.0.222:7125`)

| Fans | LEDs / effects |
|------|----------------|
| `fan` (part) | `led Chamber` |
| `heater_fan hotend_fan` | `neopixel screen`, `neopixel hotend` |
| `controller_fan MCU_fan` | `led_effect` suite (rainbow, printing, heating, …) |
| | `AFC_led AFC_Indicator`, `neopixel Turtle` |
| | macros `TURN_ON/OFF_AFC_LED`, `STATUS_*` |

### Future work (fans)

1. **Discover** — Moonraker: list fan-like objects (`fan`, `fan_generic`, `heater_fan`, `temperature_fan`, `controller_fan`); skip underscore-hidden where appropriate.
2. **Fan Tools screen** — Per-fan speed (0–100%) via `SET_FAN_SPEED FAN=<name> SPEED=…` (not only `M106`); show readback from object query; hide or mark auto fans (`heater_fan` / `controller_fan` / `temperature_fan`) as info-only or capped control.
3. **Hub tile** — Add Fans (Moonraker-only).
4. **Prefs (optional)** — Pin favorite fans per printer (macro-pin pattern).
