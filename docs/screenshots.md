# Screenshots

These are rendered straight from the firmware's own UI code with the desktop
[simulator](../firmware/sim/) (`firmware/sim/`), so they match what the device draws on its
800×480 panel. The fleet shown is mock / live data for illustration.

## Fleet dashboard

Home screen — every printer at a glance (numbered for USB keyboard 1–9), state, temperatures,
progress, and model / job thumbnails. Settings is the header gear; tap a card to open that
printer.

![Fleet dashboard](img/fleet-dashboard.png)

## Printer detail

Status badge, nozzle / heatbed / speed / Z, current job, centered **E-STOP**, plus **Files** and
**Tools**. AFC lane chip appears when BoxTurtle lanes are detected.

![Printer detail](img/printer-detail.png)

## Tools

From printer detail, **Tools** opens the Klipper hub — Move, Temperature, Webcam, AFC,
Macros, Console, Tune, Calibration.

![Tools hub](img/control.png)

<!-- Guided add-printer flow (screenshots kept for later)
## Add a printer (guided)

One place to add machines: pick a **Cloud account** (Prusa or Bambu) or a **Local printer**
(Prusa / Klipper / Bambu), and only the fields that type needs appear.

![Add a printer](img/add-printer.png)

Choosing **Bambu (LAN)** asks for the IP, LAN Access Code, and serial:

![Add a Bambu printer](img/add-bambu.png)
-->

## Screen lock (opt-in)

After a few idle minutes the screen can lock; browsing the fleet stays available, but actions
ask for a PIN.

![Screen lock](img/screen-lock.png)

## Preferences

![Preferences](img/preferences.png)

## Wi-Fi

On-device network onboarding, with the web-UI address once connected.

![Wi-Fi](img/wifi.png)

## About

![About](img/about.png)

## Portrait

The UI reflows for a portrait mount — the fleet drops to a single column.

![Fleet dashboard (portrait)](img/fleet-dashboard-portrait.png)

<!-- ![Add a printer (portrait)](img/add-printer-portrait.png) -->
