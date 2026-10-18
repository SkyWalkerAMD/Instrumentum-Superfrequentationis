# HWP 包级设置批次验证

2026-10-18，生产提交 `783dd962bb4c98503b32dd81b43bbdb4c9944e76`，分支 `refactor/platform-recovery`。
GUI / CLI 同步接入五项 HWP 包级设置：最小性能、最大性能、目标性能、能效偏好、活动窗口。
[功能与协议](intel-hwp-package.md)、[完整证据](validation/intel-hwp-package-ci-783dd96.json)。
此前的 HWP/PECI 信息、DDRIO、PL4、电压和内存等功能均包含在本批安装包中。

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39086595091) | 4/4；各 24 CTest / 320 场景组，平台控制增加至 28 组 |
| ASan / UBSan | 五个快照解析目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39086595110) | 13/13；11 个目标各 26 CTest / 39 CLI 场景组 / 939 份 JSON |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39086595141) | 26/26；各目标及 EL7/EL8 基线 154 项 Qt 回归；101 分析测试、33 移植测试 |
| 匹配内核 QEMU | 12 次启动，含 EL7 3.10；装卸、能力、非法 CPU、非特权拒绝通过 |
| 真机调参 / Secure Boot | 未执行；合成回归与虚拟机驱动检查不能替代真机验收 |

43/43 云端任务对应同一个生产提交。源码与 GUI/CLI/驱动使用相同版本；CLI 在无显示、
无构建工具、无硬件设备的全新环境中完成安装、运行、卸载和重装，未依赖 Qt/X/Wayland。
保留 858 张自动截图；人工检查的具体图片和 SHA256 见证据，不声称全部截图经过人工检查。

`dist/gui-cli-intel-hwp-package-783dd96/` 包含 33 个包：Ubuntu 20.04/22.04/24.04/26.04、
Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10，每目标 GUI、CLI、DKMS 各一个。
EL7 实测范围是 Oracle Linux 7.9 x86_64；其他 EL7 配置仍需验收。

根目录 `octool-2.0.1-src.tar.gz` 对应精确云端提交；`updated-source/` 中的源码保留本地扩展和
最新验证记录。269 个生产/构建源码路径已逐一核对，保留差异在 MANIFEST 中列出。
全部交付文件由 SHA256SUMS 校验。包版本仍为 2.0.1，同版本升级请使用发行版的本地重装方式。

本批设置仅更改 `772h` 的选定字段，并检查完整旧值、PECI/极性/本地请求上下文及完整回读。
不会改变 HWP 启用、包级选择、flexible-valid、PECI、通知或历史位。
性能范围参考所选逻辑 CPU；它可能影响同一封装的其它 CPU，并不证明实际性能或所有 CPU 的最终策略。
ICCmax/VR、增强 TVB、部分 AMD、内存与主板写入及真机验收仍见[剩余清单](recovery-status.md)。
