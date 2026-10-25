# ZX500 USB-DAC

**English** | [简体中文](README.zh-CN.md)

Turn a Sony NW-ZX505 into a native UAC2 USB DAC: plug it into a computer and it
becomes a hi-res sound card, with the sample rate, volume and mute all controlled
from the computer.

- Appears as a single audio output device named **ZX500 USB-DAC**
- Stereo 24-bit PCM at 44.1 / 48 / 88.2 / 96 / 176.4 / 192 / 352.8 / 384 kHz
- Rate, volume and mute chosen on the computer — no replug, nothing to press on
  the player
- A configurable ceiling caps how loud the computer can drive the hardware
- macOS and Windows 11; direct, USB hub and Thunderbolt dock connections
- Recovers on its own from rate changes, host stalls and stream restarts
- Android app sounds cannot retune the audio clock while USB playback is running
- Runs silently: no log files, no periodic writes to flash

Audio path: UAC2 adaptive OUT → ALSA capture → `uac2bridge` → Sony `card1`
playback.

> This is an unofficial modification. It unlocks the bootloader, replaces the
> kernel and erases the player twice along the way. Read [Safety and
> recovery](#safety-and-recovery) before you start.
>
> **You do this at your own risk.** It is not affiliated with or endorsed by Sony,
> it voids your warranty, and it can leave the player unusable. The authors accept
> no responsibility for damage to your device or loss of your data. Nothing here
> is provided with any warranty — see the licence.

---

## Before you start

**Hardware and firmware**

- A Sony NW-ZX505 running firmware **4.04.00 CHN**. That is the only combination
  this has been built and tested on.

  Other firmware versions (4.06 International, for example) very likely need
  nothing more than their own stock `boot.img` — the packer copies your kernel
  into whatever image you give it — but none of them have been tried, so the
  checksum below will not match and you would be the first.

**On your computer**

- Android platform tools — `adb` and `fastboot`
- **Docker.** All compiling happens inside pinned containers, so you do not need
  a toolchain of your own. Version 23.0 or newer (older needs
  `DOCKER_BUILDKIT=1`), and about **5 GB** of free space.
  On an Intel/AMD machine the image runs emulated; Docker Desktop handles that,
  a plain Docker Engine needs `docker run --privileged --rm tonistiigi/binfmt
  --install arm64` once.

**The stock boot image — get this first**

You need Sony's own `boot.img` from firmware 4.04.00 CHN. The build packs your
new kernel into it, so **without this file you cannot finish step 3.** It is
Sony's proprietary firmware and is not included here.

Download the unpacked 4.04 CHN fastboot firmware linked from the
[community guide][walkman] and take `boot.img` out of it. Check it before you
rely on it:

```bash
shasum -a 256 boot.img
# e2e398948de76977549d922f8c0891969a3277813e83c75462bbf184c24a193a
```

You cannot copy it off the player instead — that would need the root this
procedure has not produced yet.

---

## Installing

Six steps, in order. Steps 1 and 2 each erase the player, so do them before you
put any music or settings on it.

### 1. Unlock the bootloader — erases the player

Skip if it is already unlocked.

On the player, open **Settings → About** and tap **Build number** seven times to
reveal Developer options. In there, turn on **OEM unlocking** and **USB
debugging**. Connect the cable and accept the authorisation prompt.

```bash
adb reboot bootloader
fastboot oem unlock          # not `flashing unlock`
fastboot reboot
```

`oem unlock` prints nothing at all while it erases the player — about 80 seconds
on 4.04.00 CHN, and the [community guide][walkman] reports up to ~500 seconds on
other devices. **Let it finish; do not unplug.**

<a id="enable-adb"></a>The erase turns USB debugging off again, so before going
on: finish the setup wizard, then Settings → About → tap **Build number** seven
times, turn **USB debugging** back on, and accept the prompt when you reconnect.

### 2. Disable verified boot — erases the player

A modified kernel will not pass verified boot, so it has to be switched off.

```bash
adb reboot bootloader
fastboot --disable-verity --disable-verification flash vbmeta blank_vbmeta.img
fastboot reboot
```

Both flags are required. `vbmeta` resolves to the active slot on its own.

The player will now fail to start and land in recovery saying it cannot load
Android — **this is expected, not a failure.** Choose **factory reset** there
(volume keys move, power confirms) and it boots normally with verified boot off.

It may instead sit on the Walkman logo looking frozen. That is also expected: if
nothing has changed after a minute or two, hold power to force a restart, and the
next attempt reaches recovery within seconds.

Then re-enable USB debugging again, [the same way as before](#enable-adb).

### 3. Build the kernel and pack it

```bash
docker/zxbuild all
```

That builds the container images, clones the kernel source, compiles the kernel
and the bridge, and runs the resampler test. First run takes a few minutes and
downloads a few GB; later runs reuse everything.

Then pack your kernel into Sony's boot image:

```bash
kernel/pack-boot.py /path/to/boot.img \
  kernel/out/arch/arm64/boot/Image boot-zx500-usb-dac.img
```

`/path/to/boot.img` is the stock image you obtained above.

### 4. Flash the kernel and install KernelSU

```bash
adb reboot bootloader
./flash.sh boot-zx500-usb-dac.img
```

`flash.sh` checks the image, confirms the active slot and writes `boot_a` only.
It never touches vbmeta and never erases anything. Type `flash` when it asks.

Root comes from this kernel — `kernel/kernel.config` enables KernelSU, so the
kernel half is already in the image you just flashed. It needs the **matching**
manager app; a current KernelSU release targets a much newer kernel half and will
not connect.

```bash
curl -LO https://github.com/tiann/KernelSU/releases/download/v0.6.7/KernelSU_v0.6.7_11210-release.apk
shasum -a 256 KernelSU_v0.6.7_11210-release.apk
# 25b117052e972210d9b3dcf826d6a9cbf96bd2ae5e098d622104304d21282dc4
adb install KernelSU_v0.6.7_11210-release.apk
```

Open **KernelSU** on the player and grant root to **shell**. Step 5 will not work
without it, and this is the one part that cannot be done from the computer.

### 5. Install the player-side files

```bash
./deploy.sh
```

For Wi-Fi adb or when several devices are attached:

```bash
ADB_SERIAL=192.168.1.20:5555 ./deploy.sh
```

This copies the bridge, the control script, the boot service and the app onto the
player, and switches on "enter DAC mode at boot". Reboot when it finishes.

### 6. Grant the app root

Open **ZX DAC** on the player once and grant it root in KernelSU.

Done — the player is a USB DAC. It enters DAC mode automatically on every boot.

---

## Using it

Choose the sample rate, volume and mute **on the computer**; nothing needs to be
touched on the player. On macOS that is Audio MIDI Setup or Sound settings, on
Windows the Sound settings.

The **ZX DAC** app switches between DAC mode and ordinary USB (MTP), shows what
the computer has selected, and sets the volume ceiling.

### If the computer does not notice the DAC

Connected through a USB hub or a monitor dock, the computer picks the DAC up by
itself. Plugged **straight into a Mac it usually does not**: the player is
working and streaming, but the computer still shows the previous device and no
new audio output. Either of these avoids it:

- **Connect the cable after the player has finished booting** — a fresh
  connection always works, and this is the easiest habit.
- If the cable was already plugged in, unplug it and plug it back in once.

The player swaps its USB identity in place while running; a hub reports that as a
port change, while a directly attached port often does not notice at all. Root
cause not established.

### Volume ceiling

The computer's volume slider runs from silent up to a ceiling you choose, one
step per 0.5 dB. The ceiling is what decides how loud USB playback can get. It
defaults to **100** on Sony's 0–120 scale (−10 dB); 120 is full scale.

Set it in the app, or:

```bash
adb shell su -c 'sh /data/local/tmp/usb-dac.sh maxvol 100'
```

It takes effect the next time DAC mode starts. If the player's own volume is
above the ceiling when DAC mode starts, it comes up at 30% of the ceiling instead,
so playback never begins unexpectedly loud.

### Commands on the player

```sh
su -c 'sh /data/local/tmp/usb-dac.sh status'
su -c 'sh /data/local/tmp/usb-dac.sh appstatus'
su -c 'sh /data/local/tmp/usb-dac.sh dac'         # enter DAC mode, and at every boot
su -c 'sh /data/local/tmp/usb-dac.sh mtp'         # ordinary USB from the next boot
su -c 'sh /data/local/tmp/usb-dac.sh off'         # stop now
su -c 'sh /data/local/tmp/usb-dac.sh maxvol 100'  # volume ceiling, 1–120
```

`appstatus` also prints `LASTERROR`. If DAC mode ever fails to start, the reason
is recorded there and survives the reboot.

### Diagnostics

`bridge/uac2bridge-diag` is the same program with 10-second statistics on stderr.
Copy it over `/data/local/tmp/uac2bridge` and run it **by hand**, having stopped
the running bridge first, with stderr sent somewhere volatile such as `/dev`.
Starting it through `usb-dac.sh` produces no statistics: the launcher discards
the bridge's output so that playback never writes to flash. An optional `-1` or
`+1` argument forces a drift correction direction. Put the normal bridge back
afterwards.

---

## How it is built

You do not need this section to install the DAC.

`docker/zxbuild` runs every build step inside one pinned container:

```bash
docker/zxbuild images    # build the toolchain image
docker/zxbuild fetch     # clone the Sony kernel tree at its pinned commit
docker/zxbuild kernel    # -> kernel/out/arch/arm64/boot/Image
docker/zxbuild bridge    # -> bridge/uac2bridge, bridge/uac2bridge-diag
docker/zxbuild test      # resampler test
docker/zxbuild all       # everything above
docker/zxbuild shell     # a shell inside the image
```

The image is `linux/arm64` and compiles natively rather than cross-compiling. The
kernel tree and tinyalsa live in a named volume (`zx500-src`), not in the image.
Flashing and deployment stay on your computer: `fastboot` and `adb` need USB
access that containers on macOS do not have.

| Component | Pinned to |
|---|---|
| Sony/community kernel | [`97lily/2019_android_walkman`][walkman] commit `1b316da17a6f4c50511f46498de0a16dd53231d9`, directory `kernel_imx/` |
| tinyalsa | commit `e43025bbf702eb7dd8edd48c1eb50530c60f1de8` |
| Build image | Ubuntu 16.04, arm64 — GCC 4.9.3 for the kernel, GCC 5.4 for the bridge |
| Sony boot image | firmware 4.04.00 CHN, SHA-256 `e2e398948de76977549d922f8c0891969a3277813e83c75462bbf184c24a193a` |

**Why a distribution that reached end of life in 2021, on purpose.** GCC 4.9 is
not nostalgia: it is the only compiler whose kernel Image fits this bootloader's
28,940,800-byte slot, where a modern one produces roughly 32 MB. Keeping the
bridge in the same image buys something as valuable — that archive is frozen.
Packages there are never updated, so a rebuild years from now installs exactly
what it installs today. The compiler cannot pick up new warnings that trip
`-Werror`, and no version pin can go stale and fail to resolve. Both are real
risks on a release that is still being maintained.

Because the toolchain cannot drift, the bridge is reproducible: `docker/zxbuild
bridge` rewrites the two binaries with exactly the hashes already in
`SHA256SUMS`, so `shasum -a 256 -c SHA256SUMS` still passes afterwards.

Xenial's arm64 packages are still served from `ports.ubuntu.com`. When Ubuntu
eventually retires that, end-of-life releases move to `old-releases.ubuntu.com`
and stay — it still carries releases from 2004. `docker/Dockerfile` takes an
`APT_MIRROR` build argument for that day.

The kernel Image is padded, so its size tells you nothing about whether the patch
was applied; `kernel/build.sh` asserts on source content instead. Warnings from
the vendor tree during the build are normal.

Rebuilding the app needs Android SDK build-tools 37, platform 34 and a JDK:

```bash
app/build.sh
```

It creates `app/local.keystore` unless `APK_KEYSTORE`, `APK_KEY_ALIAS` and
`APK_KEY_PASS` are set. Keep that key — you need the same one to install your
own updates over your own build.

### Repository layout

| Path | Purpose |
|---|---|
| `kernel/` | Kernel patch, config, build script and boot packer |
| `bridge/` | Bridge source, build script and both binaries |
| `docker/` | Pinned build environment; `docker/zxbuild` drives it |
| `usb-dac.sh` | On-device gadget and bridge controller |
| `usb-dac-service.sh` | KernelSU boot service |
| `deploy.sh` | Installs the player-side files |
| `flash.sh` | Flashes a locally packed boot image to `boot_a` |
| `app/`, `ZX-DAC.apk` | On-device control app |
| `blank_vbmeta.img` | Verified-boot payload used once in step 2 |
| `tests/resampler-test.c` | Drift renderer safety test |

---

## Safety and recovery

- The computer can only drive the hardware up to the configured ceiling. If the
  volume or mute state cannot be read back and confirmed, the DAC refuses to
  start at all rather than play at an unknown level.
- If starting DAC mode fails, USB is put back the way it was and the reason is
  written to `/data/adb/usbdac.lasterror`.
- `flash.sh` writes `boot_a` only. It never touches vbmeta and never erases data.
  It refuses to run if the active slot is not `a`, because writing the inactive
  slot would look like nothing happened at all.
- **`fastboot boot` does nothing on this bootloader** — it reports success and
  boots the installed kernel regardless. Only flashing tells you anything.
- If a kernel will not boot: hold **Vol− + FF** through a restart to reach
  fastboot, then flash a known-good Sony boot image to `boot_a`. Keep the stock
  image you downloaded for exactly this.
- The reference device ran with SELinux permissive. Whether DAC mode also works
  under enforcing has not been tested.

## License

GPL-2.0 — see [LICENSE](LICENSE).

`kernel/zx500-usb-dac.patch` is a derivative work of the Linux kernel and is
necessarily GPL-2.0; the rest of the repository is released under the same
licence. The bridge statically links [tinyalsa](https://github.com/tinyalsa/tinyalsa)
(BSD-3-Clause), fetched at build time rather than vendored here. Sony's firmware
and stock boot image are proprietary and are not redistributed.

[walkman]: https://github.com/97lily/2019_android_walkman
