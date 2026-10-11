# Shimada PM 表：版本、公共字段与剩余接入条件

2026-10-11 后续实现：[PM 遥测与快照](amd-pm-recovery.md)已接通以下八版本的 24 项公共字段、
GUI / CLI 离线互通，以及系统已有兼容驱动时的 sysfs 表读取。未增加原生 DMA 传输或调参写入。
以下保留此前来源核对和剩余边界，公共字段不等同于整张 PM 表已经还原。

## 固定来源

[ZenStates-Core PowerTable.cs](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/PowerTable.cs)，
提交 `869b7e82572337e5a673e92230edc92bddf0e44b`，Git blob `62a46bab220c9b97cda415152fa1e1c173f1946e`。
本地 UTF-8/LF 来源副本 SHA256 为 `dfba3e7befc5c7672af1f486df79fa16ee11405a59d07f43a0de4ab66516d0e6`。
只核对布局事实，没有复制或链接其解析代码。

| 版本 | 表长度（字节） |
|---|---:|
| 730204h | 2844 |
| 730404h | 4100 |
| 730604h | 5356 |
| 730804h | 6612 |
| 730A04h | 7868 |
| 730C04h | 9124 |
| 730E04h | 10380 |
| 731004h | 11636 |

这些长度与[已核对的驱动候选补丁](amd-shimada-crosscheck.md)一致。
来源还提供 `000730h` 通用回退表，但它不是以上八个固件版本之一；不能仅依据版本前缀匹配，
就宣布一个未知完整版本受支持，也不能依据表长度把空洞核心重新编号。

## 可继续核对的公共字段

八个明确版本共同使用以下字节偏移，来源按单精度浮点数读取：

| 字段 | 偏移 |
|---|---|
| FCLK / UCLK / MCLK | 20Ch / 21Ch / 22Ch |
| VDDCR_SOC | 1A4h |
| CLDO_VDDP | 5CCh |
| CLDO_VDDG_IOD / CLDO_VDDG_CCD | 714h / 71Ch |
| VDD_MISC | 1B8h |
| SVI3 起点 / 每条电源轨跨度 | 178h / 14h |

SVI3 电源轨顺序为 VDD、VDD1、SoC、Misc。每轨相对偏移 0、4、8、10h 分别是
来源映射的 VID、测量电压、电流、VRM 温度；0Ch 位置没有被其公开读取选择器解释。
它不是四套互换的电压来源，也不是用于 PStates MSR 的 VID 编码换算表。

来源还会用实测 BCLK/100 修正三个时钟字段。单独解析表文件时没有这个外部数据，
不能把未经修正的存储值当成已测得的完整实际时钟。浮点 NaN/Inf、精确长度、端序、
字段错误隔离及版本门控，现由[共同解码器](amd-pm-recovery.md)独立实现并测试。

## 实时读取与目标映射的剩余条件

同日重新读取 [ryzen_smu PR #62](https://github.com/amkillam/ryzen_smu/pull/62)：仍为 open，
head 仍为 `c12c1e60ba8eb25a2cf19d76a3d5b57ca82057fd`，未发现替代前次核对的新增实现。
[issue #60](https://github.com/amkillam/ryzen_smu/issues/60)仍未关闭。

原生传输仍需贯通版本查询、基址查询、传输顺序和每一步响应；确认使用哪一组 PCI 间接端口；
限定正确根复合体及固件返回内存范围，再完成映射和取消/超时/错误处理。
现有字段表没有提供熔断核心、CPUID ID 与固件逐核目标 ID 的完整映射。
因此本次来源核对仍不能解锁逐核曲线、完整 PBO 或满足跨核一致性的 PStates 写入。

外部补丁作者的 9995WX 实机报告是参考证据，本项目未在该机器复测；
云端编译和合成回归也不能替代这些固件及物理硬件验证。
