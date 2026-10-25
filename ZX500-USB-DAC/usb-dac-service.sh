#!/system/bin/sh
# KernelSU late_start service for ZX500 USB-DAC.
# It starts only when /data/adb/usbdac.autostart exists.
# Runtime output is discarded; no log file or periodic flash write is created.

i=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ "$i" -lt 120 ]; do
  sleep 2
  i=$((i+1))
done
sleep 5

if [ -f /data/adb/usbdac.autostart ]; then
  sh /data/local/tmp/usb-dac.sh on >/dev/null 2>&1
fi
