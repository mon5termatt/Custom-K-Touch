#!/usr/bin/env bash
# Pulse a hardware reset over the serial line (RTS), without re-flashing. Useful if the single
# HTTP worker ever wedges (e.g. a client aborted a big /api/screen.bmp mid-stream) and the
# device stops answering HTTP while otherwise running. Requires pyserial.
#
# Usage: scripts/reset.sh [PORT]    (default COM4)
PORT="${1:-COM4}"
python -c "
import serial, time
s = serial.Serial('$PORT', 115200)
s.setDTR(False); s.setRTS(True); time.sleep(0.15); s.setRTS(False); s.close()
print('reset pulse sent on $PORT')
"
