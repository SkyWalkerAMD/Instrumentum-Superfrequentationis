# CCD 选定核心频率：整批验证

2026-10-21，生产验证提交 `8c85a2819ed18f10a824f969faee8d60512e9399`。
GUI / CLI 同步支持同一 CCD 内的显式核心列表，逐核比较完整能力字和身份，首个错误后停止。
每核记录进入处理、发送尝试、固件接受状态；已完成的改变可能保留，实际频率没有回读验证。
见[用法](amd-manual-oc.md)、[REA 研究](amd-oc-cores-research.md)及[机器记录](validation/amd-oc-cores-ci-8c85a28.json)。

| 检查 | 结果 |
|---|---|
| [共享核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39560553080) | 4/4；Ubuntu 22/24、Windows、macOS 各 30 CTest、422 场景组；新增多核频率 7 组 |
| ASan / UBSan | 五个既有离线解析目标通过；新硬件设置路径未做 sanitizer 或真机验收 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39560550072) | 13/13；11 个目标各 32 CTest、63 CLI 场景组、1359 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39560549976) | 26/26；各目标及 EL7/EL8 基线各 212 项 Qt 回归；129 分析测试、34 移植测试 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力协商、非法 CPU 和非特权拒绝通过 |
| 本机用户源码 | 50 项移植检查，37 通过、13 项因工具链/系统条件跳过 |
| REA / 新原指令实验 | 八个函数静态分析；768 组循环边界实验、两项本机回归通过 |
| 真机 / Secure Boot | 未执行 |

43/43 功能验证任务绑定同一最终提交。首轮 CLI 测试缺少 parser 声明头文件，
修复后重新运行完整矩阵；首轮失败或被替代的任务单独记录，不计入成功任务。

新核心覆盖全部 255 个合法核心列表语法、稀疏/单核/八核选择、无效输入零访问、
错误平台、每个核心位置的固件拒绝/能力变化/身份变化/查询失败、
中间核心每个访问位置的故障与取消、超时及首错停止。
CLI 新增 44 份报告，验证逐核 JSON 的 null/布尔/掩码和部分完成语义。
GUI 新增四项回归，覆盖完成、部分失败、无效/确认中变化和异步目标变化。
所有硬件访问均使用合成替身；接受命令不等于实际频率生效。

Linux x86_64 目标为 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、
Oracle Linux 7.9（EL7）和 Rocky Linux 8/9/10。Windows/macOS 仅验证共享核心。
CLI 在无显示服务、Qt、编译器及宿主寄存器设备的干净容器验证安装、非特权诊断、卸载与重装。
包版本仍为 2.0.1，同版本安装使用发行版重装方式。

每个 Linux 目标保留 122 张截图，共 1342 张；本轮人工查看 Ubuntu 24.04 与 EL7 的
选定核心完成和部分失败四张新截图。完成截图保留成功明细，底部为再次应用被拦截、需重新读取能力的提示。
其余 1338 张未逐张人工检查，全部是合成数据。

`dist/gui-cli-amd-oc-cores-8c85a28/` 含 33 个 GUI/CLI/DKMS 包与校验表。
根目录源码包对应精确云端提交，`updated-source/` 另保留八项用户扩展及后续研究/验证文档。
287 个生产/构建路径逐项核对；用户源码仅保留已记录的八项扩展差异。
CLI 包 367,296–584,580 字节，
GUI 包 11,749,084–16,634,384 字节。

两项新分析测试在生产提交之后本机完成，不计入云端 129 项。
768 组实验改变的是原命令写入函数的合成返回值，不是实际固件响应或真实 I/O 故障。
仍未验证物理目标映射、活动核心发现、频率实际读回、逐核/CCD PM 布局、rails、
动态内存/EC、其它 AMD 平台、Intel 服务器及剩余板级功能。
