# AMD PM 数组整批验证

2026-10-20：生产提交 `52f823dd410d93cc514c79eb3f2b8ec76436c198`。GUI / CLI 同步增加 GMI、DIMM、PCIe
三组共 92 个原始数组存储值，PM 共 220 个显示字段、12 个视图。
[实现范围](amd-pm-arrays.md)、[完整记录](validation/amd-pm-arrays-ci-52f823d.json)。

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39427503841) | 4/4；各 25 CTest / 373 场景组，含 40 组 PM |
| ASan / UBSan | 五个离线解析目标通过，保留逐字节截断覆盖 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39427512860) | 13/13；11 目标各 27 CTest、51 CLI 场景组、1154 JSON，含 94 份 PM 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39427521564) | 26/26；各目标及 EL7/EL8 基线 185 项 Qt 回归，108 分析测试 / 33 移植测试 |
| 原指令回放 | 128 组合成上下文，各 92 存储字，本机/云端报告内容和 SHA256 相同 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力、非法 CPU、非特权拒绝通过 |
| 本机用户移植测试 | 49 项：37 通过，12 项缺 Linux/C++ 工具链跳过 |
| 真机 / Secure Boot | 未执行 |

43/43 功能验证任务绑定同一生产提交。第一轮发现共享文件读取器仍限制为 64 KiB；
补齐 PM 的 128 KiB 读取链路后重新整批验证。其它捕获格式仍为原来的上限。
测试覆盖全部槽位、逐位输入、NaN/Inf/负零、错误隔离、旧快照与伪造派生字段、
最大表、CLI 外层、128 KiB 边界、元数据变化和失败后清空。数组值不受 BCLK 换算影响。

保留 1144 张截图；人工查看 Ubuntu 24.04 与 Oracle Linux 7.9 的 GMI、
DIMM、PCIe、非有限值页面各四张，共八张。其余 1136 张未逐一人工检查。
所有页面使用合成数据；DIMM 截图展示滚动后的部分行，完整 48 行由回归测试逐项核对。

本机完整分析初跑为 102 通过、3 错误、3 跳过；其中两个错误来自临时目录权限，
另一个是既有 TVB 回放达到时间边界。将临时目录放到工作区后单独重跑三个失败项全部通过。
未修改这些测试的断言或回放限制；云端 108 项完整运行通过。

`dist/gui-cli-amd-pm-arrays-52f823d/` 包含 33 个 GUI/CLI/DKMS 安装包、
精确云端源码、保留八项本地扩展的 `updated-source/` 源码、MANIFEST 与 SHA256SUMS。
276 个生产/构建源码路径已逐一核对。目标为 Ubuntu 20.04/22.04/24.04/26.04、
Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10，均为 x86_64；
EL7 特指 Oracle Linux 7.9。Windows/macOS 仅验证独立核心。
包版本仍为 2.0.1，本地更新需使用发行版重装方式。

[REA 后续研究](amd-pm-rea-arrays.md)的两次静态分析另计，不混入 43 项功能验证。
槽位对应的物理目标、实际数量和单位仍未认证；逐核/CCD、原生 PM 传输、
AMD 高层调参、其它 Intel 型号与板级控制仍见[剩余清单](recovery-status.md)。
