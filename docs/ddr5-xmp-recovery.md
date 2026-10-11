# DDR5 XMP 3.0 档案读取

GUI 的 Memory / Motherboard 页面与 CLI `spd-decode --file module.spd`、`inventory`
复用 `decodeSpd`。现新增三个厂家 XMP 档案的启用状态、16 字节有界名称、完整原始档案、
VPP/VDD/VDDQ/内存控制器电压，以及 tCK、tAA、tRCD、tRP、tRAS、tRC、tWR、tRFC1/2/sb。
电压单位 mV；时序保留 SPD 存储的 ps 或 ns。零电压编码只表示存储值，不推断 Auto 含义。
现也解码 8 项次级时序，各保留 ps 时间值和 nCK 周期下限，详见下表。
这些值是档案要求值，不表示当前 BIOS 已启用、实际训练结果或稳定性测试结果。
不把 tAA/tCK 的简单除法当作实际 CAS，也不将量化后的 tCK 反推为额定速度档位。

## 字节布局与校验

| 区域 | 起点 | 大小 | CRC 覆盖 |
|---|---:|---:|---|
| XMP 头 | 640 | 64 | 前 62 字节，末两字节存放小端 CRC |
| 厂家档案 1/2/3 | 704 / 768 / 832 | 各 64 | 各自前 62 字节 |

头标志为 `0c 4a`，只解码版本 `30h`。byte 643 的 bits 0..2 分别启用三个档案，
即使档案 1 未启用，也独立处理档案 2/3。名称位于 654/670/686，最多读取 16 字节；
不可打印名称报告无效，不影响已校验时序。未启用档案不解码残留数据。

CRC-16/XMODEM 使用多项式 1021h、初值零。基础区、XMP 头和各档案分别报告结果。
基础区损坏时不解码扩展；XMP 头损坏、截断或版本未知时不信任档案列表。
某个已启用档案截断、CRC 错误或 tCK 为零，只停止该档案的数值解码；其它有效档案和
已校验的基础 SPD 仍可查看。完整档案的原始字节和 CRC 存储值/计算值保留在 CLI JSON。
CRC 一致只证明字节一致，不认证配置合理或适用于当前平台。

捕获不足 642 字节时，XMP 状态为“未捕获”，不是“没有 XMP”。在 832 处检测到 `EXPO`
时，该共享区域不作为档案 3 解码；若 XMP 头同时启用档案 3，则报告布局冲突。
[EXPO 1.0 基础参数](ddr5-expo-recovery.md)另行校验并解码，不依赖 XMP 头有效。
XMP 两个用户档案和 EXPO 增强时序尚未还原；[DDR4 XMP 2.0 读取](ddr4-xmp-recovery.md)已另行实现。

## GUI 与 CLI

GUI 可打开二进制 SPD 文件，或读取内核已绑定设备的 EEPROM；各节显示独立校验状态。
文件读取共用 CLI 的有界普通文件读取器，最多 4096 字节；失败时清空上一文件的表格。
不扫描裸 SMBus，不切换 EEPROM 页，不写 SPD，不应用档案。

CLI 保留已有 `data.spd.fields`，新增 `data.spd.xmp`：`inspected`、可空的 `present`、
`revision`、`error`、`crc`、可空的 `expo_present` 和 `profiles`。
每个档案有 `index/offset/enabled/blocked_by_expo/error/name_valid/name/crc/raw_hex/values`；
数值包含明确的 `name/value/unit`。扩展错误使 `spd-decode` 以状态 3 退出，仍返回基础字段和
其它有效档案。`inventory` 保留逐设备错误。`active_configuration_measured` 恒为 false。

## 次级时序与周期下限

每个有效厂家档案现返回 30 个数值：原有 4 项电压和 10 项基础时序，加上以下 8 对要求值。
表内偏移相对于档案起点。时间为两字节小端无符号 ps；周期下限为一字节无符号 nCK。

| 时序 | 时间偏移 | 周期下限偏移 |
|---|---:|---:|
| tRRD_L | 31 | 33 |
| tCCD_L_WR | 34 | 36 |
| tCCD_L_WR2 | 37 | 39 |
| tCCD_L_WTR | 40 | 42 |
| tCCD_S_WTR | 43 | 45 |
| tCCD_L | 46 | 48 |
| tRTP | 49 | 51 |
| tFAW | 52 | 54 |

GUI 和 CLI `values` 使用 `<时序> minimum`（ps）与 `<时序> cycle lower limit`（nCK）两个名称。
这两个量独立显示，不除以 tCK，不取较大值作为实际训练周期，也不推断零值是 Auto。
零和最大编码 65535 ps / 255 nCK 如实保留；不宣称这些编码合理或已经通过稳定性验证。
原有的 tRFC1/2/sb 仍使用 ns。DDR4 XMP 和 EXPO 不使用这套次级时序布局。

同一档案的 62 字节 CRC 覆盖新增字段。头无效、档案禁用、截断、CRC 失败、tCK 为零，
或第三档案与 EXPO 重叠时，不输出该档案的时序；其它有效档案继续显示。
未知版本不按 XMP 3.0 解码。`values` 追加字段，JSON 版本保持 1；调用方应按名称取值。

字段、单位与公开样本依据固定在[来源记录](validation/xmp-secondary-sources.json)。
[上游字段访问器](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/xmp3.cpp)
与 [GUI 周期下限绑定](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/mainwindow_xmp1.cpp)
用于格式事实核对，未复制或链接其实现。其 `gettCCD_L_WR_tick()` 错取 `gettCCD_L()`，
本实现分别读取 WR 与读时序的原始字段，不使用这个推导路径。
公开独立 XMP 档案含非零次级值；既有 XMP+EXPO 混合样本的这些字段为零，两种情况都记录。
测试使用合成模块，其中第一组次级值与公开独立档案一致；不是目标机器采集或原程序执行。

新增核心测试逐一覆盖 24 个字段字节的全部 256 种编码、邻接字段/档案隔离、
零和最大值，以及改变 tCK 后存储要求保持不变。GUI 检查全部三组的名称/单位/数值和旧行清除；
CLI 检查相同 JSON、损坏/禁用档案及不创建硬件后端。

## 依据与边界

- 固定原 ELF 的 `Get_DDR5_XMP` 位于 `4c1570h`，大小 3798 字节；读取 byte 643，
  检查三个启用位并构造电压/频率/时序行。[静态记录](validation/legacy-ddr5-xmp-static.json)
  固定函数字节和指令；这是静态研究，没有宣称完成该函数的模拟执行。
- [Memtest86+ 固定版本](https://github.com/memtest86plus/memtest86plus/blob/70e4ec1881660970757e8be058c8169700a9d94d/system/spd.c)
  提供 XMP 标志以及 tCK/tAA/tRCD/tRP/tRAS/tRC 的小端偏移交叉依据。
- [DDR5SPDEditor 固定字段定义](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/ddr5spd_structs.h)、
  [电压编码](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/utilities.cpp)
  与其 XMP 实现提供头、档案、名称、CRC 及 EXPO 重叠布局。仅用于格式事实核对，未复制或链接其 GPLv3 实现。
- [spdr 的来源说明](https://github.com/The-Open-Memory-Initiative-OMI/spdr/blob/6b0d2d55f18a87b39c260ed250f0ef23f32a53a2/spdr/src/vendor.rs)
  和公开 TEAMGROUP 样本帮助交叉核对；它使用相同上游参考和样本，不视作完全独立的格式认证。
- [来源与样本校验清单](validation/ddr5-xmp-sources.json)。新增实现采用逐字节读取，无外部运行依赖。

核心回归覆盖全部输入截断长度、头/档案各个损坏位、独立启用位、未知版本、混合 EXPO、
名称边界与整数单位；GUI/CLI 同步覆盖成功及错误展示。云端另用内存和未定义行为检查器执行
解码测试。自动化测试不能替代目标主板的 EEPROM 采集、内存训练、SPD 写入或调参验收。
