# EL7 兼容适配

2026-10-13：第 11 个目标 `el7` 已通过 GUI、CLI、DKMS、全新环境安装和匹配内核 VM 验证。
首次完整验证提交 `bc30c4f`；本批功能更新在 `dbe9206` 于 2026-10-18 再次通过，[最新交付记录](intel-iccmax-validation.md)。测试环境是官方
`oraclelinux:7` 镜像与 Oracle 签名软件仓库，实际为 **Oracle Linux 7.9 x86_64**。
RHEL/CentOS 7 的 ABI 兼容设计不等于这些系统已经逐台验收。

EL7 使用独立静态 Qt 5.15.18 基线，GUI/helper/CLI 的发布要求限制为
`GLIBC <= 2.17`、`GLIBCXX <= 3.4.19`、`CXXABI <= 1.3.7`。
用户态构建采用 devtoolset-11，程序静态链接 C++ 编译器运行库并使用系统 glibc。
运行时无需编译器工具集、SDK 或 Python，也不替换系统 glibc。GUI 需要 X11 环境；CLI 无桌面依赖。

RPM 4.11 不支持的弱依赖标签已按目标处理。GUI 与 CLI 可独立安装；驱动为可选 DKMS 包，
由目标机器为其内核构建 `.ko`。EL7 原生 GCC 4.8 编译内核模块，按 `kernel` / `kernel-devel`
精确配对。Kbuild 根据实际头文件探测 `class_create` 和 TSC 访问方式，兼容 EL7 回移 API，
并处理 Linux 5.4 的头文件依赖及 Linux 7.0 的 TSC 声明位置变化。

安装示例，在本目录对应 EL7 子目录中执行：

```sh
sudo yum install ./octool-cli-2.0.1-1.el7.x86_64.rpm
octool-cli diagnose
```

有桌面时另安装 `octool-2.0.1-1.el7.x86_64.rpm`。需要驱动时先安装当前内核匹配的
`kernel-devel`、GCC 和 DKMS，再安装对应 `octool-hwio-dkms` RPM。不要把 CI 容器引导脚本当作主机安装器。

构建环境中的 Python 3.8 来自 SCL，仅 Python 包装器设置自己的库路径。
参考：[Oracle Python](https://yum.oracle.com/oracle-linux-python.html)、
[Oracle SCL](https://docs.oracle.com/en/operating-systems/oracle-linux/scl-user/scl-ReleaseNotes.html)。
QEMU 验证不执行超频寄存器写入；真机功能效果和 Secure Boot 仍需单独验收。
