# ZX DSEE

面向 **国行 Sony NW-ZX505 / Android 9 / 4.08.00（build 337）** 的非官方 USB DAC 音效扩展，基于 [ZX500-USB-DAC](https://github.com/remove/ZX500-USB-DAC)。

**[下载 ZX-DSEE-0.3.1.apk](ZX-DSEE-0.3.1.apk)** · **[部署指南](release/DEPLOY.zh-CN.md)** · **[来源与许可](NOTICE.md)**

必须先具备兼容的 USB DAC 内核、KernelSU 和 root 授权；仅安装 APK 无法让原厂系统获得 USB DAC 功能。控制器补丁需按部署指南单独安装。

## 功能

```text
电脑 USB PCM → 时钟漂移校正 → DSEE（可选）
            → 原厂 EQ / DC（可选）→ ZX505 耳机输出
```

- DSEE：44.1/48 kHz 四倍、88.2/96 kHz 两倍，输出 176.4/192 kHz。
- EQ/DC：同步播放器的原厂十段均衡器和 DC 设置；352.8/384 kHz 自动旁路。
- 同步 USB 主机音量与 Sony 音量服务，保留实体音量键操作。
- 独立开关，自动校验部署；退出界面、熄屏后继续处理。
- 控制器补丁修复 3.5 mm 耳机路由及 Windows 重复 DAC 端点名称。

项目调用设备已有的 Sony 组件，不包含其算法实现、库、模型、固件、解密配置或签名密钥，不宣称完整复刻 DSEE Ultimate，也不代表 Sony 官方项目。

## 安装

已有兼容内核和 KernelSU 的设备，可下载根目录 APK，按[部署指南](release/DEPLOY.zh-CN.md)安装控制器和 App，并授予 ZX DSEE root 权限。首次内核改造、升级、停用与恢复步骤均见指南。

强制停止 App 会停止设置同步；再次打开后恢复。卸载前先关闭两个音效开关。只验证过一台国行 4.08 设备，其他型号、地区、固件和长期稳定性尚未验证。

## 从源码构建

Windows 环境需要 Python 3、Git、JDK 17、Android NDK r27c、SDK build-tools 35.0.0 和 Android 34 平台。环境变量设置见部署指南。

```powershell
python release/prepare_dependencies.py
python dsee-experimental/build.py bridge arm
python dsee-app/build.py
python release/prepare_usb_controller.py
```

APK 输出到 `images/dsee-app/`，控制器输出到 `images/controller/`。构建会在忽略的 `private/android-signing/` 生成自己的签名密钥；自己的签名无法覆盖不同签名的同包名应用。

`ZX500-USB-DAC/` 保留固定上游提交的构建源码子集；tinyalsa 必需源码及补丁在仓库内。APK 内回退程序的 glibc 对应源码、许可和重链接说明见 [third_party/glibc](third_party/glibc/README.md)。

## 验证范围

0.3.1 已完成四档 DSEE 试听、86 组 EQ/DC 离线用例、两档高采样率负对照及 64 组转换契约验证。48→192 kHz 退出界面/熄屏检查中 XRUN=0、CLIPS=0；控制器模式往返及重启后复用 Windows 端点。测试工具保留在 `dsee-experimental/`，需要匹配固件和实际设备。

长期耗电、周期性设置文件写入优化、更多耳机接口及完整模拟输出测量仍待补充。固件和 HAL 哈希校验是适配边界，不应通过移除校验来宣称兼容。

## 许可

项目代码按 [GPL-2.0](LICENSE) 发布，保留上游版权；tinyalsa 使用 BSD 许可，glibc 使用随附的 LGPL 及逐文件许可。完整来源见 [NOTICE.md](NOTICE.md)。这些许可证不授予 Sony 私有组件的重新分发权利。

`SHA256SUMS` 和 `SOURCE-MANIFEST.json` 用于核对公开文件。请勿在 Issue 或贡献中提交设备序列号、IP、日志中的个人信息、密钥或 Sony 私有文件。
