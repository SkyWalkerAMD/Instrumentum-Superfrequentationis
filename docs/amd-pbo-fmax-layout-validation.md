# Fmax、rails 编码与 PM 布局诊断整批验证

2026-10-21：生产代码验证提交 `0d360d7ee5b1a704a8fdfb2bbe42f51fe6ee4bf1`。
GUI / CLI 共用实现新增 Shimada Fmax 能力读取和命令确认、rails 离线编码、逐核/CCD 布局冲突诊断。
Fmax 没有当前设置回读；rails 尚未开放写入，逐核/CCD 尚未开放真实遥测解码。
见[用法与 REA 研究](amd-pbo-fmax-layout.md)、[完整机器记录](validation/amd-pbo-fmax-layout-ci-0d360d7.json)。

| 检查 | 结果 |
|---|---|
| [共享核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39540062423) | 4/4；Ubuntu 22/24、Windows、macOS 各 27 CTest、398 场景组；新增 Fmax/rails/布局 7 组 |
| ASan / UBSan | 五个既有离线解析目标通过；不代表新硬件设置路径有 sanitizer 或真机验收 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39540062364) | 13/13；11 个 Linux 目标各 29 CTest、55 CLI 场景组、1221 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39540062397) | 26/26；各目标及 EL7/EL8 基线 199 项 Qt 回归；122 分析测试、34 移植测试 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力协商、非法 CPU 和非特权拒绝通过 |
| 本机用户源码 | 50 项移植检查，37 通过、13 项因工具链/系统条件跳过 |
| 新原指令实验 | 512 个熔丝输入、2 个访问故障、1025 个 rails 输入及 128 个跨版本构造组合；均使用合成硬件边界 |
| 真机 / Secure Boot | 未执行 |

43/43 功能验证任务绑定同一提交。初始 `8dea5ea` 的 EL7 构建发现
`slots` 成员名与 Qt 宏冲突，已改名为 `slotCount` 并整批重跑；JSON 字段名保持不变。
初始核心任务已成功，其余旧矩阵被替代，未将旧任务算进本轮通过数量。

主矩阵中三个新页面的截图生成测试通过，但截图没有进入旧 CI 复制清单。
已补齐六条复制命令，并在独立[截图验证任务](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39543777766)中
重新编译 Ubuntu 24.04 / EL7 的 GUI，各 199 项回归通过、三张新截图归档成功。
补充任务逐项核验运行时应用输入与主矩阵一致；额外变化只有归档脚本、补充工作流和
截图测试中的六处事件循环刷新。首次六张截图在布局完成前抓取，部分长说明被截断，
已在测试中等待 Qt 布局后重新验证，未改变运行时界面实现。
合计 43 个主矩阵任务与 2 个补充任务通过；安装包与根目录源码仍对应 `0d360d7`。
两次验证脚本提交自动触发的重复矩阵已取消，未计作通过，记录中保留其链接及取消状态。

Linux x86_64 覆盖 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、
Oracle Linux 7.9（EL7）、Rocky Linux 8/9/10。Windows/macOS 只验证共享核心。
CLI 在没有显示服务、Qt、编译工具和宿主寄存器设备的干净容器完成安装、非特权诊断、卸载、重装。
包版本仍为 2.0.1；同版本本地更新应使用发行版的重装方式。

最终验证集为主矩阵的 1210 张截图和补充任务的六张新截图，共 1216 张。本轮人工查看 Ubuntu 24.04 与 Oracle Linux 7.9 的 Fmax 命令确认、
rails 编码、PM 布局诊断两轮截图，共十二张，其中修正后的六张作为最终视觉记录；
初次截图另存于被替代的补充记录。其余截图未逐张人工检查。全部页面使用合成数据。

`dist/gui-cli-amd-pbo-fmax-layout-0d360d7/` 包含 33 个 GUI/CLI/DKMS 包及校验表。
根目录源码包对应精确云端提交；`updated-source/` 包含同步后的用户源码、八项保留扩展、
后续研究文档、本机运行过的构造实验工具及截图归档修复。283 个主矩阵生产/构建路径逐项核对；
三个后续验证文件单独记录哈希和验证来源，八项用户扩展保持原状。
CLI 包 357,056–574,444 字节；
GUI 包 11,598,615–16,334,808 字节。

本轮实际运行 REA 分析 12 个函数，并核对原指令。Fmax 命令确认仅支持
AMD 1Ah/08h、PCI 1022:153a、BDF 0；response=1 仍保持 `verified=false`。
Rails 编码和功耗表诊断完全离线。其它 AMD 平台、真实 core/CCD 布局、rails 写入、
原生 PM 传输以及未完成的板级功能仍需独立协议或硬件样本；本轮不宣称全部还原。
