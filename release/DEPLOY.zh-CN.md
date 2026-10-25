# ZX505 4.08 USB DAC + DSEE / EQ / DC 部署指南

适用版本：ZX DSEE **0.3.1**，含后续 USB 身份固定修复。整理日期：2026-10-25。

这是基于 [remove/ZX500-USB-DAC](https://github.com/remove/ZX500-USB-DAC) 的非官方扩展。它把电脑发送的 PCM 接入播放器已经安装的 Sony 处理器，再输出到播放器耳机口。仓库提供适配代码和控制 App；Sony 固件、库、模型、解密配置和已打包的 boot 镜像不随源码分发。

当前只实测 **国行 NW-ZX505，Android 9，4.08.00 / build 337**。不要通过删除版本或 HAL 哈希校验来安装到其他型号、地区或固件。已编译 APK 位于仓库根目录，也可按下文从源码构建。

## 1. 选择部署路线

| 当前设备 | 从哪里开始 |
|---|---|
| 已运行兼容的 USB DAC 内核，KernelSU 和 shell root 正常 | 第 3 节：准备源码和工具，然后部署控制器/App |
| 已安装 ZX DSEE，仅升级到 0.3.1 或补 USB 名称修复 | 第 6 节；不要重复解锁、刷 vbmeta 或写入原桥接 |
| 完整原厂系统，尚未解锁和 root | 先完成第 2 节的固件、内核和槽位准备 |

仅安装 APK 不能让原厂内核获得 USB DAC 功能。第一次内核改造涉及解锁和关闭验证启动，实机流程清空内部数据两次；先备份并取出 microSD。后续 App/控制器升级不需要再次清空或刷分区。

## 2. 原厂系统：先准备内核与 root

这一阶段需要独立完成后再部署扩展。参考上游固定提交的 [中文安装说明](https://github.com/remove/ZX500-USB-DAC/blob/af3a7fba2e9043739f406d88690052c2a86416a6/README.zh-CN.md)，但必须应用以下 4.08 差异。

### 2.1 原厂镜像和备份

使用自己设备所对应的国行 4.08 原厂 UPG，并在本地提取 `boot.img`、`vbmeta.img` 等恢复文件。索尼的 [4.08 更新说明](https://service.sony.com.cn/download/firm/00002909.html) 和 [国行更新清单](https://info.update.sony.net/PA001/NW-ZX500Series_0003/info/info.xml) 是固件来源；清单可能随时间更新，不要把其中的新版本自动当成 4.08。解包方法参考 [2019_android_walkman](https://github.com/97lily/2019_android_walkman)。

本次验证使用的 4.08 数据：

| 文件 | SHA-256 |
|---|---|
| `NW-ZX500_0003_V4_08_00_NW_WM_FW.UPG` | `9a64b1c8e83b6b79079c1f0dc63f298c91ed6d02257a37e87a5deead6bb3b216` |
| 提取的原厂 `boot.img`，48 MiB | `4e1ab4496ad201a0c7fcbe4810a6d8fd857b79a120a7fc8de7cc0fcbb3d08c1f` |

UPG 不能直接作为 boot 镜像刷写。应校验下载文件、解密后的 OTA 校验和、payload 分区哈希及 boot 头。解密配置只保存在自己的本地私有目录，不提交到仓库。

### 2.2 记录槽位，再解锁

在第一次进入 bootloader 前，通过已授权的 ADB 记录：

```powershell
adb shell getprop ro.build.display.id
adb shell getprop ro.build.fingerprint
adb shell getprop ro.boot.slot_suffix
```

实测设备原先是 **b 槽 4.08**，解锁后自动切到 **a 槽旧 4.04**。这只是本次设备记录，不代表每台机器的 4.08 都在 b 槽。解锁后必须重新核对活动槽位和系统版本。

上游解锁命令为 `fastboot oem unlock`，不是 `fastboot flashing unlock`。需要先在开发者选项开启 OEM 解锁和 USB 调试。解锁会清空内部数据，之后需重新完成首次设置、开启并授权 USB 调试。

本机普通 Windows fastboot 未识别设备，使用 NXP 官方 **uuu 1.5.21** 才能访问 bootloader。[UUU 官方说明](https://github.com/nxp-imx/mfgtools/wiki/UUU) 与 [发行页](https://github.com/nxp-imx/mfgtools/releases) 可用于核对工具。解锁命令出现读取超时不等于设备未执行：先等待清空完成、再只读检查 `unlocked`，不要连续重发解锁请求。

### 2.3 编译与打包 4.08 内核

在支持上游 ARM64 容器的 Linux / WSL2 Docker 环境中，使用固定源码：

```bash
git clone https://github.com/remove/ZX500-USB-DAC.git upstream-full
git -C upstream-full checkout af3a7fba2e9043739f406d88690052c2a86416a6
cd upstream-full
docker/zxbuild all
python3 kernel/pack-boot.py /path/to/stock-4.08/boot.img \
  kernel/out/arch/arm64/boot/Image /path/to/boot-zx505-4.08.img
```

上游内核来源固定为 `97lily/2019_android_walkman` 提交 `1b316da17a6f4c50511f46498de0a16dd53231d9`。本机验证过内核大小、4.08 原厂模块的符号 CRC、上游重采样测试，以及打包后的 header/ramdisk/尾部保持不变。自己的构建也应检查这些结果，不要仅凭输出文件存在就刷入。

### 2.4 写入前的检查点

1. 明确目标槽位承载的是 4.08；解锁后重新检查，不沿用猜测。
2. 对目标槽的 vbmeta 同时禁用验证启动与 verity。实机 flags=3，随后在 Recovery 执行第二次恢复出厂设置，再确认启动版本为 4.08。
3. 只向已核对的目标 `boot_a` 或 `boot_b` 写入由该版本原厂 boot 打包的镜像。
4. **不要直接运行上游 `flash.sh`**：固定版本脚本只写 `boot_a`，不适合不经检查地用于本次 b 槽场景。
5. 不确定 fastboot/uuu、槽位或镜像对应关系时，停在这里核对；不要把本机的 `b` 硬编码复制到其他设备。

完成后安装匹配内核的 [KernelSU v0.6.7 / 11210](https://github.com/tiann/KernelSU/releases/tag/v0.6.7)，不要直接换成新内核系列的管理器。该版 APK 本次校验值为 `25b117052e972210d9b3dcf826d6a9cbf96bd2ae5e098d622104304d21282dc4`。允许 shell root，确认 `adb shell su -c id` 返回 UID 0，并验证 4.08 正常启动、Wi-Fi 正常。

本文不提供跳过上述检查的一键刷写脚本。源码包的主部署范围是下面的用户态扩展。

## 3. 电脑环境和源码构建

以下命令在源码根目录的 **PowerShell** 中执行；第 2 节内核构建块是 Linux shell 命令，不要混用。

需要 Python 3、Git、JDK 17、Android NDK r27c、Android SDK build-tools 35.0.0、Android 34 平台，以及 [Google Platform-Tools](https://developer.android.com/tools/releases/platform-tools)。SDK/NDK 由使用者自行安装和接受许可。

```powershell
$env:JAVA_HOME = 'C:\Dev\jdk-17'
$env:ZX_ANDROID_BUILD_TOOLS = 'C:\Android\Sdk\build-tools\35.0.0'
$env:ZX_ANDROID_JAR = 'C:\Android\Sdk\platforms\android-34\android.jar'
$env:ZX_NDK_TOOLCHAIN = 'C:\Android\Sdk\ndk\27.2.12479018\toolchains\llvm\prebuilt\windows-x86_64'

python release/prepare_dependencies.py
python dsee-experimental/build.py bridge arm
python dsee-app/build.py
```

`prepare_dependencies.py` 只获取固定提交的上游回退桥接，并核对 SHA-256；也可使用 `--original PATH` 指定已有文件。它不会下载 Sony 固件或库。源码包已包含 patched tinyalsa 所需源码。

公开仓库根目录提供 `ZX-DSEE-0.3.1.apk`；自己构建的输出为 `images/dsee-app/ZX-DSEE-0.3.1.apk`；`build-manifest.json` 包含本次 APK 和 payload 哈希。首次构建的签名密钥位于忽略的 `private/android-signing/`，维护者需保存自己的密钥。重新签名的 APK 哈希会与本地已验证发布构建不同，且不能直接覆盖其他签名的同包名应用。

生成本机控制器：

```powershell
python release/prepare_usb_controller.py
```

输出 `images/controller/usb-dac.sh`，包含耳机接口识别修复和 USB 身份固定修复，默认不自动启用局域网 ADB。如明确需要并接受在本地网络提供已授权的 ADB 调试，改用：

```powershell
python release/prepare_usb_controller.py --allow-network-adb
```

USB 身份修复是**独立控制脚本更新**，仅安装 ZX DSEE 0.3.1 APK 不会替换 `/data/local/tmp/usb-dac.sh`。不要漏掉第 5 或第 6 节的控制器部署。

## 4. 连接并核对目标设备

```powershell
$Adb = '.\tools\platform-tools\adb.exe'
& $Adb devices -l
$Device = '替换为上一步已授权设备的序列号'
if ($Device -like '替换*') { throw '请先填写目标设备序列号' }
& $Adb -s $Device shell getprop ro.build.display.id
& $Adb -s $Device shell 'su -c "id -u"'
& $Adb -s $Device shell 'su -c "sha256sum /vendor/lib/hw/audio.primary.icx1295.so"'
```

期望：`4.08.00`、UID `0`、HAL SHA-256：

```text
dfd3481e5be322f306acef4fac08ce85b204821570597e72c541a1978ce305b8
```

USB DAC 模式使用纯音频 USB 接口，USB ADB/MTP 会断开。后续可在播放器 App 上操作；需要电脑诊断时，先在普通 USB 模式开启 Wi-Fi 调试连接：

```powershell
& $Adb -s $Device tcpip 5555
$Device = '替换为播放器当前局域网IP:5555'
if ($Device -like '替换*') { throw '请先填写播放器地址' }
& $Adb connect $Device
& $Adb -s $Device shell 'su -c "id -u"'
```

电脑和播放器需处于可互通的网络。不要把自己设备的 IP、序列号、ADB 授权公钥或解密配置写进公开文档。

## 5. 首次部署基础控制器和扩展

仅用于已经完成第 2 节、但尚未部署用户态 DAC 组件的设备。已有 DSEE 用户走第 6 节，不重新覆盖正在运行的桥接。

准备上游完整固定版本以取得原 ZX DAC APK；源码包中 `ZX500-USB-DAC/` 是构建所需的源码子集，不冒充完整上游仓库：

```powershell
git clone https://github.com/remove/ZX500-USB-DAC.git upstream-full
git -C upstream-full checkout af3a7fba2e9043739f406d88690052c2a86416a6
Get-FileHash .\upstream-full\ZX-DAC.apk -Algorithm SHA256
```

如果第 2 节已在当前目录取得该固定版本，不重复 clone。上游 ZX DAC APK 的期望哈希：`4523eb4f154ce1d883126b38b695186f3a2bd0912ed5a0b839c313cc1ab45852`。先安装两个 App，尚不进入 DAC：

```powershell
& $Adb -s $Device install -r .\upstream-full\ZX-DAC.apk
& $Adb -s $Device install -r .\images\dsee-app\ZX-DSEE-0.3.1.apk
& $Adb -s $Device push .\ZX500-USB-DAC\bridge\uac2bridge /data/local/tmp/uac2bridge
& $Adb -s $Device push .\images\controller\usb-dac.sh /data/local/tmp/usb-dac.sh
& $Adb -s $Device push .\ZX500-USB-DAC\usb-dac-service.sh /data/local/tmp/usb-dac-service.sh
& $Adb -s $Device shell 'su -c "chmod 755 /data/local/tmp/uac2bridge /data/local/tmp/usb-dac.sh /data/local/tmp/usb-dac-service.sh"'
& $Adb -s $Device shell 'su -c "sh -n /data/local/tmp/usb-dac.sh"'
```

每条命令成功后再执行下一条；不要忽略 push、校验或安装错误。在 KernelSU 中允许 **shell、ZX DAC（com.zx.dac）、ZX DSEE（com.zx.dsee）** 的 root 权限。

打开 ZX DSEE，让它完成组件校验与准备。App 会保存上游原桥接供回退；它不会自动重新刷内核。插好耳机、暂停电脑音乐，随后在 App 中进入 USB DAC，选择 DSEE 和“应用本机 EQ/DC”。

手动验证成功后再配置开机自启动：

```powershell
& $Adb -s $Device shell 'su -c "mkdir -p /data/adb/service.d && cp /data/local/tmp/usb-dac-service.sh /data/adb/service.d/usb-dac-service.sh && chmod 755 /data/adb/service.d/usb-dac-service.sh && touch /data/adb/usbdac.autostart"'
```

此时 `$Device` 必须是仍在线的调试连接；若没有启用网络 ADB，可使用 ZX DAC App 的 DAC/自启动操作，不依赖已经断开的 USB ADB。

## 6. 已有用户升级

暂停音乐。App 升级使用同一签名：

```powershell
& $Adb -s $Device install -r .\images\dsee-app\ZX-DSEE-0.3.1.apk
```

首次打开新版会自动暂存和校验组件，但不会立即替换正在运行的音频进程。关闭再打开所需功能开关，才会启用新桥接。退出旧 EQ 时整体音量可能恢复，保持音乐暂停，重新播放前从低音量开始。

补 USB 身份修复时，只替换控制器，不覆盖 DSEE 桥接：

```powershell
& $Adb -s $Device shell 'su -c "cp /data/local/tmp/usb-dac.sh /data/local/tmp/usb-dac.pre-update.sh"'
& $Adb -s $Device push .\images\controller\usb-dac.sh /data/local/tmp/usb-dac.next
& $Adb -s $Device shell 'su -c "sh -n /data/local/tmp/usb-dac.next && chmod 755 /data/local/tmp/usb-dac.next && mv /data/local/tmp/usb-dac.next /data/local/tmp/usb-dac.sh"'
```

核对 `Get-FileHash .\images\controller\usb-dac.sh` 与 `adb ... shell 'su -c "sha256sum /data/local/tmp/usb-dac.sh"'` 的哈希相同。随后通过 App 或 `sh /data/local/tmp/usb-dac.sh on` 重新进入 DAC，USB 会短暂断连。上面这个脚本更新不会更新 APK 版本号。

## 7. 日常使用和实际范围

耳机插在 **ZX505** 上，电脑输出设备选 **ZX500 USB-DAC**。从 24 位 / 48 kHz 开始；多数音乐内容也可使用 44.1 kHz。提高电脑输出采样率不等于提升源文件质量。

| 电脑 USB 输入 | DSEE 开启后的输出 | EQ/DC |
|---|---|---|
| 44.1 kHz | 176.4 kHz | 可处理 |
| 48 kHz | 192 kHz | 可处理 |
| 88.2 kHz | 176.4 kHz | 可处理 |
| 96 kHz | 192 kHz | 可处理 |
| 176.4 / 192 kHz | 保持输入，DSEE 旁路 | 可处理 |
| 352.8 / 384 kHz | 保持输入，DSEE 旁路 | EQ/DC 也旁路 |

DSEE 关闭时，EQ/DC 可独立在不超过 192 kHz 的处理端采样率运行。EQ/DC 开关跟随原厂“音效设置”；原厂“直通音源”开启时旁路 EQ/DC，ZX DSEE 自身的 DSEE 开关仍独立控制。当前不接入黑胶处理器、动态音量等其他效果。

EQ 使用原厂增益余量，开启后整体音量可能降低。主机音量改变时会同步 Sony 服务，避免实体键突然跳回旧值；本机按键可正常逐档调整。不会为了补偿 EQ 衰减而自动抬高音量。

关闭界面、熄屏后，独立桥接和带通知的前台同步服务继续工作。**强制停止 App 会停止设置同步**，重开 App 可恢复；不要用强制停止代替正常退出。DSEE 是否与原厂完整 Ultimate 模式等效尚未确认，不宣传为已完整复刻 Ultimate/AI 全通路。

USB 身份修复在 DAC 模式暂停原厂 `usb-hal-1-1`，固定 VID/PID/序列号和纯 UAC2 接口；退出 DAC 或重启恢复阶段由系统/控制器恢复原厂服务。DAC 期间不提供原厂 USB 模式/角色管理；已验证 USB 供电和 sink 角色正常，OTG 主机模式不在本部署范围。

## 8. 验收

至少检查一次以下项目：

- Windows 识别 `ZX500 USB-DAC`，耳机端正常发声。
- DSEE 开启时 48→192 kHz；EQ 预设变化能被听到，DC 能正常切换。
- 暂停音乐时检查实体音量键只增加一档，恢复播放后无突增。
- 退出界面、熄屏后继续播放；重启或重新插线仍识别同一 Windows 端点。

需要底层状态时使用命令行，日常 App 不显示调试入口：

```powershell
& $Adb -s $Device shell 'su -c "/data/local/tmp/dsee-experimental/dsee-mode.sh appstatus"'
```

48 kHz 正在播放且两个功能开启时，期望 `INPUT_RATE=48000`、`OUTPUT_RATE=192000`、`PLAYBACK_STATE=RUNNING`、`STATE=active`。在原厂 EQ、DC 本身也开启时，应读到 `EQ_ACTIVE=1`、`DC_ACTIVE=1`、`FX_UNAVAILABLE=0`。暂停时 `idle` 或输出采样率为空是正常状态。开机路径可能把诊断日志送到 `/dev/null`，所以 XRUN/CLIPS 为空表示未取得计数，**不等于 0**。

开发者可运行不打开声卡的离线回归：

```powershell
python dsee-experimental/verify_blocks.py --serial $Device
python dsee-experimental/verify_effects.py --serial $Device
```

这些脚本需要前述 NDK、ADB 和 shell root，产物保存在忽略的 `private/`。旧 `dsee-app/verify_device.py` 主要验证 DSEE 开关，不能代替 0.3.1 的 EQ/DC 同步验收；它包含强制停止 App 的操作。

本机证据：基础 DAC 八档经用户试听；DSEE 四档经用户确认；EQ/DC 有 86 组离线处理、两档高采样率负对照、64 组实时转换契约，最终 48→192 kHz 熄屏 30 秒记录为 XRUN=0、CLIPS=0。不能据此宣称所有组合都经过长期模拟音频测量。

## 9. 常见问题

| 现象 | 处理 |
|---|---|
| 3.5 mm 耳机没声 | 检查控制器已包含 tinymix 当前枚举值解析修复；在进入 DAC 前插好耳机；运行中换接口未完整验证 |
| 修改原厂 EQ 没变化 | 确认是 0.3.1、前台同步通知存在、“应用本机 EQ/DC”开启、直通音源关闭、采样率不超过支持范围 |
| 音量键突然很响 | 暂停音乐，检查 0.3.1 同步服务是否运行；不要通过连续加音量排查 |
| Windows 显示 `2- ZX500 USB-DAC` | 安装独立 USB 身份修复脚本；旧的编号记录可以保留为离线项，无需删除所有声卡驱动 |
| USB ADB 断开 | 纯 DAC 模式的预期行为；用已授权 LAN ADB 或播放器 App 操作 |
| App 提示组件不兼容 | 核对国行 4.08 和 HAL 哈希，不移除校验 |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | APK 签名不同；先停用功能并备份需要的数据，再自行决定是否卸载旧 App；维护者保持同一发布签名 |

## 10. 停用与恢复

只停用音效：先暂停音乐，再关闭两个音效开关，或执行：

```powershell
& $Adb -s $Device shell 'su -c "/data/local/tmp/dsee-experimental/dsee-mode.sh rollback"'
```

该命令恢复上游 PCM 桥接，清除 DSEE/EQDC 标志；**它不是固件回滚，也不会取消 DAC 开机自启动**。

退出 DAC 并取消自启动：在 ZX DAC 选择 MTP 后按提示重启，或：

```powershell
& $Adb -s $Device shell 'su -c "sh /data/local/tmp/usb-dac.sh mtp"'
& $Adb -s $Device reboot
```

恢复旧控制器前，先用当前修复版 `usb-dac.sh off` 退出并恢复 HAL，再替换备份脚本；必要时重启。不要在旧脚本不认识 HAL 暂停标记时直接覆盖运行。

需要恢复原厂内核时，重新核对目标槽位，只写入自己保存的对应版本原厂 boot。boot 恢复、vbmeta 恢复及重新锁定是不同操作；未恢复完整原厂状态前不要重新锁定 bootloader。

## 11. 维护者发布前

来源、许可及对应依赖源码见根目录 [NOTICE.md](../NOTICE.md) 和 [glibc 构建说明](../third_party/glibc/README.md)。发布源码使用白名单导出，不直接上传整个刷机工作目录：

```powershell
python release/package_source.py
```

不提交 `private/`、设备备份、UPG/boot/vbmeta、Sony 库和模型、签名密钥、设备地址和原始日志。当前 APK 内含上游静态 glibc 回退程序；对应完整源码、依赖许可和重链接步骤已随仓库提供。项目许可不包含 Sony 私有组件的分发授权，也未给上游 GPL 添加额外的私有库链接例外。

现有前台同步通过 App 私有文件交换状态，包含周期性写入；不能沿用上游“运行时零周期闪存写入”的宣传。长期稳定性、耗电与写入优化仍是后续工作。
