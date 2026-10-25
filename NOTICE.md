# 来源与许可

本仓库适配代码和控制 App 按 GPL-2.0 发布；上游代码保留原有版权与许可。根目录 LICENSE 为 GPLv2 全文。

- ZX500-USB-DAC：<https://github.com/remove/ZX500-USB-DAC>，固定提交 `af3a7fba2e9043739f406d88690052c2a86416a6`，GPLv2。
  子目录保留构建所需的上游源码；扩展由生成器和独立补丁表示。
- tinyalsa：<https://github.com/tinyalsa/tinyalsa>，固定提交 `e43025bbf702eb7dd8edd48c1eb50530c60f1de8`，BSD。
  已应用上游 `bridge/tinyalsa-xruns.patch`；close-on-exec 修改由构建时生成。
  逐文件版权、免责声明、NOTICE 和 MODULE_LICENSE_BSD 均保留。
- APK 内原始回退桥接 `original/uac2bridge` 与上述上游二进制 SHA-256 一致：
  `d80136c9136ada8fdf829801880e26d7817c3ba554ea1124f0ad2494c1f6d336`。
  使用 Ubuntu/Linaro GCC 5.4.0-6ubuntu1~16.04.12、glibc 2.23-0ubuntu11.3 构建。
  glibc 完整对应源码和许可在 `third_party/glibc/`，桥接及 tinyalsa 源码、补丁和构建配方同时提供。
  GCC 运行库适用 GCC Runtime Library Exception，相关文本见 `third_party/gcc/`。
- 音效扩展使用 Android NDK r27c / Bionic ARM32 构建。
- Sony 私有组件仅从使用者设备加载；仓库及 APK 未包含其库、模型、固件、解密配置或算法实现源码。
  本项目未给上游 GPL 添加额外私有库链接例外，也不授予 Sony 组件的许可。
  Sony、Walkman、DSEE 等名称属于各自权利人，本项目与 Sony 无官方隶属关系。

各文件的版权及许可声明继续适用。对应构建和重链接步骤见 `third_party/glibc/README.md`。
