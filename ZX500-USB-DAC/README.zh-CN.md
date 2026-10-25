# ZX500 USB-DAC

[English](README.md) | **简体中文**

把 Sony NW-ZX505 改造成原生 UAC2 USB DAC：接上电脑，它就是一块 hi-res 声卡，
采样率、音量、静音全部在电脑上控制。

- 在电脑上表现为单一音频输出设备，名称为 **ZX500 USB-DAC**
- 双声道 24-bit PCM：44.1 / 48 / 88.2 / 96 / 176.4 / 192 / 352.8 / 384 kHz
- 采样率、音量、静音都在电脑上选择——不用插拔，播放器上什么都不用按
- 可配置的上限决定电脑最高能把硬件驱动到多响
- 支持 macOS 和 Windows 11，可直连、通过 USB Hub 或雷电扩展坞使用
- 采样率切换、主机停顿、音频流重启后都能自行恢复
- USB 播放期间，Android 应用提示音无法改动音频时钟
- 运行时保持静默：不写日志，不进行周期性闪存写入

音频通路：UAC2 adaptive OUT → ALSA capture → `uac2bridge` → Sony `card1` playback。

> 这是非官方改造。过程中会解锁 bootloader、替换内核，并且**两次清空播放器**。
> 动手前请先读[安全与恢复](#安全与恢复)。
>
> **风险由你自行承担。** 本项目与 Sony 无任何关联、未获其认可，会使保修失效，
> 并且可能导致播放器无法使用。作者不对你的设备损坏或数据丢失承担任何责任。
> 本项目不提供任何形式的担保——详见许可证。

---

## 开始之前

**硬件与固件**

- 一台运行 **4.04.00 CHN** 固件的 Sony NW-ZX505。这是唯一经过构建和实测的组合。

  **其它固件版本**（比如 4.06 国际版）很可能只需换用它自己的原厂 `boot.img`
  ——打包工具会把你的内核装进你给它的任何镜像——但都没有人试过，下面那个校验和
  也不会匹配，你会是第一个。

**电脑上**

- Android platform tools —— `adb` 和 `fastboot`
- **Docker**。所有编译都在固定版本的容器里进行，你不需要自备工具链。版本 23.0
  或更新（更早的需要设置 `DOCKER_BUILDKIT=1`），以及约 **5 GB** 可用空间。
  在 Intel/AMD 电脑上镜像通过模拟运行，Docker Desktop 自带该支持；纯 Docker
  Engine 需要先执行一次
  `docker run --privileged --rm tonistiigi/binfmt --install arm64`。

**原厂 boot 镜像——请先准备好这个**

你需要 Sony 4.04.00 CHN 固件里的 `boot.img`。构建过程要把新内核装进它里面，
**没有这个文件，第 3 步就无法完成。** 它属于 Sony 的专有固件，本仓库不提供。

从[社区文档][walkman]中链接的 4.04 CHN 已解包 fastboot 固件里下载并取出 `boot.img`，
使用前先核对：

```bash
shasum -a 256 boot.img
# e2e398948de76977549d922f8c0891969a3277813e83c75462bbf184c24a193a
```

不能改用"从播放器里导出"的办法——那需要本流程尚未产生的 root 权限。

---

## 安装

六个步骤，按顺序执行。第 1、2 步各会清空播放器一次，所以请在往里放音乐和做设置之前完成。

### 1. 解锁 bootloader —— 会清空播放器

已解锁可跳过。

在播放器上打开**设置 → 关于本机**，连续点击**版本号**七次，开发者选项就会出现。
进去打开 **OEM 解锁**和 **USB 调试**。接上数据线，在播放器上确认授权弹窗。

```bash
adb reboot bootloader
fastboot oem unlock          # 不是 flashing unlock
fastboot reboot
```

`oem unlock` 执行期间**没有任何输出**，实际在清空播放器——4.04.00 CHN 上约 80 秒，
[社区文档][walkman]提到其它设备上可能长达约 500 秒。**耐心等它结束，不要拔线。**

<a id="enable-adb"></a>这次清空会把 USB 调试关掉，所以继续之前需要：走完开箱设置向导，
然后 设置 → 关于本机 → 连续点击**版本号**七次，重新打开 **USB 调试**，
重新连接时确认授权弹窗。

### 2. 关闭验证启动 —— 会清空播放器

改过的内核无法通过验证启动，所以必须先把它关掉。

```bash
adb reboot bootloader
fastboot --disable-verity --disable-verification flash vbmeta blank_vbmeta.img
fastboot reboot
```

两个 flag 缺一不可。`vbmeta` 会自动解析到当前槽位。

此时播放器将无法正常启动，会进入 recovery 并提示无法载入 Android——**这是预期现象，
不是失败。** 在那里选择**恢复出厂设置**（音量键移动，电源键确认），之后它就会以关闭
验证启动的状态正常开机。

它也可能停在 Walkman 开机画面上像是卡死了。这同样是预期现象：若过了一两分钟仍无变化，
长按电源键强制重启，下一次通常几秒内就会进入 recovery。

然后再次[按前面的方法](#enable-adb)重新打开 USB 调试。

### 3. 编译内核并打包

```bash
docker/zxbuild all
```

这会构建容器镜像、克隆内核源码、编译内核和 bridge，并运行 resampler 测试。
首次运行需要几分钟并下载数 GB，之后的运行会复用这些内容。

然后把你的内核装进 Sony 的 boot 镜像：

```bash
kernel/pack-boot.py /path/to/boot.img \
  kernel/out/arch/arm64/boot/Image boot-zx500-usb-dac.img
```

`/path/to/boot.img` 就是你前面准备好的原厂镜像。

### 4. 刷入内核并安装 KernelSU

```bash
adb reboot bootloader
./flash.sh boot-zx500-usb-dac.img
```

`flash.sh` 会校验镜像、确认当前活动槽位，且**只写 `boot_a`**。它不碰 vbmeta，
也不清除任何数据。提示时输入 `flash` 确认。

root 来自这个内核——`kernel/kernel.config` 已启用 KernelSU，内核那一半就在你刚刷进去的
镜像里。它需要**版本匹配**的管理器 App；当前版本的 KernelSU 面向新得多的内核端，
无法与之对接。

```bash
curl -LO https://github.com/tiann/KernelSU/releases/download/v0.6.7/KernelSU_v0.6.7_11210-release.apk
shasum -a 256 KernelSU_v0.6.7_11210-release.apk
# 25b117052e972210d9b3dcf826d6a9cbf96bd2ae5e098d622104304d21282dc4
adb install KernelSU_v0.6.7_11210-release.apk
```

在播放器上打开 **KernelSU**，授予 **shell** root 权限。没有这一步第 5 步无法进行，
而且这是唯一无法从电脑端完成的操作。

### 5. 部署播放器端文件

```bash
./deploy.sh
```

使用 Wi-Fi adb 或同时连接多台设备时：

```bash
ADB_SERIAL=192.168.1.20:5555 ./deploy.sh
```

它会把 bridge、控制脚本、开机服务和 App 复制到播放器，并打开"开机自动进入 DAC 模式"。
完成后重启。

### 6. 给 App 授权

在播放器上打开一次 **ZX DAC**，在 KernelSU 中授予其 root。

完成——播放器已经是一台 USB DAC，此后每次开机都会自动进入 DAC 模式。

---

## 日常使用

采样率、音量、静音都**在电脑上**选择，播放器上什么都不用动。macOS 上是 Audio MIDI
设置或声音设置，Windows 上是声音设置。

**ZX DAC** App 用来在 DAC 模式和普通 USB（MTP）之间切换、显示电脑当前的选择、
以及设置音量上限。

### 如果电脑没认出 DAC

通过 USB Hub 或显示器扩展坞连接时，电脑会自动识别。**直插 Mac 则通常不会**：
播放器已经在正常工作，而电脑上仍显示切换前的设备、没有新的音频输出。
以下任一做法都可规避：

- **等播放器开机完成之后再插数据线**——全新接入总能成功，这是最省事的习惯。
- 若线已经插着，把它拔下再插回一次即可。

播放器是在运行中原地更换 USB 身份的；hub 会把这个过程作为端口变化上报，
而直连端口往往根本不会察觉。根因尚未确定。

### 音量上限

电脑的音量条从静音一直到你设定的上限，每一格 0.5 dB。这个上限决定 USB 播放能有多响。
默认是 Sony 0–120 刻度上的 **100**（−10 dB），120 为满刻度。

在 App 里设置，或者：

```bash
adb shell su -c 'sh /data/local/tmp/usb-dac.sh maxvol 100'
```

**下次进入 DAC 模式时生效。** 如果进入 DAC 模式时播放器自身的音量高于上限，
它会以上限的 30% 启动，避免播放一开始就过响。

### 播放器上的命令

```sh
su -c 'sh /data/local/tmp/usb-dac.sh status'
su -c 'sh /data/local/tmp/usb-dac.sh appstatus'
su -c 'sh /data/local/tmp/usb-dac.sh dac'         # 进入 DAC 模式，并设为开机自启
su -c 'sh /data/local/tmp/usb-dac.sh mtp'         # 下次开机起恢复普通 USB
su -c 'sh /data/local/tmp/usb-dac.sh off'         # 立即停止
su -c 'sh /data/local/tmp/usb-dac.sh maxvol 100'  # 音量上限，1–120
```

`appstatus` 还会输出 `LASTERROR`。如果 DAC 模式启动失败，原因会记录在那里，
并且能在重启后读到。

### 诊断

`bridge/uac2bridge-diag` 是同一份程序，额外向 stderr 输出 10 秒一次的统计。
把它覆盖到 `/data/local/tmp/uac2bridge`，先停掉正在运行的 bridge，然后**手动运行**，
把 stderr 重定向到 `/dev` 等易失性位置。经由 `usb-dac.sh` 启动是拿不到统计的：
启动器会丢弃 bridge 的输出，以保证播放过程从不写入闪存。可选参数 `-1` 或 `+1`
用于强制指定漂移校正方向。用完记得把原来的 bridge 换回去。

---

## 构建方式

安装 DAC 并不需要读这一节。

`docker/zxbuild` 在**单个**固定版本容器中执行全部构建步骤：

```bash
docker/zxbuild images    # 构建工具链镜像
docker/zxbuild fetch     # 按 pinned commit 克隆 Sony 内核树
docker/zxbuild kernel    # 产出 kernel/out/arch/arm64/boot/Image
docker/zxbuild bridge    # 产出 bridge/uac2bridge、bridge/uac2bridge-diag
docker/zxbuild test      # resampler 测试
docker/zxbuild all       # 以上全部
docker/zxbuild shell     # 进入镜像的 shell
```

镜像是 `linux/arm64`，采用原生编译而非交叉编译。内核源码树和 tinyalsa 存放在具名
volume（`zx500-src`）中，不打进镜像。刷机和部署留在电脑上执行：`fastboot` 和 `adb`
需要 USB 访问，而 macOS 上的容器没有这个能力。

| 组件 | 固定版本 |
|---|---|
| Sony/社区内核 | [`97lily/2019_android_walkman`][walkman] commit `1b316da17a6f4c50511f46498de0a16dd53231d9`，目录 `kernel_imx/` |
| tinyalsa | commit `e43025bbf702eb7dd8edd48c1eb50530c60f1de8` |
| 构建镜像 | Ubuntu 16.04，arm64——内核用 GCC 4.9.3，bridge 用 GCC 5.4 |
| Sony boot 镜像 | 4.04.00 CHN 固件，SHA-256 `e2e398948de76977549d922f8c0891969a3277813e83c75462bbf184c24a193a` |

**为什么刻意选用 2021 年就已停止支持的发行版。** 选 GCC 4.9 不是怀旧：只有它产出的
内核 Image 装得进这个 bootloader 的 28,940,800 字节槽位，现代编译器要 32 MB 左右。
而把 bridge 也放进同一个镜像，换来的东西同样重要——**这个归档是冻结的**。其中的软件包
永远不会再更新，所以若干年后重建镜像，装到的仍是今天这一套：编译器不可能长出新的警告
去触发 `-Werror`，钉死的版本号也不可能因为失效而无法解析。这两点在仍在维护的发行版上
都是真实存在的风险。

正因为工具链不会漂移，bridge 是可复现的：`docker/zxbuild bridge` 写出的两个二进制，
哈希与 `SHA256SUMS` 中已登记的完全一致，构建后 `shasum -a 256 -c SHA256SUMS` 依然通过。

xenial 的 arm64 软件包目前仍由 `ports.ubuntu.com` 提供。将来 Ubuntu 把停止支持的版本
迁走时，它们会进入 `old-releases.ubuntu.com` 并长期保留（那里至今还留着 2004 年的版本）。
`docker/Dockerfile` 预留了 `APT_MIRROR` 构建参数以备那一天。

内核 Image 带对齐填充，所以**它的体积说明不了补丁是否生效**；`kernel/build.sh`
改为断言源码内容。构建过程中来自厂商源码树的警告属于正常现象。

`docker/Dockerfile` 篇幅很短，如果你不想用 Docker，它就是权威的软件包清单。

重新构建 App 需要 Android SDK build-tools 37、platform 34 和一个 JDK：

```bash
app/build.sh
```

未设置 `APK_KEYSTORE`、`APK_KEY_ALIAS`、`APK_KEY_PASS` 时它会创建
`app/local.keystore`。请保留该密钥——只有用同一个密钥才能覆盖安装你自己的后续版本。

### 仓库结构

| 路径 | 用途 |
|---|---|
| `kernel/` | 内核补丁、配置、构建脚本和 boot 打包工具 |
| `bridge/` | bridge 源码、构建脚本和两个二进制 |
| `docker/` | 固定版本的构建环境，入口是 `docker/zxbuild` |
| `usb-dac.sh` | 设备端 gadget 与 bridge 控制脚本 |
| `usb-dac-service.sh` | KernelSU 开机服务 |
| `deploy.sh` | 部署播放器端文件 |
| `flash.sh` | 将本地打包的 boot 镜像刷入 `boot_a` |
| `app/`、`ZX-DAC.apk` | 设备端控制 App |
| `blank_vbmeta.img` | 第 2 步关闭验证启动时使用的载荷 |
| `tests/resampler-test.c` | 漂移校正渲染器安全测试 |

---

## 安全与恢复

- 电脑最多只能把硬件驱动到所配置的上限。若音量或静音状态无法回读确认，DAC 会**拒绝启动**，
  而不是在音量未知的情况下出声。
- DAC 模式启动失败时，USB 会被恢复原状，失败原因写入 `/data/adb/usbdac.lasterror`。
- `flash.sh` **只写 `boot_a`**，不碰 vbmeta，不清除数据。当前活动槽位不是 `a` 时它会拒绝执行，
  因为写入非活动槽看上去会像什么都没发生。
- **此 bootloader 上 `fastboot boot` 无效**——它会报告成功，但实际启动的仍是已安装的内核。
  只有真正刷入才能说明问题。
- 内核无法启动时：重启过程中按住 **Vol− + FF** 进入 fastboot，然后把已知可用的 Sony boot
  镜像刷回 `boot_a`。请为此保留你下载的那份原厂镜像。
- 参考设备运行在 SELinux permissive 状态。enforcing 下 DAC 模式是否可用尚未测试。

## 许可证

GPL-2.0，见 [LICENSE](LICENSE)。

`kernel/zx500-usb-dac.patch` 是 Linux 内核的派生作品，必然为 GPL-2.0；仓库其余部分
同样以该许可证发布。bridge 静态链接
[tinyalsa](https://github.com/tinyalsa/tinyalsa)（BSD-3-Clause），在构建时获取，
未内置于本仓库。Sony 的固件与原厂 boot 镜像属于专有内容，不在此分发。

[walkman]: https://github.com/97lily/2019_android_walkman
