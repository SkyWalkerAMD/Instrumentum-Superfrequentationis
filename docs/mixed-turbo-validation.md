# 睿频阈值与混合内存容量：验证交付

生产提交 `64339183de462a9ba75655f66d543135255d1d72`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/mixed-turbo-ci-6433918.json)绑定同一源码的云端任务、日志、截图、包和 SHA256。

GUI / CLI 同步新增两项：

- [Intel 中间活动核心阈值](turbo-thresholds.md)：B7 的 P/E 表，可改已有中间组的
  阈值，保留全部倍率、其它字节和末组覆盖范围；新旧值、权限、锁与完整读回共同验证。
- [DDR5 混合 rank 容量](spd-asymmetric.md)：两种密度、器件位宽和堆叠层数分别计算，
  显示偶数/奇数组的 rank 数、容量分项与总量；未知几何、CRC 损坏时不发布推算容量。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140350467) | 4/4；各 13 CTest、185 场景组，含 15 组睿频、55 组 SPD |
| ASan / UBSan | SPD、PM、UMC 快照测试通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140353378) | 12/12；十目标各 15 CTest、17 组命令场景、392 份 JSON，其中 91 份 SPD、13 份阈值报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140349232) | 23/23；十目标及 EL8 基线各 77 Qt 测试、11 套目标内核 VM |
| 原指令/分析回归 | 既有 74 项通过；本轮沿用已固定的原版数量表读写研究 |
| 用户原目录本机 | 48 项：36 通过、12 因本机环境缺工具链跳过；文档与确定性源码归档检查通过 |

初次 CLI 回归发现报告数量断言未随新增场景更新；随后静态复核修复了异步刷新时
父窗口暂时禁用导致输入框不能恢复的问题，并加入 GUI 状态检查。
截图复核后又缩短设置结果提示，增加完整容量分项/合计同屏的第二张截图和布局断言。
最终上述三条流水线全部使用生产提交，未用早期通过结果或截图替代最终验证。

十套 CLI 均在无桌面、无 Qt、无构建工具的独立环境完成安装、普通用户启动、卸载与重装。
保存 500 张截图，实际查看的子集与结论见证据；合成读数不是实机测量。
29 个本轮文件同步原目录，23 个换行规范化后与生产提交一致；六份既有文档只应用
功能变更，保留用户目录独立历史。17 个运行时/测试/构建源文件全部匹配生产提交。
本轮不修改驱动/HAL/ABI，原有内核装载与权限检查仍经过验证。

## 交付文件

`dist/gui-cli-mixed-turbo-6433918/` 含 10 GUI、10 CLI、10 可选 DKMS 包，源码、
源码校验侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以提交目录和哈希区分。
源码 4,271,133 字节，SHA256 `d914a4ecbc40cb704d768bbbf47b04780643e16b9d952205453e3ac093f77d0a`。
安装包及源码固定于生产提交；最终交付报告与状态记录属于其后的文档提交。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 11,010,000 | 258,564 | 19,068 |
| el9 | 10,665,343 | 257,637 | 19,060 |
| el10 | 10,662,426 | 275,094 | 19,160 |
| ubuntu20.04 | 14,548,214 | 304,032 | 10,674 |
| ubuntu22.04 | 14,548,186 | 296,852 | 10,670 |
| ubuntu24.04 | 14,548,196 | 315,482 | 10,668 |
| ubuntu26.04 | 14,548,194 | 324,882 | 10,668 |
| debian11 | 14,548,192 | 294,366 | 10,666 |
| debian12 | 14,548,186 | 301,326 | 10,668 |
| debian13 | 14,548,214 | 309,322 | 10,668 |

## 剩余范围

原版全部功能尚未还原：[状态清单](recovery-status.md)仍保留 AMD PStates 写入、
固件/物理目标映射、曲线/完整 PBO、完整 PM 表与原生传输；Intel server 电压域、
其它型号 VF、逐核 override、TVB/fabric/BCLK、末组阈值与组数量重建；DDR4 非对称容量、
XMP/EXPO 未确认的启用/版本语义，以及内存训练和板级 PMIC/VRM/EC/时钟写入。

本轮另核对 Intel 在 EDK2 中的 DDR4 定义：
[`SdramSpdDdr4.h`](https://github.com/tianocore/edk2/blob/1a44fc33295d32653fb114e1f1b9b91a81d47239/MdePkg/Include/IndustryStandard/SdramSpdDdr4.h)，
blob `d413bf4cc88535d9c181f1052065c7eb38d6175d`。它确认第二组封装的密度比字段位置，
但没有完整容量换算规则；本轮未取得能补齐密度等级与 rank 分配的直接规范，故没有
把 DDR5 的规则套给 DDR4。公开格式/固件定义不足的部分仍单独保留，未以原始寄存器入口
代替完成状态。

四台目标真机、调参稳定性、Secure Boot 与完整桌面交互验收未完成。
配置读回不能证明物理效果；Windows/macOS 核心通过不表示这些系统已完成硬件适配。
