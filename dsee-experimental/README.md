# ZX505 4.08 USB PCM → Sony DSEE → ALSA 实验实现

0.3.1 修复原厂十段 EQ / DC 相位线性器。已定位 0.3.0 依赖页面缓存导致实时设置不跟随，以及 USB 硬件音量与 Sony 服务保存值不一致的问题；现改为 Binder 实时同步。用户已确认 EQ/DC 试听和实体音量键正常，48→192 kHz 实机熄屏验证通过。范围和验证记录见 [EQ/DC 适配](EQ-DC.md)。

现已提供并在本机安装 **ZX DSEE App**，可直接在播放器上控制开关与查看实际采样率；使用方法见 `dsee-app/README.md`。下文命令行方式仍可使用。

已在本机实现实时通路：

```text
USB UAC2 / card2 capture (S24_3LE)
  → 原项目的时钟漂移校正
  → 24-bit PCM 转浮点、拆分左右声道
  → 本机原厂 DPFDSX / DSEE AI 处理器
  → 浮点转回 S24_LE（含限幅和异常数值检查）
  → card1 playback
```

这是调用设备现有原厂算法的用户态适配。未刷写分区、未重写 DSEE 算法、未打包 Sony 库或模型。当前称为“Sony DSEE 实验模式”，尚未确认它与原厂播放器显示的 Ultimate 在所有参数、输入内容信息和策略上完全等效。

## 验证结果

2026-10-24，国行 ZX505 4.08.00：

| 输入 | 输出 | 处理器状态 | 已完成验证 |
|---|---|---|---|
| 44.1 kHz | 176.4 kHz | 两个内部处理标志开启 | 离线；实时连续统计无 XRUN/削波；用户确认声音正常；漂移插帧实际触发 |
| 48 kHz | 192 kHz | 两个内部处理标志开启 | 离线；实时连续数分钟无 XRUN/削波 |
| 88.2 kHz | 176.4 kHz | 0.1.2 修复两子块处理，原厂标志 0,1 | 28 组离线回归；实时 XRUN/削波为 0；用户确认 DSEE 试听正常 |
| 96 kHz | 192 kHz | 0.1.2 修复两子块处理，原厂标志 0,1 | 同上，实机日志确认实际进入处理而非直通 |
| 176.4 / 192 / 352.8 / 384 kHz | 保持输入采样率 | 明确旁路 | 192 kHz 离线处理标志均关闭；384 kHz 实时旁路观察到 XRUN=0。其余新桥接档位尚未逐档复测 |

48→192 kHz 离线测试：输入 104192 帧，输出 416768 帧，原厂处理块 64→256 帧；处理约 2.17 秒音频耗时约 362 ms，单块最慢约 0.463 ms，单块音频时长约 1.333 ms。输出峰值约 0.172，无非有限数值或削波。此耗时仅是离线原厂处理调用，不含实时格式转换、调度及声卡等待。

实时统计中的 `process_ms` 是处理调用的累计墙钟耗时，包含可能的调度等待，不是 CPU 占用率。较早日志中的 `cpu_ms` 同样是墙钟耗时，后续已修正名称。算法接口返回 delay=6420，但本轮没有测量整机 USB 到耳机的总延迟，不将它直接当作端到端延迟。

首次实时实现出现反复 XRUN：输出缓冲区扩展为 8192 帧后，错误沿用了输入端的 2048 帧 stop_threshold。现已按输出端实际缓冲区设置阈值。修复前日志不能作为稳定性通过的证据。

受控 SIGABRT 回退测试已通过：实验进程保留 PID，自动 exec 原桥接，清除 DSEE 开关，输出从 176.4 kHz 恢复 44.1 kHz。所有 ALSA/mixer/event 描述符均设置 close-on-exec，已检查运行时 fdinfo，避免回退进程因声卡仍被旧描述符占用而启动失败。

原项目中的普通八档直通已由用户在此前验证。不能把该结果计作本轮 DSEE 八档验证。当前也未做长时间发热/耗电测试、重启后的实机验证或模拟耳机端测量。

**0.1.2 两倍通路修复：** 原厂初始化报告 128→256 帧，但原始执行器的调用批次数仍为 1。其内部存在明确的 ratio=2、batch=2 分支，会执行两次 64 帧子块。旧适配未设置该批次数，独立输出缓冲后半段 128 帧未被写入，导致播放栈残留。离线以 NaN 填充时，每档 256 个块都重现 65536 个未写样本；共用输入输出缓冲也不能解决。

适配层现针对本机版本的两倍模式将工作描述符 `state+0x1e738` 设置为 2。修复后零/哨兵/NaN 初始输出逐字节一致、全部为有限样本、末端哨兵未越界；44.1/48 kHz 与修复前逐字节一致。恢复旧批次数的负对照仍能精确重现故障。实时路径每块预填 NaN，若有未写样本则自动回退原桥接，不把残留数据送到耳机。没有修改 Sony ELF、模型或固件分区。

用户已确认 88.2/96 kHz 两档 DSEE 均正常。最终设备记录在 `private/dsee-research/highrate-dsee-fixed-live.json`。此结论覆盖帧契约、数字缓冲、实时计数及用户听感，不代表完成模拟输出或完整 Ultimate 等效性测量。

可重跑离线回归（不打开声卡）：

```powershell
python .\dsee-experimental\verify_blocks.py --serial 播放器IP:5555
```

## 本机使用与回退

在本项目目录打开 PowerShell，先用 `adb devices` 找到局域网地址，再设置：

```powershell
$deviceAddr = '播放器IP:5555'
python .\dsee-experimental\deploy.py --serial $deviceAddr --mode status
python .\dsee-experimental\deploy.py --serial $deviceAddr --mode off
python .\dsee-experimental\deploy.py --serial $deviceAddr --mode on
```

`off` 原子恢复原桥接文件并重启该进程；`on` 启用实验桥接。切换会短暂停顿，不重建 USB gadget，也不调整耳机音量。Windows 的 44.1/48 kHz 是 USB 输入格式；DSEE 启用时，播放器内部输出为其四倍，不需要把 Windows 本身设为 176.4/192 kHz。

`trial` 启用有 15 分钟自动回退的测试窗口；`commit` 取消该窗口的定时回退。每次 trial 使用独立标识，旧测试计时器不会关闭新测试。开关文件 `/data/adb/usbdac.dsee` 是独立的实验设置，由新 ZX DSEE App 或命令行控制，**未与 Sony 音效设置关联**。USB 未处于 DAC 模式时，开关只暂存选择，不会启动声卡桥接。

运行日志在 `/dev/.zx-dsee.log`，限制约 128 KiB 并循环截断，重启后消失，不持续写入闪存。`status` 同时显示输入/输出 hw_params 及最近统计。

出现 SIGSEGV/SIGBUS/SIGABRT/SIGFPE 或无效音频输出时，程序尝试清除实验标志并 exec 备份原桥接；若连续三次出现 DSEE 播放欠载风暴，也转回原桥接。初始化失败或不支持的采样率明确记录 BYPASS。此保护不能替代所有故障场景的完整验证。

## 文件及构建

- `sony_dsx.h`：32 位原厂 DPFDSX ABI、保护页内存和参数读取。
- `bridge_dsee.h`：实时格式转换、采样率策略、故障回退及日志限制。
- `generate_bridge.py`：对原桥接源码应用少量受校验的挂钩；不修改原仓库源码。
- `build.py`：使用 Android NDK 构建独立程序及桥接。
- `offline.c` / `probe.c`：不打开声卡的离线处理和库加载工具。
- `deploy.py` / `dsee-mode.sh`：哈希校验、部署、独立试运行与回退。

需要放在本项目 `tools/` 的构建依赖：

1. Google Android NDK r27c Windows，归档 SHA1：`ac5f7762764b1f15341094e148ad4f847d050c38`。安装目录 `tools/android-ndk-r27c/`。
2. tinyalsa 提交 `e43025bbf702eb7dd8edd48c1eb50530c60f1de8`，放在 `tools/dsee-tinyalsa/`，应用原项目 `bridge/tinyalsa-xruns.patch`。构建脚本另生成 close-on-exec 版本的 pcm_hw/mixer_hw，不修改 tinyalsa 原文件。
3. 原项目源码及 Google platform-tools。

```powershell
python .\dsee-experimental\build.py probe arm
python .\dsee-experimental\build.py offline arm
python .\dsee-experimental\build.py bridge arm
python .\dsee-experimental\deploy.py --serial $deviceAddr --mode stage
python .\dsee-experimental\deploy.py --serial $deviceAddr --mode trial
```

原项目桥接为 AArch64 静态 glibc 程序。原厂 HAL 的 64 位版本包含 text relocations，实机 dlopen 被拒绝；本实现使用 Android/Bionic ARM32、API 22 目标，以匹配设备实际使用的 32 位原厂音频服务。它仍运行于设备的 Android 9 系统。不能把 glibc 工具链编译结果直接链接到该 Android 库。

本次基于原项目提交 `af3a7fba2e9043739f406d88690052c2a86416a6`；上游 `bridge/uac2bridge.c` SHA256 为 `77bb991ed93e696390a8efe945a2210dd4495f80519ea085a89c50923c067367`。保留上游代码的适用许可与归属；如对外发布，应按上游实际许可整理分发材料。

部署只接受显示版本 `4.08.00` 且 32 位原厂 HAL SHA256 为 `dfd3481e5be322f306acef4fac08ce85b204821570597e72c541a1978ce305b8` 的设备。实验代码含特定版本的结构偏移，不能直接用于别的固件。原桥接备份会先按已知上游二进制校验。

## 私有 ABI 记录

本机实测的入口与布局，仅供维护此适配器：

- `EffectInitDPFDSX(handle, state, state_bytes, scratch, scratch_bytes)`。
- `EffectSetParamDPFDSX(handle, params, 0xeaa8, update=1)`，先调用 `check_dsx_param` 验证。
- 参数前 72 字节包含表示方式、声道、输入/输出采样率及源编码信息；随后为设备现有的 60000 字节参数文件。
- 以原厂 DsxParam 构造函数选用的表示方式 3，传入非交错浮点声道。USB 只有解码 PCM，所以编码类型、VBR、码率按未知值传入，没有虚构 MP3/AAC 元数据。
- `dsx_execute_func(state + 0x1d550, in_planes, out_planes)` 按接口返回的固定块工作。
- 本版本 ARM32 工作区中的 input/output block 字段为 `0x1e73c/0x1e740`，子块批次数为 `0x1e738`，处理标志为 `0x1d5a0/0x1d5a4`。两倍 128→256 模式须配置两个 64 帧子块。

工作区和音频平面分配保护页，初始化后校验块大小、声道和输入输出整数倍关系。上述符号存在不等于未来版本兼容，部署门槛不可移除。

原厂 ELF、模型、密钥、个人备份均留在被忽略的本地目录；源码不包含它们。研究日志在 `private/dsee-research/`，编译结果在 `images/dsee-experimental/`。

参考：[原项目](https://github.com/remove/ZX500-USB-DAC)、[Sony 的 Ultimate/HX 应用范围说明](https://helpguide.sony.net/dmp/nwzx500/v1/en/contents/TP0002432956.html)、[NDK 官方归档版本信息](https://github.com/android/ndk/wiki/Home/24fe2d7ee3591346e0e8ae615977a15c0a4fba40)。
