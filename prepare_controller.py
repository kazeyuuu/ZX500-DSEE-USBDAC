"""Apply the verified Sony tinymix enum parsing fix to an upstream controller."""

def fix_headphone_route(controller):
    old='''dac_path(){
  BTL=$(tinymix -D 1 33 2>/dev/null | sed 's/.*: //')
  SE=$(tinymix -D 1 32 2>/dev/null | sed 's/.*: //')
  case "$BTL" in *btl*) AMP=smaster-btl;; *)
    case "$SE" in *pin*) AMP=smaster-se;; *) AMP=smaster-btl;; esac ;; esac
'''
    new=r'''mixer_enum_value(){
  ENUM_TEXT=$(tinymix -D 1 "$1" 2>/dev/null) || return 1
  # Sony tinymix lists every enum choice and prefixes the selected one with >.
  # Matching the entire list selects btl even when the actual value is >none.
  case "$ENUM_TEXT" in
    *'>'*) printf '%s\n' "$ENUM_TEXT" | sed -n 's/.*>\([^[:space:]]*\).*/\1/p';;
    *) printf '%s\n' "$ENUM_TEXT" | sed 's/.*: *//';;
  esac
}

dac_path(){
  BTL=$(mixer_enum_value 33)
  SE=$(mixer_enum_value 32)
  case "$BTL" in btl) AMP=smaster-btl;; *)
    case "$SE" in 3pin|4pin|5pin) AMP=smaster-se;; *) AMP=smaster-btl;; esac ;; esac
'''
    if controller.count(old)!=1:
        raise ValueError('Unexpected upstream dac_path; do not apply an unverified replacement')
    return controller.replace(old,new,1)


def fix_usb_identity(controller):
    """ZX505 4.08: exclusively own configfs while DAC mode is active.

    Sony's combined USB HAL handles gadget functions through HIDL, so writing
    sys.usb.config=none alone does not stop it from restoring MTP/ADB and PID
    0D01. Use the existing 054C:0CF2 DAC identity and restore HAL on exit/error.
    """
    def replace(old,new):
        nonlocal controller
        if controller.count(old)!=1:
            raise ValueError('Unexpected USB identity anchor: '+old[:70])
        controller=controller.replace(old,new,1)
    replace('# ZX500 hi-res USB-DAC control (UAC2-only; keep the USB HAL running).',
            '# ZX505 4.08 USB-DAC control (UAC2-only; HAL restored on exit).')
    replace('restore_usb_config(){\n  RC=$USBCFG_PREV',
            'restore_usb_config(){\n  resume_usb_hal\n  RC=$USBCFG_PREV')
    replace('clear_links(){', '''# Local ZX505 firmware service; stop only during exclusive DAC ownership.
HAL_MARK=/dev/.zx-dac-usb-hal-owned
pause_usb_hal(){
  HS=$(getprop init.svc.usb-hal-1-1)
  case "$HS" in running|restarting)
    touch "$HAL_MARK" || return 1
    stop usb-hal-1-1 || return 1;;
    stopped) ;; *) return 1;;
  esac
  HI=0
  while [ "$HI" -lt 30 ]; do
    [ "$(getprop init.svc.usb-hal-1-1)" = stopped ] && return 0
    sleep 0.1; HI=$((HI+1))
  done
  return 1
}
resume_usb_hal(){
  [ -f "$HAL_MARK" ] || return 0
  start usb-hal-1-1 || return 1
  rm -f "$HAL_MARK"
}
clear_links(){''')
    replace('setprop sys.usb.config none; sleep 1\n  unbind_udc',
            'setprop sys.usb.config none; sleep 1\n  pause_usb_hal || { abort_start "cannot obtain exclusive USB ownership"; return 1; }\n  unbind_udc')
    replace('  SERIAL="${SERIAL}DAC"\n', '''  SERIAL="${SERIAL}DAC"
  write_attr_checked "$G/idVendor" "0x054c" || { abort_start "idVendor readback mismatch"; return 1; }
  write_attr_checked "$G/idProduct" "0x0cf2" || { abort_start "idProduct readback mismatch"; return 1; }
''')
    replace('  setprop sys.usb.config none; sleep 1; setprop sys.usb.config "$(default_usb_config)"',
            '  resume_usb_hal\n  setprop sys.usb.config none; sleep 1; setprop sys.usb.config "$(default_usb_config)"')
    return controller
