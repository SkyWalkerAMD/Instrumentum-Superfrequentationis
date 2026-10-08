# 原版平台判断与 Controls 分派

日期：2026-10-08。输入仍为原 Linux ELF `44598dc8…aff10`，未修改 GUI 或设备 ABI。
本页补充此前只定位到 `is_it_asus` 调用点的分析。函数名、截图标题、CPU 营销型号均不能代替分支证据。

云端 `b70df08 / 37758588798` 十目标共23任务全部成功，Linux分析7项测试无跳过。
550项平台分派的Linux/Windows观测除环境字段外完全一致；作业及下载件哈希见
[该轮验证记录](validation/legacy-dispatch-ci-b70df08.json)。此结论不包含之后新增的Qt/MSR实验。

## 可复现输入及验证层次

```sh
python3 -m pip install --target build/reference-tools -r analysis/tools/requirements-emulation.txt
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/audit-legacy-dispatch.py \
  build/input-audit/octool --output docs/validation/legacy-dispatch-analysis.json \
  --export-fixture analysis/fixtures/legacy-platform-dispatch.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/emulate-legacy-dispatch.py \
  --output build/dispatch-emulation.json
```

- [静态报告](validation/legacy-dispatch-analysis.json)：28 个详细函数，以及 MainWindow 的 83 个
  `on_*() -> void` 槽的直接调用/尾跳转/文字清单。源 ELF 的 42,115 个声明函数解码完整。
  **83 个槽的清单不是 83 个面板的业务逻辑已恢复**；尚不包含 QObject 自动连接和间接分支解析。
- [公开片段](../analysis/fixtures/legacy-platform-dispatch.json)：13 个完整决策函数的 3,695 个原指令字节，
  SHA-256 `517a81ab352df1c257cf74146a6cb5c47a2be640abf7c5215caf060f25e62366`。
  提取时校验完整 ELF；公开测试只校验片段哈希，不上传原 ELF/旧模块。
- [模拟器](../analysis/tools/emulate-legacy-dispatch.py)：550 个有界用例，Windows Unicorn 2.1.4 已通过；
  云端结果另行追加。每例最多 10,000 条指令及 1 秒 emulator 时间；未知调用、越界执行、硬件指令立即失败。
  PCI、DMI 返回字符串、hwloc、分配器和 Qt 边界是合成输入；每次在实际面板构造函数**之前**停止。
  不执行原 Qt、原 ELF 启动、真实 VRM 检查、设备操作或内核代码，不把此报告称为 GUI/真机通过。

用例包含 PCI 区间两端、错误厂商、重复/短 PCI 行、大小写和字符串优先级、hwloc 错误返回、
缓存标志的全部组合以及 OC 状态的全部布尔组合。异常向量只走到 C++ 抛异常入口，未模拟异常展开。
Qt 字符串/窗口边界为简化模型，不能据此证明真实 Qt 分配器、提示框或析构正确。

## PCI 判断实际比较什么

| 原函数 | 原地址 | 指令确认的条件 |
|---|---|---|
| `check_if_amd` | `0x4b9e10` | `0000:00:00.0` offset0 word 为 `0x1002` 或 `0x1022` |
| `isit_adl` | `0x378b30` | offset0=`8086`；offset2 为 `4648/4668/4660` 或闭区间 `a700..a780` |
| `isit_rpl` | `0x378ad0` | offset0=`8086`；调用 `usleep(1000)` 后 offset2 在 `a700..a707` |
| `isit_arl` | `0x378ba0` | 只读 offset2，闭区间 `7d00..7fff`，**不检查厂商** |
| `isit_nvl` | `0x378aa0` | 只读 offset2，闭区间 `d700..d740`，**不检查厂商** |
| `isit_rkl` | `0x378bd0` | 依次查 `8086:4c43/4c53/4c63/4c33`，首个返回值不为 `-1` 即真 |
| `isit_spr` | `0x3902a0` | 整份 PCI 列表里 `8086:3258` **恰好一行** |
| `isit_gnr_sp` | `0x390410` | 同一 ID **多于一行**，有符号 count 比较 `>1` |
| `is_intel_hedt` | `0x4bbb40` | 查询 `8086:3251`，返回 BDF/-1；调用者检查不等于 `-1` |

以上是旧程序数值规则，不把 `3258` 数量解释成 CPU 代际、插槽或真实支持范围。
计数没有去重；合成重复行也会令 `isit_gnr_sp` 为真。只有该函数返回真，不能证明硬件就是 GNR。
也不能依据用户的 i9-14900KS、w5-2565X、658X、9995WX 名称预先填入 PCI ID。

列表来自 `GetPciDeviceList`，每行 uint32 顺序由 `0x3ac440..0x3ac4f3` 确认为
bus/device/function/vendor/device_id/packed_BDF；底层 `0x36e4bf/0x36e4d2` 实际读 PCI offset0/2。
两计数函数访问第 3/4 项，行长不足会走 `__throw_out_of_range_fmt`，不是返回“不匹配”。
列表制造器和 libpci 枚举在本轮仅静态核对；模拟器提供的是合成列表。

## 主板名称检查包含大小写、优先级与写路径

`getmobo` 调 `test_dmi_get_mb(1)`；品牌函数传 `0`。DMI 表解码先找 Type2，
`dmi_decode_mb` 的 `0x3a2f60/0x3a2f70` 分别使用字段偏移5/4的字符串索引。
这对应旧程序所读的 product/manufacturer 路径；不是简单读取 sysfs board_name 文本。
无可用表的既有失败分支可能返回空字符串。完整异常、畸形表、分配失败及所有 DMI fallback 尚未穷尽。

`is_it_asus`（`0x3b2000`）对字符串执行**区分大小写的子串查找**，先后次序为：

1. product 含 `ASUS`、`ROG` 或 `STRIX`：接受，不查 VRM。
2. 否则 product 含 `MANGO`：拒绝，后面的品牌和 `GNR/OHTANI` 都不会再查。
3. 否则 product 含 `OHTANI` 或 `GNR`：接受。
4. 否则读取 manufacturer；含 `ASUS`、`ROG` 或 `STRIX` 时调用 `checkvrm`，仅其返回值非0才接受。
5. 其他情况拒绝。

因此 `ROG MANGO` 接受而 `MANGO GNR` / `GNR MANGO` 拒绝；`asus` 不等于 `ASUS`。
这些合成名称只验证比较顺序，不是已实测主板身份，也不是建议通过改 DMI 绕开检查。

`checkvrm`（`0x43bc10`）并非只读。在品牌 fallback 分支中可达：

- `0x43bd9f/0x43bdb0` 调 `EC::Wr_ECRAM`；`0x43bdbe` 调 `Wr_ECSMB_Byte`。
- 其他分支在 `0x43bf82/0x43c016` 调 SMBus byte 写，`0x43c1ff` 调 word 写。
- 部分分支调用 `vrm1420::stopec` 并继续 EC 操作。

这里仅证明存在这些原始调用路径；尚未解释写入含义/适用硬件，且没有运行它们。
仅放开 DMI 权限就可能从原先拒绝启动进入这些路径，不能把修复 DMI 读取视为纯显示修复。
这补充了先前已发现的 `en_ec_decoding` 启动写入。

## Controls 确切分派顺序

入口 `MainWindow::on_actionControls_triggered` 位于 `0x8ec1e0`：

| 按顺序匹配 | 到达的类/结果 | 原调用点 |
|---|---|---|
| `check_if_amd()` 为真 | `Not Supported!` 提示，返回 | `0x8ec4a8` |
| 否则 `GLOBAL_IS_NVL` | `intel_ctl6(parent=null, bool=!get_oc_ok(...))` | `0x8ec3e4` |
| 否则 `GLOBAL_IS_GNR_SP` | `intel_ctl5` | `0x8ec552` |
| 否则 `IS_ARL_GLOBAL` | `intel_ctl3` | `0x8ec252` |
| 否则 hwloc count 字段 `>24` 且 `!isit_adl()` | `intel_ctl2` | `0x8ec52c` |
| 否则 `is_intel_hedt()!=-1` | `intel_ctl2` | 同上 |
| 以上均不成立 | `intel_ctl` | `0x8ec594` |

三个全局标志由 MainWindow 启动时设置；Controls 不重新读取其对应谓词。
模拟器把标志的全部16种组合单独测试，包含现实平台未必会出现的矛盾组合，用来确认优先级。
它没有伪造宿主 PCI/DMI、运行原窗口或声称四台实机将进入特定类。

截图中 W790 页面文字与 `intel_ctl2/3` 相似，之前无法确定分派。
现在可确定本 ELF 的 `intel_ctl3` 是 **ARL 缓存标志分支**，不能仅按布局选择它恢复 W790。
W790 18核也不是该 `>24` 条件的充分依据，仍须核对 `3251` 及上游判断。

`proc_class` 的 `+0x2c` 字段来自 `hwloc_get_type_depth(topology, 2)` 后的对象数量。
hwloc2 的类型2是 CORE（[上游枚举说明](https://www.open-mpi.org/projects/hwloc/doc/v2.10.0/a00137.php)），
不等于 Linux CPU 编号或 `getlogicalcpu` 结果（后者在 `+0x24`）。原库 SONAME 是 `libhwloc.so.15`；
不能套用 hwloc3/master 的枚举顺序。模拟器验证原代码传参为2；库本身没有在此模拟中执行。
depth=-1 时字段为0，depth=-2时为-1；init/load失败字段也保持0。
load失败路径没有调用 topology_destroy。上述失败会影响面板分派，不能伪装为真实核心数。

## NVL 的 OptIn 输出被覆盖

`get_oc_ok`（`0x2504a0`）确实调用 `get_overclocking_optin_support`，但随后
`0x2504f6` **把第三个 bool 输出直接写为1**，没有读回该 vector<bool> 的任何位。
最终返回 `!first(get_oc_support()) && rd_oc_enable()`。空的第一向量走异常入口。

静态下游：`get_oc_support` 读 MSR `0x194`、首项取低32位的 bit20；
`rd_oc_enable` 取 `Rd_CAPID0_B_HOSTBRIDGE_CFG_Tuner()` 返回值 bit29；
OptIn helper 读 MSR `0x195`、取 bit3/4/5。此处只记录旧算法的操作数，
未将位名当成硬件规范，也未恢复任何写入。

48 个组合让锁输出为0/1/非零负值、enable为0/1、OptIn三位取全部8种值；第三输出始终1。
Controls 的四种组合均继续走到 `intel_ctl6` 构造边界；失败时传bool=1。
该 bool 之后究竟怎样限制 UI/写入要继续分析构造/slot，不能据此说 OC 被安全禁用。

## 对 EL8–EL10 的实际意义与未决范围

运行库闭包已解决原 ELF 的装载，但上述条件不由发行版版本号决定；原应用没有仅凭CPU型号选择面板。
CI runner 的硬件身份不适合证明这些平台功能。不能通过把失败检查返回真，让原主窗口测试变绿。
现有 EL8 基线重构 GUI/模块仍按已有十目标矩阵验证，本轮只增加分析和离线门禁。

未完成的范围仍包括：各面板寄存器/缩放/写入顺序、AMD其余平台分派、Qt槽的间接跳表、
原 Windows/Linux 两版差异、故障恢复与线程生命周期，以及四台真机实际 DMI/PCI 路径。
“函数完整解码”或“550个合成用例通过”均不等于逆向完毕。更新入口见 [逆向覆盖台账](reverse-engineering-status.md)。
