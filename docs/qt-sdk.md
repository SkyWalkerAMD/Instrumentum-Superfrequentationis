# EL8 静态 Qt SDK 实测记录

更新：2026-09-30。SDK 是 GUI 构建工具链；本记录不代表 OCTool GUI 已恢复或运行。

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
后续代码更新时，正式 baseline 仍从固定官方源码重新构建，不以旧 SDK 掩盖构建失败。
需独立重跑时使用仓库 Actions 页的 `Qt SDK diagnostic` workflow_dispatch。

SDK 带源码许可文件副本和 Qt 来源记录；最终 OCTool 的第三方许可、Qt 静态链接交付要求及
重链接材料，仍需结合恢复后的实际 GUI 工程完成，SDK 归档本身不是完整应用发布包。
原 GUI 源码已丢失，目前 Windows 包路径不可读，后续步骤见 [GUI 恢复流程](gui-recovery.md)。
