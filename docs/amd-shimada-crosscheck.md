# Shimada 曲线与 PBO 的外部实现交叉核对

2026-10-10 核对了原版 OCTool ELF、ZenStates-Core、linux-corecycler 和后者固定的 ryzen_smu 驱动源码。
本记录新增协议证据，不宣称新增 AMD 曲线/PBO 设置功能。

| 项目 | 核对结果 |
|---|---|
| Shimada RSMU 地址 | ZenStates 的 command/response/argument 与已还原查询一致：`03B10924/03B10970/03B10A40` |
| 曲线命令 | ZenStates 明确列出单核 `06`、全核 `07`、查询 `A3`；原 OCTool PM 构造分支也保存 `06/07` |
| 单核参数 | ZenStates 明确将 margin 收窄到低 16 位，CCD/CCX/core 另占高位；原版 setter 的 `0F0FFFFF` 掩码不能直接当作此合同 |
| PBO 命令 | ZenStates Shimada 表为 PPT/TDC/EDC/温度 `56/57/58/59`；此前还原的 OCTool 非 mobile、非 Granite 分支为 `53/54/55/56`，不能混用 |
| PCI 间接访问 | OCTool 查询为 B8/BC，PM setter 使用对象中的端口、初始化值 F8/FC；核对的 Linux 驱动使用 C4/C8。尚未证明这些通道在目标平台的等价性 |
| 型号与运行证据 | linux-corecycler 文档列出 Shimada，但它固定的驱动版本在 family 1Ah 型号选择中没有 08h 分支，未知型号返回失败；支持表不是端到端实测证明 |

原版固定 SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
原 PM 构造函数的 Shimada 分支 `0x2ccf60` 保存查询候选 `D5`、全核 `07`、单核 `06`；
独立 AMD_PBO 查询函数则使用 `A3`。因此不能照抄原版连续发送候选命令的方式。
原版目标索引来源及掩码分析见[曲线目标映射](amd-curve-target-mapping.md)。

仍需确认：可使用的写入通道、单次命令的完整参数合同、固件支持范围、
稀疏/熔断核心的实际索引映射，以及修改后的可靠回读。
查询成功不能证明该核心槽存在，也不能单独证明 setter 协议有效。
当前软件继续保留已有的显式固件 CCD/core 查询。

原始来源与版本：

- [ZenStates Shimada 设置](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)
  与[单核曲线编码](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetPsmMarginSingleCore.cs)。
- [linux-corecycler 命令表](https://github.com/Daaboulex/linux-corecycler/blob/da74486f9f5e2741ca4c813012bd042082ec1213/src/corecycler/smu/commands.py)
  与[固定驱动版本](https://github.com/Daaboulex/linux-corecycler/blob/da74486f9f5e2741ca4c813012bd042082ec1213/nix/ryzen-smu.nix)。
- [ryzen_smu 型号选择及发送实现](https://github.com/amkillam/ryzen_smu/blob/d2983668300dd2a598e5a7dc40e71ce0678cc270/smu.c)
  与[PCI 端口定义](https://github.com/amkillam/ryzen_smu/blob/d2983668300dd2a598e5a7dc40e71ce0678cc270/smu.h)。
- [来源清单](validation/amd-shimada-crosscheck-sources.json)。

## 后续线索：Shimada 驱动候选补丁

2026-10-10 另行核对仍处于 open 状态的 [ryzen_smu PR #62](https://github.com/amkillam/ryzen_smu/pull/62)，
固定其分支提交 `c12c1e60ba8eb25a2cf19d76a3d5b57ca82057fd`，而不是把它当成已合入的主线版本。
它的 [smu.c](https://github.com/Bjorgz/ryzen_smu/blob/c12c1e60ba8eb25a2cf19d76a3d5b57ca82057fd/smu.c)
新增 family 1Ah/model 08h，RSMU 地址与上表一致；
[smu.h](https://github.com/Bjorgz/ryzen_smu/blob/c12c1e60ba8eb25a2cf19d76a3d5b57ca82057fd/smu.h)
仍采用 C4/C8。Shimada 的 MP1/HSMP 地址留空，不能据此替换原 OCTool 对这些名称的使用。

PR 作者报告在 9995WX、SMU 115.46.0 上读取 PM 表；这是外部作者报告，**本项目没有复测**。
源码使用 05 查询表版本、04 查询基址、03 传输表；版本 730204～731004 有八个明确长度，
其中 730C04 为 0x23A4。它先查基址再传输，对首次 FD 拒绝另有最多四次延迟重试。
这些初始化/传输会发送命令并涉及物理内存映射，不能当成普通只读寄存器采样接入。

[关联 issue #60](https://github.com/amkillam/ryzen_smu/issues/60)讨论的固定根复合体、物理内存
所属范围等保护尚未全部出现在本次固定源码中。
[drv.c](https://github.com/Bjorgz/ryzen_smu/blob/c12c1e60ba8eb25a2cf19d76a3d5b57ca82057fd/drv.c)
是首个匹配设备原子占用全局状态，并没有限定 0000:00:00.0。

这条线索补充了型号分支和 PM 表初始化顺序，尚未解决原版访问端口等价性、稀疏/熔断核心
映射、曲线/PBO setter 参数与完整回读。已有依赖固定版本缺少 08h 的结论仍然成立；
新增候选分支与原固定依赖分开记录。三个文件的 Git blob 与 SHA256 见
[补充来源清单](validation/amd-shimada-pr62-sources.json)。本轮未复制其驱动实现或新增硬件写入。
