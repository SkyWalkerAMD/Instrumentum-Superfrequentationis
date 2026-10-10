# 无桌面版验证与交付

2026-10-10；生产源码 `b2ef0a3cebd44c6bceb6b4e8282deaed4bd03c4d`；研究分支 `refactor/platform-recovery`。
[操作手册](headless-cli.md)；[完整证据与文件哈希](validation/headless-cli-ci-b2ef0a3.json)。

`octool-cli` 是独立 C++ 命令行程序，共用现有核心与 Linux HAL，构建和运行均不需要 Qt。
主板/传感器/已绑定 SPD 读取也已拆成 GUI 和 CLI 共用的无 Qt 实现。
诊断、帮助、离线 SPD 解码不打开硬件后端；设置命令先验证所有选项和 `--apply`，再访问设备。

## 验证

- [专用 CLI 工作流](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38016466064)：12/12（矩阵准备、10 目标、总门禁）。
- 每个目标 9 CTest：7 个既有核心测试、CLI 命令测试、CLI 进程测试，全部通过。
- 核心仍有 56 场景组；CLI 测试含 31 种参数拒绝、真实核心配模拟设备的读写与错误路径、53 份 JSON 报告检查。
- 稀疏 CPU 亲和性、64 位十六进制精度、SPD CRC、清单单位/错误/文件上限/重复 EEPROM、无显示运行通过。
- 各目标新容器安装、普通用户诊断、卸载和重装通过。运行容器没有 Qt、编译器、DKMS、显示服务或宿主寄存器设备。
- [既有 Linux 工作流](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38016466047)：23/23，每目标 32 Qt 回归；11 套目标内核在 VM 内加载/卸载驱动并测试受限请求。
- 本地原工作目录 48 项脚本测试：36 通过，12 因 Windows 缺 C++/Linux 环境跳过；可执行 CLI 测试已在云端十目标完成。
- 最小 Ubuntu/部分 RPM 镜像默认省略软件文档。测试已允许发行版的安装策略；安装包仍包含说明文件，运行不依赖该文件。

容器诊断里的 kernel 是共享的宿主内核；发行版内核验证单独使用 VM。没有把云端 CPUID、模拟寄存器值或 VM 成功作为真实主板调参验收。

## 体积

| 目标 | CLI 包字节 | CLI 可执行文件字节 | CTest |
|---|---:|---:|---:|
| el8 | 138,156 | 324,256 | 9/9 |
| el9 | 143,710 | 324,296 | 9/9 |
| el10 | 153,925 | 340,664 | 9/9 |
| ubuntu20.04 | 163,688 | 370,648 | 9/9 |
| ubuntu22.04 | 160,850 | 354,264 | 9/9 |
| ubuntu24.04 | 164,648 | 354,264 | 9/9 |
| ubuntu26.04 | 173,404 | 380,240 | 9/9 |
| debian11 | 157,082 | 342,944 | 9/9 |
| debian12 | 153,886 | 326,640 | 9/9 |
| debian13 | 164,660 | 356,680 | 9/9 |

上述体积不含系统 C/C++ 运行库、可选 DKMS 包或编译驱动所需的内核头文件。
独立 CLI 仅开放已经还原的功能，不能按体积推断原版全部功能已恢复。

## 本地文件

`dist/headless-cli-b2ef0a3/` 下按发行版分目录，每个目录有 CLI 和独立 DKMS 包。
共 10 个 CLI + 10 个 DKMS 包，另有对应源码归档、源码校验和、统一 SHA256SUMS。
选择目标发行版后按使用说明安装，不能将较新系统编译的 CLI 包当成旧系统通用包。

`build/headless-cli-b2ef0a3/` 保存原始工作流日志、CLI 测试、包、运行诊断、目标内核 VM 证据。
本机 `cli/` 及本次修改的共享文件共 18 项与云端源码归档逐字节核对（仅规范化 CRLF）。
用户工作目录中其它已有研究改动保留，未混入本次云端产物。

## 功能边界

支持 Linux x86_64。`.ko` 驱动依然独立，HAL/ABI/驱动代码与此前验证版本相同。
未增加 PStates/曲线写入、Intel server VF/fabric 或主板专用电压/时钟控制。
RAPL/HWP 设置报告提交结果；Intel OC 设置验证寄存器回读；两者都不表示整机稳定性已验证。
四台目标机器的真实寄存器操作和固件行为、Secure Boot 注册、跨版本升级仍未验收。
