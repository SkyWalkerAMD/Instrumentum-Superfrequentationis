# REA：手动 OC 与原版核心选择换算

2026-10-21，实际运行 [REA 云端任务 39553845129](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39553845129)。
工具固定为 REA 6.7.0、Ghidra 12.1.4；十个原函数查询均完成，study.error 与每条记录 error 均为空。
完整函数映射、原 ELF 函数字节哈希、输出文件哈希及固定公开来源见[机器记录](validation/amd-manual-oc-rea.json)。
Ghidra 地址相对 ELF 偏移增加 `0x100000`，记录同时保留两者。

本批[运行时实现](amd-manual-oc.md)支持手动 OC 模式以及全核/明确单核频率。
REA 用于还原原程序路径；新 RSMU 消息、参数与能力位同时核对固定公开实现，未将 BIOS 或 MP1 命令直接移用。

| 原入口 | 原 ELF 地址 | 研究结论 |
|---|---|---|
| enableocmode / amd_disableocmode | `21FB00` / `21FC00` | BIOS `24/25`、arg0=1；Phoenix/GPT 分支走 MP1 `17/18` |
| Wr_AMD_freq | `21FEB0` | BIOS `26` 或 MP1 `19`，传递原 32 位输入 |
| per_ccx_freq / one_core_freq2 | `220110` / `220430` | Shimada 路由到 modern helper；核心选择不能简单照搬 |
| 两个 fast 入口 | `21EFE0` / `21F1F0` | 只写 arg0 与命令，不清空其余五个参数 |
| Wr_ocvid | `21FD00` | 输入钳制 700..2400，Shimada 分支编码 `(mV-245)/5` 后取整 |
| set_core01_rails | `239990` | 仍是未验证语义的双 rail 事务，未开放硬件写入 |
| per_ccx_oc_apply 槽 | `752270` | 隐式启用 OC、默认写入 1250 mV，再发频率；Applied 不能作为成功证据 |

## 原指令实验

[回放工具](../analysis/tools/legacy-amd-oc-frequency.py)固定原 ELF SHA256
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`，提取十二个完整函数及有界原常量。
模式/频率/VID 分支执行原指令；SMU、MP1、平台判定、锁与定时调用由合成边界替代。
不运行原 ELF 入口，不接触 PCI 或真实固件；不将边界返回值解释为固件规范。

共 1217 条回放：36 条模式/全核路由，1152 条单核/CCD 编码与频率边界，20 条非法输入，9 条 VID 输入。
额外四项[分析回归](../analysis/tests/test_amd_oc_frequency.py)已本机执行通过；产生于生产提交之后，不计入该提交的云端分析测试。

一个可重放的原版问题是：modern 单核 helper 的平坦编号换算同时使用 `target / 8` 和 `target % 7`。
慢路径在结果大于零时再减一，fast 路径直接减一。对编号 0..127，
慢路径有 17 个、fast 有 19 个结果与按八核心线性分组不同。这只描述软件编码，尚未实测物理目标。

| 输入 / 路径 | 原编码（5000 MHz） | 软件换算观察 |
|---|---|---|
| core 14，普通或 fast | `00601388` | 编到 CCD 0/core 6，而非线性分组的 CCD 1/core 6 |
| core 0，fast | `F0001388` | 下溢后的 CCD 字段为 15 |
| core 7，fast | `F0701388` | 同样编码 CCD 15 |
| core 128，普通或 fast | `00001388` | 超范围输入回绕到较低目标 |
| CCD 16，普通或 fast | 从 `00001388` 起共八条 | CCD 编码回绕，仍执行整个八核心循环 |

原单核频率使用 20 位掩码，`1048576` 变成零，`-1` 变成 `1048575`。
原全核入口则把 `-1` 作为 `FFFFFFFF` 原值传出。新接口均在访问前拒绝越界输入。
每个原 CCD 请求即使合成写入边界均返回 `FE`，也继续发送八条请求；这不能证明物理核心存在、固件接受或成功设置。

新接口不使用平坦编号自动映射，不暗中展开整个 CCD；用户明确给出固件 CCD/core。
CPU 拓扑读取仍不足以证明禁用 CCD/core 的固件排列，故结果保留 `physical_mapping_verified=false`。

## 尚未获得的证据

本轮未解决逐核/CCD 功耗表偏移与长度冲突，也未取得双 rail 写入及物理对象映射依据。
原 VID 转换与公开 SetOverclockCpuVid 的存在，不证明双 rail 打包、两条命令副作用或电压回读可靠。
动态内存与 EC 动态 EDC 仍按[既有研究](amd-pbo-dynamic-research.md)保持未开放。
真实硬件频率、功耗、电压、稳定性、其它 AMD 型号、Intel 服务器及板级控制仍需要各自的协议与验收。
