# REA：全核曲线、Force/UnForce 与剩余 PM 布局

2026-10-26，实际完成 [REA 任务 40253093159](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40253093159)：
REA 6.7.0 / Ghidra 12.1.4，八个函数，所有工具记录均无错误。
原 ELF SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
Ghidra 地址比原 ELF 偏移多 `100000h`；混合调用约定丢失的参数由完整原符号指令复核。
函数身份、字节哈希、独立公开来源和证据文件见[机器记录](validation/amd-curve-all-rea.json)。

| 原函数 | ELF 偏移 | 结论 |
|---|---|---|
| AMD_PM_LOG::set_curve_opt | `238D90` | 全核入口；端口与消息来自对象成员 |
| AMD_PBO::set_curve_opt | `239400` | 固定 F8/FC；在原 Shimada 构造之后依次发 A3、07，原始 32 位输入未钳制，释放锁后才读 response |
| on_per_ccx_oc_apply_5_clicked | `750B70` | 选择框 0 进入全核 PM_LOG setter；先显示 Applied，再进入后续查询，不能据此证明完成 |
| ForceAllDLDOPsm | `220600` | 在合成 Shimada 分支发送 FD、0E，写 response 寄存器并发布未验证参数 |
| UnForceAllDLDOPsm | `2209E0` | 发送 FD、0F；不是已确认的 CO 清零事务 |
| UnForceCpuVid | `221240` | 发送 FD、13；不是已确认的双 rail 恢复事务 |
| Wr_ocvid | `21FD00` | 复核旧 VID 钳制与编码路径，没有获得双 rail 目标映射 |
| set_core01_rails | `239990` | 复核既有 61→5D 事务；访问状态和主动 response 写入仍不足以证明电压效果 |

[新增原指令回放](../analysis/tools/legacy-amd-curve-all.py)提取五个完整函数：
原 PBO 构造、全核 setter、三个 Force/UnForce helper。
12 个全核输入包含合法边界和整数溢出边界；48 个 helper 场景交叉四种合成平台与四种 response；
另覆盖全部 60 个 PCI 故障位置，共 120 次实验，三项分析回归通过。
PCI 枚举、返回字、故障哨兵、锁和等待为明确的合成边界，没有执行原 ELF 入口或真实硬件。

三个 helper 均先读取并写入输出参数，随后才读取 response。
response 为 busy、拒绝或全 1 时，输出也已发布。60 个故障位置均继续执行原函数尾部。
这些行为说明无法用名称中的 UnForce 推断“恢复默认电压 / 曲线”，运行时未开放它们。

新的[全核曲线功能](amd-curve-all.md)采用独立公开实现确认的 Shimada RSMU `07`、低 16 位 CO 编码，
六参数初始化、完整能力字比较和身份复核，GUI / CLI 同步提供。
它不复刻旧 F8/FC、A3 前导命令、无限制输入或 Applied 提示；成功仅表示固件确认。

## PM 长度的新增约束

对八个精确版本，公开长度都满足 `T = 0x634 + 0x274 × k`，其中 `k` 是版本号的中间字节。
这是长度的算术关系，不能独立证明 `k` 是真实 CCD 数，也不能证明 `0x634` 是固件公共头。

若额外假设存在 `8k` 个 core 槽、`k` 个 CCD 槽，且原版 15 种 core 行、11 种 CCD 行
从 `0x634` 连续排列，则只解释 `(15×8 + 11)×4×k = 0x20C×k` 字节，
仍剩 `0x68×k` 字节。它们可能属于其它字段或不同布局；本研究不为这些字节指定名称或偏移。

八次原默认构造与 mode 4 指令回放另外确认：

| 版本 / 合成 fused 输入 | 原 core / CCD 槽 | 越界字数 | core/CCD 重叠字数 | 连续排列假设的未解释字节 |
|---|---:|---:|---:|---:|
| `730204 / 2` | 16 / 2 | 22 | 0 | 208 |
| `730604 / 6` | 16 / 2（构造回退） | 22 | 0 | 624 |
| `731004 / 16` | 128 / 16 | 0 | 176 | 1664 |

全部八组仍读取 `714h`、`71Ch`，与现有公开 VDDG 字段解释冲突。
完整结果见[容量记录](validation/amd-pm-capacity-review.json)。回放沿用明确的“校准已完成”合成条件；
此前[实际原校准路径研究](amd-pm-native-research.md)也没有改变这些读取组。
因此不能通过平移 CCD 起点、把版本字节当拓扑，或把边界内的浮点数当成瓦特来补齐遥测。

逐核/CCD 真实布局和双 rails 写入仍缺独立带标签样本或对应固件协议。
原生 PM 采集、已有离线布局诊断及 rails 编码预览保持各自合同；本批没有声称真机验收或全部功能已完成。
