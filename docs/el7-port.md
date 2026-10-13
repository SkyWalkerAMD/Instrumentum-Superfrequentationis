# EL7 兼容适配

当前正在验证的第 11 个发行版目标为 `el7`，CI 使用官方 `oraclelinux:7`
镜像和 Oracle 签名的 EL7 软件仓库。它代表实际测试的 Oracle Linux 7.9
环境；不能把 ABI 兼容设计直接表述为所有 CentOS/RHEL 7 安装均已验证。

EL7 独立构建静态 Qt 5.15.18 SDK、GUI 和 helper，发布检查限制
`GLIBC <= 2.17`、`GLIBCXX <= 3.4.19`、`CXXABI <= 1.3.7`。
CLI 同样在 EL7 构建并检查 ELF 版本要求。用户态构建使用 devtoolset-11，
EL7 程序静态链接 C++ 编译器运行库，继续使用系统 glibc 和显示依赖。
不打包或替换系统 glibc，也不要求运行程序时启用编译器工具集。

RPM 4.11 不支持的弱依赖标签在 EL7 spec 中省略；GUI 和 CLI 均可独立安装，
驱动为可选 DKMS 包。EL7 内核以 `kernel` / `kernel-devel` 配对，使用本机原生 GCC
构建；Kbuild 根据实际头文件选择编译器头，继续探测 `class_create` 签名。

Python 3.8、SDK 和编译器是构建/验证工具，不是 GUI/CLI 产品运行依赖。
CI 的 Python 来自 SCL，仅 Python 包装器设置它自己的库路径。
官方说明：[Oracle Linux Python](https://yum.oracle.com/oracle-linux-python.html)、
[Oracle SCL](https://docs.oracle.com/en/operating-systems/oracle-linux/scl-user/scl-ReleaseNotes.html)。

验证要求包括：EL7 独立 Qt/GUI 回归、无桌面 CLI 回归、全新镜像安装与普通用户启动、
卸载重装、DKMS 编译与内核镜像匹配、QEMU 中实际加载/卸载驱动。
云端完成前，这一目标应标注为**适配验证中**。真机调参和 Secure Boot 验收另行记录。
