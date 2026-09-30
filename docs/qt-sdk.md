# EL8 静态 Qt SDK 实测记录

更新：2026-09-30。本页保留首个成功 SDK 的构建证据；新 GUI 的实测结果见
[第一阶段](gui-phase1.md)和[验证状态](verification-status.md)。SDK 成功不能单独代替应用验收。

## 已成功的构建

[GitHub Actions run 36661824443](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36661824443)
在提交 `bfa992d2114c4203b1fb4213cc46b7bc7eafbb18` 上成功，job `109718066632`，
2026-09-30 02:52–03:09 UTC。标准 ubuntu-24.04 runner 内使用 Rocky 8 容器，
实际 GCC 8.5.0、glibc 2.28；镜像原始 os-release 标识为 8.9，依赖从当时 EL8 软件源安装。
不能把镜像标签或包更新推断为已验证所有 Rocky/Alma/RHEL 小版本。

构建调用 `port/ci/in-container.sh sdk el8`，与正式 baseline 共用 SDK 函数。
官方 Qt 5.15.18 源码 SHA-256 已校验：
`cea1fbabf02455f3f0e8eaa839f5d6f45cdb56b62c8a83af5c1d00ac05f912ea`。
配置摘要、make、安装、静态插件/AT-SPI 检查和归档全部完成；JOBS=4。
原始镜像 digest、配置/日志/包清单哈希和 SDK ELF 检查见
[机器可读证据](validation/qt-sdk-run-36661824443.json)，完整配置见
[configure summary](validation/qt-sdk-run-36661824443-config.txt)。

## 配置与实际检查

| 项目 | 本次结果 |
|---|---|
| Qt 共享库 | 不构建，Qt 静态链接 |
| ICU | 关闭，避免 ICU 大版本 soname 绑定 |
| JPEG/PNG/TIFF/WebP、zlib/PCRE/HarfBuzz | 使用 Qt 内置副本 |
| FreeType/Fontconfig/D-Bus | 系统依赖，仍须随最终 GUI 核对运行时闭包 |
| xcb | `plugins/platforms/libqxcb.a` 存在 |
| Wayland | Client=yes，静态 generic/egl/xcomposite-glx 插件存在 |
| Accessibility/AT-SPI | 公开 accessibility 和版本化 private header 的 bridge 宏均为 1 |
| OpenSSL | no；网络/TLS 范围待实际 GUI 功能确认 |
| qmake 的 ELF 依赖 | libc、libm、libstdc++、libgcc_s；无 RPATH |
| qmake 最大符号需求 | GLIBC_2.28、GLIBCXX_3.4.15、CXXABI_1.3.8，EL8 门禁通过 |

静态平台插件存在不等于应用已导入插件，更不等于 EL10/Xwayland 实际窗口测试通过。
SDK qmake 的 ABI 结果也不能外推为最终 GUI 的符号版本、依赖或 CPU 指令基线。
下一步必须对实际 GUI 链接结果执行 `check_elf.py` 和十目标 runtime 门禁。

## 产物及复用

Actions artifact 为 `el8-qt-sdk-diagnostic`（ID `11074823344`），保留 7 天。
本地已保存为 `dist/qt-sdk/octool-qt-5.15.18-el8-sdk.tar.gz` 和同名 `.sha256` 文件。
SDK tar 大小 56,674,389 字节，SHA-256：
`3aaa15077aa3c15989c6a0f7d40215d8210a1c6aa765d9e07590c16ca3823b55`。
下载副本与云端记录相同；压缩包没有提交 Git。

在一次性 EL8 构建容器中，安装现有 bootstrap 声明的开发依赖，校验哈希后解到 `/opt`，
使 qmake 位于 `/opt/octool-qt/bin/qmake`。该 SDK 配置为不可重定位，不应任意改安装前缀。
正式 baseline 对相同配方使用带 SHA-256 校验的 SDK 缓存，每次重新编 GUI/测试；
缓存键包含 Qt 构建脚本和 bootstrap 依赖脚本哈希，配方改变会从固定官方源码重建。
历史成功 artifact 只在配方哈希和 SDK SHA-256 均匹配时可以 seed；不使用过期配方缓存。
需独立重跑时使用仓库 Actions 页的 `Qt SDK diagnostic` workflow_dispatch。

SDK 带源码许可文件副本和 Qt 来源记录；最终 OCTool 的第三方许可、Qt 静态链接交付要求及
重链接材料，仍需结合恢复后的实际 GUI 工程完成，SDK 归档本身不是完整应用发布包。
原 GUI 源码已丢失，现已检查可读取的 Windows 包并重构基础版；详细输入见
[GUI 恢复流程](gui-recovery.md)。基础版只链接 QtBase，不能把 SDK 中 Charts 等模块的许可
或存在状态当作当前 GUI 的已链接依赖。

## 完整 GUI 矩阵使用的后续 SDK

bootstrap 修复发行版差异后，81ea801 的 baseline 从源码重建 Qt 并保存新配方缓存。
c48a38f / run 36670288030 恢复该 SDK，通过哈希校验，再重编当前 GUI/测试；全矩阵通过。
当前 SDK SHA-256 为 `6afaa07b5716c00c92ece61ef42eae007b18e57e2d650f6b450ddfa52afc8d7c`，
本地另存 `dist/qt-sdk/octool-qt-5.15.18-el8-sdk-c48a38f.tar.gz` 及 `.sha256`，未覆盖上面的历史 SDK。
最终发行 GUI 的实际最高需求为 GLIBC2.28、GLIBCXX3.4.15、CXXABI1.3.9，不能继续沿用 qmake 的
CXXABI1.3.8 结果替代它。完整结果见 [矩阵证据](validation/actions-run-36670288030.json)。
