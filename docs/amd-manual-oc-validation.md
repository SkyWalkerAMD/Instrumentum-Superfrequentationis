# 手动 OC 模式与频率：整批验证

2026-10-21，生产验证提交 `f4332b78223c4de91fd1a76bc7ed955da68a09d1`。
GUI / CLI 共用核心新增 Shimada 手动 OC 模式切换、全核及明确单个固件核心的频率设置。
模式验证仅覆盖能力 bit 0；频率仅确认固件接受，未验证实际频率或物理核心映射。
见[用法](amd-manual-oc.md)、[REA 与原指令研究](amd-manual-oc-research.md)、
[完整机器记录](validation/amd-manual-oc-ci-f4332b7.json)。

| 检查 | 结果 |
|---|---|
| [共享核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39555418688) | 4/4；Ubuntu 22/24、Windows、macOS 各 29 CTest、415 场景组；新增手动 OC 10 组 |
| ASan / UBSan | 五个既有离线解析目标通过；不代表新设置路径有 sanitizer 或真机验收 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39555418707) | 13/13；11 个 Linux 目标各 31 CTest、60 CLI 场景组、1315 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39555418754) | 26/26；各目标及 EL7/EL8 基线各 208 项 Qt 回归；125 分析测试、34 移植测试 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力协商、非法 CPU 和非特权拒绝通过 |
| 本机用户源码 | 50 项移植检查，37 通过、13 项因工具链/系统条件跳过 |
| 新原指令实验 | 1217 条回放、四项本机回归通过；记录 36 个平坦编号换算差异 |
| 真机 / Secure Boot | 未执行 |

43/43 功能验证任务均绑定同一提交，无后续运行时修补。
新核心测试覆盖全部 1048575 个有效全核频率编码、128 个明确核心选择及频率边界，
身份/能力变化、逐访问故障与取消、超时、模式回读失败、禁止命令和六参数初始化。
固件替身忽略设置但返回成功时，模式不会误报验证通过，频率始终保持 `verified=false`。
所有寄存器测试均使用替身，不等同于真实处理器调参验收。

Linux x86_64 目标为 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、
Oracle Linux 7.9（EL7）及 Rocky Linux 8/9/10。Windows/macOS 仅验证共享核心。
CLI 在无显示服务、Qt、编译工具及宿主寄存器设备的干净容器完成安装、非特权诊断、卸载与重装。
包版本仍为 2.0.1；同版本升级应使用发行版的重装方式。

每个 Linux 目标归档 120 张截图，共 1320 张。本轮人工查看 Ubuntu 24.04 与 Oracle Linux 7.9 的
模式切换成功、模式回读失败、单核频率接受、全核频率接受四种新页面，共八张。
其余 1312 张未逐张人工检查；全部为合成数据，不代表硬件效果。

`dist/gui-cli-amd-oc-frequency-f4332b7/` 包含 33 个 GUI/CLI/DKMS 包与校验表。
根目录源码包对应精确云端提交；`updated-source/` 另保留八项用户扩展，并包含后续研究与验证文档。
286 个生产/构建路径逐项核对，用户目录仅保留已记录的八项扩展差异。
CLI 包 364,526–581,800 字节；
GUI 包 11,705,940–16,537,546 字节。

REA 实际查询并导出十个原函数。十二函数的原指令回放及新增四项分析测试在生产构建之后完成，
已在本机执行，不计入云端 125 项分析测试。原单核 helper 的除 8/余 7 混用、fast 编号下溢、
CCD 无条件八核循环和隐式电压行为已保留为可重放证据，新接口未照搬这些行为。

新设置仅支持 AMD 1Ah/08h、PCI 1022:153a、domain 0 / BDF 0。
逐核/CCD PM 布局、rails 写入、动态内存/EC 策略、其它 AMD 平台、原生 PM 传输、
Intel 服务器和未完成板级功能仍需协议或硬件证据。本批不宣称全部还原。
