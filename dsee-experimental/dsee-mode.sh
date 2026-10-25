#!/system/bin/sh
# Switch only the PCM bridge; never rebuild USB, flash partitions, or set volume.
BASE=/data/local/tmp/dsee-experimental
BRIDGE=/data/local/tmp/uac2bridge
PIDFILE=/dev/.zx-usbdac.pid
FLAG=/data/adb/usbdac.dsee
FXFLAG=/data/adb/usbdac.effects
TRIAL=/dev/.zx-dsee-trial-v2
LOG=/dev/.zx-dsee.log
LOCK=/dev/.zx-dsee-switch-lock
start_bridge() {
  setsid "$BRIDGE" </dev/null >"$LOG" 2>&1 &
  BP=$!
  printf '%s uac2bridge\n' "$BP" > "$PIDFILE"
  sleep 1
  kill -0 "$BP" 2>/dev/null && [ "$(cat /proc/$BP/comm 2>/dev/null)" = uac2bridge ]
}
stop_bridges() {
  for BP in $(pidof uac2bridge 2>/dev/null); do
    [ "$(cat /proc/$BP/comm 2>/dev/null)" = uac2bridge ] || continue
    case "$(readlink /proc/$BP/exe 2>/dev/null)" in /data/local/tmp/*/uac2bridge*|/data/local/tmp/uac2bridge*) ;; *) echo "Unexpected bridge executable" >&2;return 1;; esac
    START=$(awk '{print $22}' /proc/$BP/stat 2>/dev/null)
    kill "$BP" 2>/dev/null
    I=0
    while kill -0 "$BP" 2>/dev/null && [ "$I" -lt 20 ]; do sleep 0.05; I=$((I+1)); done
    [ "$(awk '{print $22}' /proc/$BP/stat 2>/dev/null)" = "$START" ] && kill -9 "$BP" 2>/dev/null
  done
  rm -f "$PIDFILE"
  return 0
}
switch_mode() {
  MODE=$1
  FXMODE=${2:-keep}
  [ -x "$BASE/original/uac2bridge" ] || return 1
  [ "$MODE" != on ] || [ -x "$BASE/uac2bridge" ] || return 1
  mkdir "$LOCK" 2>/dev/null || { echo "Switch already in progress" >&2;return 1; }
  trap 'rmdir "$LOCK" 2>/dev/null' EXIT
  FXREQ=0; [ ! -f "$FXFLAG" ] || FXREQ=1
  case "$FXMODE" in on) FXREQ=1;; off) FXREQ=0;; esac
  DAC_BOUND=0
  if [ -n "$(cat /config/usb_gadget/g1/UDC 2>/dev/null)" ]; then
    case "$(readlink /config/usb_gadget/g1/configs/b.1/f1 2>/dev/null)" in *functions/uac2.0) DAC_BOUND=1;; esac
  fi
  SRC=$BASE/original/uac2bridge
  if [ "$MODE" = on ] || [ "$FXREQ" = 1 ]; then SRC=$BASE/uac2bridge; fi
  cp "$SRC" "$BRIDGE.next" && chmod 755 "$BRIDGE.next" || return 1
  stop_bridges || return 1
  mv -f "$BRIDGE.next" "$BRIDGE" || return 1
  rm -f /dev/.zx-dsee-trial "$TRIAL"
  if [ "$MODE" = on ]; then touch "$FLAG" || return 1; else rm -f "$FLAG"; fi
  if [ "$FXREQ" = 1 ]; then touch "$FXFLAG" || return 1; else rm -f "$FXFLAG"; fi
  if [ "$DAC_BOUND" = 1 ] && ! start_bridge; then
    echo "Experimental start failed; restoring original" >&2
    rm -f "$FLAG" "$FXFLAG" /dev/.zx-dsee-trial "$TRIAL"
    stop_bridges
    cp "$BASE/original/uac2bridge" "$BRIDGE.next" && chmod 755 "$BRIDGE.next" && mv -f "$BRIDGE.next" "$BRIDGE" && start_bridge
    return 1
  fi
  echo "DSEE requested=$MODE bridge=$BP"
}
appstatus() {
  echo "CONTROL_VERSION=4"
  [ -x "$BASE/original/uac2bridge" ] && [ -x "$BASE/uac2bridge" ] && echo "INSTALLED=1" || echo "INSTALLED=0"
  REQ=0; [ ! -f "$FLAG" ] || REQ=1
  echo "REQUESTED=$REQ"
  USB=0
  if [ -n "$(cat /config/usb_gadget/g1/UDC 2>/dev/null)" ]; then
    case "$(readlink /config/usb_gadget/g1/configs/b.1/f1 2>/dev/null)" in *functions/uac2.0) USB=1;; esac
  fi
  echo "USB_BOUND=$USB"
  P=$(cut -d ' ' -f1 "$PIDFILE" 2>/dev/null)
  case "$P" in ''|*[!0-9]*) P=;; esac
  LIVE=0
  [ -z "$P" ] || { [ "$(cat /proc/$P/comm 2>/dev/null)" != uac2bridge ] || LIVE=1; }
  echo "PROCESS_PID=$P"
  IN=$(awk '/^rate:/{print $2}' /proc/asound/card2/pcm0c/sub0/hw_params 2>/dev/null)
  OUT=$(awk '/^rate:/{print $2}' /proc/asound/card1/pcm0p/sub0/hw_params 2>/dev/null)
  PS=$(awk '/^state:/{print $2}' /proc/asound/card1/pcm0p/sub0/status 2>/dev/null)
  echo "INPUT_RATE=$IN"
  echo "OUTPUT_RATE=$OUT"
  echo "PLAYBACK_STATE=$PS"
  STATE=off
  if [ "$REQ" = 1 ]; then
    if [ "$USB" != 1 ]; then STATE=waiting
    elif [ "$LIVE" != 1 ]; then STATE=error
    elif [ -z "$IN" ] || [ -z "$OUT" ] || [ "$PS" != RUNNING ]; then STATE=idle
    else
      STATE=bypass
      case "$IN:$OUT" in 44100:176400|48000:192000|88200:176400|96000:192000)
        [ "$(readlink /proc/$P/exe 2>/dev/null)" != "$BRIDGE" ] || STATE=active;;
      esac
    fi
  fi
  echo "STATE=$STATE"
  [ ! -f "$FXFLAG" ] && echo "EFFECTS_REQUESTED=0" || echo "EFFECTS_REQUESTED=1"
  if [ -f "$FXFLAG" ] && [ "$LIVE" = 1 ] && [ "$(sed -n 's/^FX_PID=//p' /dev/.zx-effects-status 2>/dev/null)" = "$P" ]; then
    sed -n '/^EQ_ACTIVE=/p; /^DC_ACTIVE=/p; /^DC_TYPE=/p; /^FX_DIRECT=/p; /^FX_UNAVAILABLE=/p; /^FX_RATE=/p' /dev/.zx-effects-status
  else
    printf 'EQ_ACTIVE=0\nDC_ACTIVE=0\nDC_TYPE=0\nFX_DIRECT=0\nFX_UNAVAILABLE=0\nFX_RATE=0\n'
  fi
  STAT=$(grep '^\[STAT ' "$LOG" 2>/dev/null | tail -n 1)
  DSPSTAT=$(grep '^DSEE STAT ' "$LOG" 2>/dev/null | tail -n 1)
  XR=$(printf '%s\n' "$STAT" | tr ' ' '\n' | sed -n 's/^XRUN=//p')
  CL=$(printf '%s\n' "$DSPSTAT" | tr ' ' '\n' | sed -n 's/^clips=//p')
  echo "XRUN=$XR"
  echo "CLIPS=$CL"
  [ ! -f /data/adb/usbdac.autostart ] && echo "DAC_AUTOSTART=0" || echo "DAC_AUTOSTART=1"
  return 0
}
case "$1" in
 on|off) switch_mode "$1";;
 effects-on|effects-off)
   DM=off; [ ! -f "$FLAG" ] || DM=on
   switch_mode "$DM" "${1#effects-}";;
 rollback) switch_mode off off;;
 appstatus) appstatus;;
 trial|trial-effects)
   if [ "$1" = trial-effects ]; then
     DM=off; [ ! -f "$FLAG" ] || DM=on
     switch_mode "$DM" on || exit 1
   else switch_mode on || exit 1; fi
   TOKEN="$(date +%s)-$$"
   echo "$TOKEN" > "$TRIAL"
   setsid /system/bin/sh -c 'sleep 900; [ "$(cat /dev/.zx-dsee-trial-v2 2>/dev/null)" != "$1" ] || /data/local/tmp/dsee-experimental/dsee-mode.sh rollback' zx-dsee-watchdog "$TOKEN" </dev/null >/dev/null 2>&1 &
   echo "Trial rollback armed for 900 seconds, generation=$TOKEN";;
 commit) rm -f /dev/.zx-dsee-trial "$TRIAL";echo "Trial rollback cancelled";;
 status)
   [ -f "$FLAG" ] && echo "DSEE requested=on" || echo "DSEE requested=off"
   cat "$PIDFILE" 2>/dev/null
   cat /proc/asound/card2/pcm0c/sub0/hw_params 2>/dev/null
   cat /proc/asound/card1/pcm0p/sub0/hw_params 2>/dev/null
   tail -n 12 "$LOG" 2>/dev/null
   exit 0;;
 *) echo "usage: $0 on|off|effects-on|effects-off|rollback|trial|trial-effects|commit|status|appstatus";exit 2;;
esac
