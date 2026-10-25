#!/system/bin/sh
set -eu
SRC=$1
BASE=/data/local/tmp/dsee-experimental
case "$SRC" in /data/user/0/com.zx.dsee/files/payload|/data/data/com.zx.dsee/files/payload) ;; *) echo "Unexpected payload directory" >&2;exit 2;; esac
[ "$(id -u)" = 0 ] || { echo "Root permission required" >&2;exit 3; }
[ "$(getprop ro.build.display.id)" = 4.08.00 ] || { echo "Unsupported firmware" >&2;exit 4; }
[ "$(sha256sum /vendor/lib/hw/audio.primary.icx1295.so | cut -d ' ' -f1)" = dfd3481e5be322f306acef4fac08ce85b204821570597e72c541a1978ce305b8 ] || { echo "Unsupported Sony audio library" >&2;exit 5; }
[ -f /data/local/tmp/usb-dac.sh ] || { echo "Install the ZX500 USB-DAC base setup first" >&2;exit 6; }
cd "$SRC"
sha256sum -c SHA256SUMS || exit 7
mkdir -p "$BASE/original"
chmod 700 "$BASE"
for NAME in uac2bridge original/uac2bridge dsee-mode.sh; do
    cp "$SRC/$NAME" "$BASE/$NAME.app-next"
    chmod 755 "$BASE/$NAME.app-next"
    mv -f "$BASE/$NAME.app-next" "$BASE/$NAME"
done
# Staging does not replace or restart the running bridge, change the DSEE flag,
# rebuild the USB gadget, or alter volume. User controls mode with the app.
echo "COMPONENTS_READY=1"
