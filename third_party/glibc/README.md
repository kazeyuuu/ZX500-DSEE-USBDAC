# glibc 对应源码与回退程序重链接

APK 的 `assets/payload/original/uac2bridge` 为固定上游的 AArch64 静态回退桥接。原构建日志及二进制编译器标识对应 Ubuntu 16.04 ARM64、GCC 5.4.0-6ubuntu1~16.04.12、glibc 2.23-0ubuntu11.3。

本目录提供 [Ubuntu 官方源](https://ports.ubuntu.com/pool/main/g/glibc/) 的完整对应源码。两个 tar 包的 SHA-256 已与 `.dsc` 的 Checksums-Sha256 比对：

| 文件 | SHA-256 |
|---|---|
| glibc_2.23.orig.tar.xz | bf6c528eeebefcacc295270068b79330c1fb2b22458ff66285b4175d23442c96 |
| glibc_2.23-0ubuntu11.3.debian.tar.xz | 3e7cf8a22ae34f2ed85360e761757fd7153f47a676c16b1593831d8cc0cfa155 |

`COPYING.LIB` 为 LGPLv2.1 全文；其他适用声明见 `COPYING`、`LICENSES` 和各源码文件。本项目未修改 glibc。

## 重建回退桥接

应用完整源码为 `ZX500-USB-DAC/bridge/uac2bridge.c`，配合 `tools/dsee-tinyalsa/` 的源码及已应用 XRUN 补丁。在 Ubuntu 16.04 ARM64 环境安装上述编译器及 libc6-dev 2.23-0ubuntu11.3，从仓库根目录运行：

```bash
mkdir -p images/fallback
gcc -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations -static -s \
  -Itools/dsee-tinyalsa/include \
  ZX500-USB-DAC/bridge/uac2bridge.c \
  tools/dsee-tinyalsa/src/{pcm,pcm_hw,pcm_plugin,mixer,mixer_hw,mixer_plugin,limits,snd_card_plugin}.c \
  -o images/fallback/uac2bridge -ldl -lpthread
```

扩展桥接由 `dsee-experimental/build.py` 生成和构建，不混入此回退源码。

## 使用修改后的 glibc

可以解包并修改 glibc 源码，在匹配的 ARM64 Ubuntu 隔离构建环境安装构建依赖后重建库：

```bash
dpkg-source -x glibc_2.23-0ubuntu11.3.dsc
cd glibc-2.23
# 按需求修改源码，并按 Debian/Ubuntu 流程安装构建依赖。
dpkg-buildpackage -b -uc -us
```

将新生成的匹配 ARM64 libc6/libc6-dev 软件包安装到隔离构建环境，再运行上面的完整桥接编译命令即可重链接。也可使用自己的 GCC/sysroot 并相应设置编译及链接路径。
仓库提供完整应用源码，可以修改应用并与修改后的库重新链接；修改版不要求保持原版哈希。

生产构建脚本严格核对原始回退程序哈希。实验重链接版本需要同步更新 `release/prepare_dependencies.py` 的 EXPECTED 及 App 源码的原始桥接哈希，再重新构建 APK；保留固件门槛和故障回退，并自行验证采样率及恢复行为。重新打包须使用自己的签名。

这里只提供源码和重链接途径，不声明修改后的库或回退构建已通过设备测试。上游容器配方在 `ZX500-USB-DAC/docker/Dockerfile`。
