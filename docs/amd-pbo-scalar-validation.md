# AMD PBO Scalar 整批验证

2026-10-21：验证提交 `7467c96c11caedcfa4f78af32c89401f0c446e99`。
GUI / CLI 共用核心新增 Shimada 1Ah/08h PBO Scalar 读取、整数 1..10 设置、旧值检查和精确回读。
见[实现与用法](amd-pbo-scalar.md)、[REA 与功耗表研究](amd-pm-percore-layout-research.md)、
[机器可核对的完整记录](validation/amd-pbo-scalar-ci-7467c96.json)。

| 检查 | 结果 |
|---|---|
| [共享核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39532156812) | 4/4；Ubuntu 22/24、Windows、macOS 各 26 CTest、391 场景组；PBO Scalar 10 组 |
| ASan / UBSan | 五个既有离线解析目标通过，不代表本次硬件设置路径有 sanitizer 或真机验收 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39532156802) | 13/13；11 个 Linux 目标各 28 CTest、53 CLI 场景组、1200 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39532156784) | 26/26；各目标及 EL7/EL8 基线 195 项 Qt 回归；120 分析测试、34 移植测试 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力协商、非法 CPU 和非特权拒绝通过 |
| 本机用户源码移植检查 | 50 项，37 通过；13 项因本机缺 Linux/C++ 工具链跳过，对应验证在云端完成 |
| 新增原指令研究 | PBO/邮箱 79 场景、PM 布局 144 组合；9 项新分析回归进入本次云端 120 项测试 |
| 真机 / Secure Boot | 未执行 |

43/43 功能验证任务绑定同一提交。最初 `af10474` 的 Windows/macOS 核心作业触及
PBO 故障遍历测试的 90 秒总时限，未报告断言失败；本次将该测试程序的预算调至 240 秒后整批重跑通过。
生产传输的轮询次数、单次访问及事务期限没有放宽。旧任务作为 superseded 记录保留。
本机完整分析尝试另有一项既有 Intel TVB 仿真触及 1 秒墙钟限制，单独重跑两项通过；
完整通过的 120 项结果取自本次云端记录，未把本机失败尝试写成通过。

覆盖范围为 Linux x86_64：Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、
Oracle Linux 7.9（EL7）、Rocky Linux 8/9/10。Windows/macOS 验证共享核心，未提供完整 GUI/CLI 安装适配。
CLI 在没有显示服务、Qt、编译工具和宿主寄存器设备的干净容器完成安装、非特权诊断、卸载、重装。
包版本仍为 2.0.1；同版本本地更新应使用发行版的重装方式。

保留 1210 张 GUI 截图。本轮人工查看 Ubuntu 24.04 与 Oracle Linux 7.9 的 Scalar 读取、
成功回读、已接受但回读不符页面，共六张；其余截图未逐张人工检查。全部页面使用合成数据。

`dist/gui-cli-amd-pbo-scalar-7467c96/` 包含 33 个 GUI/CLI/DKMS 包及校验表。
根目录源码包对应精确云端提交；`updated-source/` 包含已同步的用户源码、八项保留扩展与后续研究文档。
282 个生产/构建路径逐项核对。CLI 包 347,902–564,728 字节；
GUI 包 11,541,991–16,216,558 字节。

本轮实际使用 REA 分析 11 个函数，并核对原始指令。逐核/CCD PM 表发现版本长度、数组重叠和字段含义冲突，
仍未启用生产解码。PBO Scalar 回读一致只确认固件数值，不能证明实际电压、功耗、稳定性或跨固件兼容性。
Fmax/rails、其它平台调参、Curve Shaper、原生 PM 与板级功能仍有未还原部分。
