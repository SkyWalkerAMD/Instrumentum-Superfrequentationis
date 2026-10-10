# 平台面板恢复：证据与待确认规格

<!-- intel-ratio-recovery-fa39af6 -->
最新生产代码 `fa39af6` 已验证：[核心 4/4](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38011778227)、[Linux 23/23](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38011778246)。
新增 [core/cache 最大 OC 倍频](intel-ratio-recovery.md)，两种设置均保留其它字段并完整回读。
十个 x86_64 Linux 目标每个 29 Qt / 31 Python，各核心环境 6 CTest / 49 场景组，11 套真实目标内核 VM 启动。
安装包保存于 `dist/intel-ratio-recovery-fa39af6/`：每目标 GUI + DKMS，共 20 个 DEB/RPM；另有源码包、侧车 SHA 和 `SHA256SUMS`。
[完整证据](validation/intel-ratio-recovery-ci-fa39af6.json)包含产物哈希、源码比对、原指令观测和 200 张界面截图。
目标物理机器的固件行为、Secure Boot 和实际电压/频率效果未验收；[剩余功能](recovery-status.md)仍独立列出。

以下保留此前验证记录。

<!-- intel-oc-recovery-90e16fb -->
2026-10-10：最新验证生产代码为 `90e16fb`，分支 `refactor/platform-recovery`。
在 UMC 212 字段、快照文件和 AMD 拓扑基础上，新增 Raptor Lake-S core/cache 电压 offset：
分别选域、保持其它字段、检查锁和旧值、有限等待与写后回读。
功能边界与剩余工作统一见[还原状态](recovery-status.md)，实现见[Intel offset](intel-oc-recovery.md)。

- [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38009434518) 23/23，通过十目标；每目标 27 Qt / 31 Python，11 套目标内核 VM 启动。
- [独立核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38009434514) 4/4，各 6 CTest / 45 场景组。
- 20 份 DEB/RPM、源码包和校验表已归档至 `dist/intel-oc-recovery-90e16fb/`；[完整结果](validation/intel-oc-recovery-ci-90e16fb.json)含 200 张截图及源码/产物哈希。
- [AMD 六字段研究](amd-limits-recovery.md)另有 882 项本地原指令实验和 3 项回归；它们没有被算入上述生产代码的云端测试数。

原版全部功能仍未完成：PStates 设置、Intel server 电压域 / VF / fabric、AMD 完整 PBO/MP1 高层调参，
以及 Intel 训练时序与板级 PMIC/VRM/EC/时钟写入仍有缺口。四台目标机器尚无真机验收。

以下为此前记录；最新生产代码及验收结果以上述版本为准。


2026-10-10 平台恢复增量已接入，验证代码为 `0b514bb`（分支 `refactor/platform-recovery`）。
AMD PStates 增加完整 VID/Idd 原始字段；Intel Controls 增加 RAPL/HWP 读写和温度；
AMD 增加受限 BIOS SMUIO 与 CCD/core/MHz 命令准备；内存与主板页增加 DMI、hwmon、
驱动已暴露的 SPD、DDR4/DDR5 基础 CRC 和 SPD 时序解码。
**仍未完成原版全部功能**：PStates 设置/物理电压电流、Intel VF/逐核 turbo/fabric、
AMD 完整调参/拓扑与曲线、运行时内存时序及 PMIC/VRM/EC/板载时钟仍有缺口。
详细范围见[本轮功能表](platform-controls.md)，不能把测试通过当作四台目标机器的硬件验收。

- [Linux 完整验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068664) 23/23，通过十个发行版目标；每目标 21 Qt / 29 Python，11 套内核 VM 启动。
- [独立核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068831) 四环境全部通过，每环境 4 CTest / 26 场景组。
- [本轮证据与哈希](validation/platform-recovery-ci-0b514bb.json)；20 份 DEB/RPM 与源码包位于
  `dist/platform-recovery-0b514bb/`，参见[产物说明](artifacts.md)。

以下保留此前记录；最新功能和验证以本段及其链接为准。

最新重构提交 `beb10b7` 已把 PStates、寄存器请求/校验/串行服务和 Linux 硬件适配分离。
独立核心四环境通过，Linux 完整矩阵 23/23；EL8 基线与十目标各 13 项 Qt 测试通过，
每目标 26 项 Python 回归无跳过，见[本次源码、测试及产物哈希](validation/refactor-hardware-ci-beb10b7.json)。
当前主线是先完成功能重构，再移植重构版；Windows/macOS 此时只验证核心，尚无硬件后端/完整 GUI。
剩余系统信息、多步事务、其他平台面板和目标真机验收仍待继续。

以下保留此前研究与验证记录。

更新：2026-09-30。作者在第一阶段基础版实现后选择继续恢复平台面板。
作者随后指定源码父目录下 Z790、W790 ACE、W890E-SAGE SE、TRX50 SAGE 四组截图作为参考。
17 张图已全部查看并原样保存到 docs/references/platforms。截图没有 CPU/BIOS 信息，
作者随后用文字确认了下表；BIOS 版本仍未知。
本页把可确认的入口和未知规格分开记录，避免把聊天里的推测变成硬件实现。

## 作者确认的硬件

| 参考目录/主板 | CPU | BIOS | 确认范围 |
|---|---|---|---|
| Z790 | Intel Core i9-14900KS | 未知 | 尚未提供 Z790 主板完整厂商/型号 |
| W790 ACE | Intel Xeon w5-2565X，18 核 | 未知 | 作者明确更正先前的 w5-2575X 笔误 |
| W890E-SAGE SE | Intel Xeon 658X | 未知 | 作者原文为 658X |
| TRX50 SAGE | AMD Ryzen Threadripper PRO 9995WX | 未知 | 作者原文为 AMD TR 9995WX |

机器可读登记见 [platform-hardware.json](validation/platform-hardware.json)。此表是作者提供的
实机配置，不能替代程序运行时检测到的 CPUID family/model/stepping、在线 CPU 拓扑与 BIOS。
尤其不能把截图中的 Core/CCX 选项编号直接当成 Linux 逻辑 CPU 编号。
[Intel 产品规格](https://www.intel.com/content/www/us/en/products/sku/236883/intel-xeon-w52565x-processor-37-5m-cache-3-20-ghz/specifications.html)
确认 w5-2565X 为 18 核/36 线程；无需再向作者重复询问已纠正的型号。

## 已取得的面板证据

原 Linux ELF 和 Windows Tool.exe 都保留了下面 23 个 QObject 类的元对象。
完整字符串、文件偏移和二进制 SHA-256 见
[platform-panel-metadata.json](validation/platform-panel-metadata.json)，提取方法见
[GUI 恢复流程](gui-recovery.md)。这些是程序中的原始类名，不保证等于用户看到的菜单标题。

| 入口组（按原类名列出） | 两平台均有的类 | 可直接核对的槽/信号示例 |
|---|---|---|
| intel 入口 | intel_mainstream、intel_client、intel_hedt_window | big_loop、sortit、on_intelhedtskewratioapply_clicked |
| memtime / timings | intel_memtime、adl_timings、arl_memtime、gnr_memtime、nvl_memtime | adl_timings::refresh；部分类只有关闭槽或类名 |
| amd 入口 | amd_am3、amd_am4、amd_am5、amd_umc、am5_tuners | pm0_monitor、refresh |
| VF | amdvf、intel_vf_curve | tr5_get_vf、tr5_get_boost_curve、percore_core_loop、ring_loop |
| mb | tr5_mb、w790_mb、w890_mb | refresh、若干 on_pushButton_*_clicked |
| vrm | vrmread、vrmread_am5 | starter、vcore_obj_done、soc_obj_done、pm0_monitor |
| SPD / PMIC | ddr5_spd、pmic、pmic_amd | refresh、refresh_settings、refresh_ec_settings |

存在 refresh 或 pm0_monitor 只证明有这个元对象入口。它不能证明哪个 CPU 能用、读取什么地址、
返回值单位是什么、是否会触发写入，也不能据 class 名推定支持某个尚未核实的平台。
例如 gnr_memtime 的字符串表只有类名，并不代表整个类没有业务代码；普通 C++ 方法不必进入 Qt 元对象。

## 截图确认的界面

[截图清单](validation/platform-screenshots.json)记录原路径、源码内路径、字节数和 SHA-256。
目录名是作者提供的参考分组，不等于已经验证 CPU/主板/BIOS 的完整组合。

| 参考组 | 已直接看到的面板 | 明确可读的字段/单位 | 不能从截图推出的内容 |
|---|---|---|---|
| Z790（7 图） | Work Tool 菜单；Intel Controls | Voltage/OC Voltage/Offset 的 mv；8 项 Ratio Limit/Act Cores 与 Atom Ratio/Act Atom；Core0–23 行 | CPU 商品型号/stepping；Core 行与逻辑 CPU 映射；BCLK 单位和编码；PL1 输入单位 |
| W790 ACE（2 图） | Intel Controls 与电压域列表 | Voltage/OC Voltage 的 mv；VID Rank；Core0–17 行；Overall SP: 77 | 不能由 18 行猜测 CPU 型号；SP 算法/单位；Offset 的编码；BIOS |
| W890E-SAGE SE（2 图） | Intel Controls，Fabric 与逐核区 | CU0/IOD South/IOD North；Max/Min Fabric、VID；CPU BCLK/SOC BCLK；Core0–23；Overall SP: 104 | CU/IOD 索引对应关系；Fabric/VID/Max V/SP 的语义和编码；CPU/BIOS |
| TRX50 SAGE（6 图） | Work Tool 菜单；CPU Functions 的 Per CCX OC、PStates | VID/VID1 的 mv；PStates 列表 MHz、mv、A；CCX0–15 控件 | 不能把 16 个选项当成实际 CCX 数；PPT/TDC/EDC/THM/FMax/FIT 单位及 SMU 协议；CPU/BIOS |

Intel 的电压域必须按面板分别保存，不能套用同一列表：

- Z790 图：0 IA Core、1 GT、2 Ring、3 GT Media、4 System Agent、5 L2 Atom。
- W790 图：0 IA Core、1 GT、2 Ring、3 GT Media、4 VCCCFN、5 VCCIO、6 VCCMDFIA、
  7 VCCMDFI、8 VCCDDRD、9 VCCDDRA。
- W890 图：0 IA Core、1 GT、2 Ring、3 GT Media、4 VCC_CFN、5 VCCIO、6 VCCMDFIA、
  7 VCC_HDC、8 VCC_DDRD、9 VCC_INF。

TRX50 页签为 Per CCX OC、One Core Valid、Frequency Hot Key、Perf Bias、CCX Profiles、
PStates、SMUIO。PStates 截图显示 PState0 为 5000MHz / VID=306mv / Current Limit=31A，
PState1 为 2300MHz / 793mv / 23A，PState2 为 1500MHz / 856mv / 15A。
这些是旧程序当时的显示文本，尚未证明物理测量或算法正确；不能把 306mv 自行改成别的值，
也不能把它当作可推荐的写入参数。禁用 PState 的显示值同样不证明该电压正在施加。

四个详细界面原图：

- [Z790 Intel Controls](references/platforms/z790-controls.jpeg)
- [W790 Intel Controls](references/platforms/w790-controls.jpeg)
- [W890 Intel Controls](references/platforms/w890-controls.jpeg)
- [TRX50 Per CCX OC](references/platforms/trx50-per-ccx.jpeg)、[PStates](references/platforms/trx50-pstates.jpeg)

## 截图与原程序符号交叉检查

新增 [elf-ui-labels.py](../analysis/tools/elf-ui-labels.py)，用 Capstone 静态解码符号函数，
仅收集 RIP-relative LEA 取地址所指向的可打印字符串。排除 SIMD packed 数据加载，
不把字节数组中的可打印片段误认成 C 字符串；仍不声称每个引用就是某个特定控件的最终绑定。
结果见 [platform-ui-literals.json](validation/platform-ui-literals.json)，含每个函数地址、大小、
代码 SHA-256、引用指令地址和字面量位置；共 12 个函数、1064 个去重至单函数的引用。

- Ui_cpufunctions::retranslateUi 中明确有 `Frequency in MHz`、`VID in mv`、`Current in A`，
  可补全截图截断标签。这里只确认显示单位，没有据此推导 MSR/SMU 原始值转换公式。
- intel_ctl::构造/翻译函数与 Z790 图的 24 行/Offset mv/电压域有一致证据。
- intel_ctl2 和 intel_ctl3 都引用 W790 和对应电压域，不能只凭类名选择其中一个。
- intel_ctl5 构造函数直接引用 W890、VCC_CFN/VCC_HDC/VCC_INF、Voltage Rule、Water-Cooled Preset；
  与 W890 截图有对应证据，具体平台分派仍需核对调用路径与 CPU 型号。
- Linux 符号还保留 cpufunctions::refresh_pstates 和 AMD_Configuration::Get_PStates_Vec，
  可作为下一步检查旧算法的入口；当前尚未验证其硬件语义或正确性。

复现命令（可选依赖见 GUI 恢复文档）：

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/elf-ui-labels.py \
  /实际路径/octool \
  --pattern 'Ui_(intel_ctl[0-9]*|cpufunctions).*retranslateUi|_ZN(9intel_ctl|10intel_ctl[2356]|12cpufunctions)C2E.*(?<!cold)$' \
  --output build/input-audit/platform-ui-literals.json
```

## 继续实现需要补齐的输入

- 实机 CPUID family/model/stepping、在线 CPU 与 socket/core 对照；商品型号已确认如上。
- 主板完整型号、PCB revision（若已知）、BIOS 版本；无需猜测未知项目。
- 优先的 1–3 个原面板名称，以及实际常用的读/写操作。
- 面板字段的单位和含义，能够提供时附作者的寄存器定义/原算法/已知测试向量。

当前登记：作者已选择继续恢复并提供四组截图和 CPU 型号；Intel Controls / AMD CPU Functions 已有视觉依据。
BIOS、实际拓扑和未标明的硬件字段规则仍待核实，不能继续笼统地说“没有参考界面”或“CPU 型号未知”。
收到信息后将对应二进制符号/槽、作者解释和实现文件关联到下表，再实现具体功能。

| 需要记录的项目 | 内容要求 |
|---|---|
| 面板与控件 | 原菜单名、类名、字段/按钮名称；是否仅只读 |
| 平台选择 | 作者确认的 CPU/主板/BIOS；检测依据与不匹配时行为 |
| 访问方式 | MSR/MMIO/PCI 等，CPU/BDF/地址如何确定；只使用现有 HAL/96 字节 ABI |
| 原始布局 | 寄存器宽度、字节序、位域、符号扩展、有效/错误状态 |
| 显示转换 | 单位、缩放、偏移、取整、无效值；每项给出处或作者确认 |
| 写入行为 | 允许范围、保留位、锁/握手/超时、撤销/恢复方法；禁止自动回读的特殊寄存器 |
| 回归向量 | 原始字节→预期值、边界、错误、只读字段；测试来自规格，不能复制实现公式自证 |
| 真机结果 | 型号/BIOS、原始输出、对照结果、错误日志及确认人 |

没有规格的字段保持未实现，不填经验系数或看似合理的温度/电压值。
基础版的手工 MSR/MMIO/PCI 输入功能不等同于已恢复平台面板。

## 与当前构建链的关系

首个恢复的子页为 [AMD PStates 只读页](amd-pstates.md)：作者已明确选择只读频率和完整原始值。
Intel Controls、AMD Per CCX/SMU 等仍处于规格核对阶段，不把参考布局当成已恢复功能。

新面板继续加入 gui/，复用 HardwareAccess 和同一 HAL；不要让 Qt 界面直接新增任意 ioctl
或改变旧 MMIO 编码。先完成可核对的只读字段，再按作者确认的写入规则增加操作。
每次修改继续跑十目标真实 GUI 编译、Qt 回归、包安装/窗口门禁与原 loopback/transport/parity selftest。
真机寄存器访问和旧 GUI 对拍必须使用 [真机清单](hardware-acceptance.md)，容器不能替代。
