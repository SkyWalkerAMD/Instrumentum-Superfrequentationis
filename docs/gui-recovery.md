# GUI 源码恢复与重构范围

更新：2026-09-30。

已从 E:/Download/Edge/OCTool0528.zip 读取 Windows 包，确认只有二进制。
作者选择先重构基础信息与 MSR/MMIO/PCI 原始读写；实际工程、传输修复与 Linux 验证范围见
[第一阶段](gui-phase1.md)。原来 F 盘不可读的问题已解除，不能继续把输入文件缺失列为阻塞。

## 作者的新输入

作者明确说明原 Linux GUI 源码已经丢失，要求先检查 Windows 版本，必要时重构。
因此后续工作不能继续把“必须找回原 Linux 源码”作为唯一选择。
允许恢复/重构 GUI 不改变以下要求：既有 96 字节请求、字段偏移、MMIO 操作码和 mmap 邮箱应答
继续兼容旧 GUI/旧 .ko；未知硬件字段、单位、寄存器位域和缩放必须问作者，不能猜测。

此前作者提供的 F 盘 Windows 包路径不可读，沙箱外查询也相同。这是历史环境结果，
不是文件损坏的结论。新提供的 E 盘 Windows/Linux 两包均已实际读取，记录如下。

## 新取得的证据和边界

[原始分析](../analysis/docs/linux-porting.md)保留输入包历史。此次重新取得 ZIP 文件清单、
PE/ELF 元数据及 Qt 字符串表，见 [机器可读证据](validation/reference-packages-20260930.json)。

| 样本 | 本次静态检查 |
|---|---|
| Windows ZIP | 79,363,256 字节、158 项；无 C/C++、qmake/CMake、UI、资源工程或 PDB |
| Tool.exe | 72,948,688 字节，x64 PE，COFF 符号数 0，debug 目录仅类型 13；358 个验证后的 Qt 元对象字符串表 |
| Tool_Win7.exe | 15,836,112 字节，x64 PE，同样无 PDB；298 个 Qt 元对象字符串表 |
| Linux ZIP | 61 项，SHA-256 与原输入相同；只有运行二进制/模块/库 |
| Linux octool ELF | 保留符号；724 个候选 Qt 字符串表符号中解析出 720 个有效表，包括 Qt 内部类 |
| 交集 | Linux 与 Tool.exe 有 243 个共同类名，包含 MainWindow、rw_msr、rw_memory、rw_pci |

这些统计包括 Qt 类，不能当作“业务面板数量”。旧分析使用的 RTTI 类清单与此处元对象清单
方法不同，不直接比较数量。Windows 主程序导入的 opencv_world4140.dll 不在这个 ZIP 中，
因此没有尝试把“原 EXE 启动”当作成功证据。没有执行 EXE、驱动、BIOS 或旧安装脚本。
Windows 的 SDK、驱动和 Qt DLL 不能直接链接进 Linux 构建。

Qt 元对象提供类名、信号/槽等功能线索，不能恢复原 C++ 控制流、寄存器语义、单位或写入范围。
包内 license.txt 是 GIMPS/Prime95 文本，不能用作 OCTool 源码许可。

## 可复现的静态分析

工具为 [qt-meta-inventory.py](../analysis/tools/qt-meta-inventory.py)，只读取文件，不装载输入代码。
可选依赖隔离安装到项目 build/；不是 GUI 或 CI 的运行时依赖：

```sh
python3 -m pip install --target build/reference-tools -r analysis/tools/requirements-reference.txt
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/qt-meta-inventory.py \
  /实际路径/Tool.exe --output build/input-audit/windows-qt-meta.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/qt-meta-inventory.py \
  /实际路径/octool --output build/input-audit/linux-qt-meta.json
```

ELF 从 qt_meta_stringdata 符号定位，PE 扫描 64 位 QByteArrayData 并校验相对偏移、长度和 UTF-8，
再用 DIR64 重定位中 QMetaObject 的 stringdata/data 指针和 revision 7/8 描述符交叉验证。
PE 表位于实际 .data，不能只搜 .rdata。工具输出二进制 SHA-256、表偏移、类名和完整元数据字符串。
布局参考 [Qt MOC generator.cpp](https://github.com/qt/qtbase/blob/v5.15.18-lts-lgpl/src/tools/moc/generator.cpp)。
原 ZIP/EXE/ELF 放在被忽略的 build/ 外部输入目录，源码仓库仅保存工具和文本证据。

## 恢复原则与后续顺序

1. 记录原 ZIP 的 SHA-256、文件清单，查找 `.pro`、CMake、C++、`.ui`、`.qrc`、调试符号和许可证。
   压缩包在私有 build/ 目录内检查，不直接运行附带安装器/驱动，也不把运行时包整体提交 Git。
2. 有工程源码则优先恢复现有调用点和界面。只有二进制时，Qt 元对象、资源、导入表和符号能帮助
   建立功能清单，但不能等价恢复 C++ 原始实现；每项结果标注证据与未知部分。
3. 根据实际界面和代码证据确定可恢复功能。硬件字段/单位/范围不明确的部分列为作者待确认，
   不能仅靠数值相近、第三方同名面板或 Windows SDK 名称推断。
4. 必须重构时，以现有 ABI/HAL 和对拍门禁为基础；明确哪些功能已恢复，哪些缺少规格或 SDK。
   不用只有外壳的 Qt 示例程序冒充完整 OCTool，通过窗口冒烟也不等于超频功能通过。
5. 将实际 qmake 工程、资源、Qt 模块、许可与标题接入 `port/gui/build.json`，在 EL8 构建，
   再运行十目标原生重编、rpm/deb、X11/Wayland/Xwayland 窗口测试及真机对拍清单。

## 可独立继续的工作

内核和 DKMS 包矩阵已取得真实十目标成功证据，见 [verification-status.md](verification-status.md)。
手动 `Qt SDK diagnostic` 使用同一 EL8 Qt 构建脚本验证静态工具链，避免 GUI 恢复期间完全停工。
它不依赖虚假的 GUI 源文件、不豁免正式 baseline/desktop 门禁，也不证明现有 GUI 功能已恢复。
该诊断已在 [run 36661824443](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36661824443)
成功完成；SDK 和配置证据见 [qt-sdk.md](qt-sdk.md)。当前正式流程已开始实测基础版 GUI。
后续平台面板需要作者提供 CPU/主板/BIOS 和优先面板；硬件字段不确定时先确认再实现。

## 重构增量：独立核心与 Linux 适配器（2026-10-09）

目标是先重构 OCTool 的功能与模块边界，再把重构版移植到其他系统。
本轮把已有 AMD PStates 的 CPU 能力判断、频率解码、一次性快照，以及寄存器请求、
校验和串行访问抽到 `gui/core`；核心仅使用 C++11 标准库，不依赖 Qt 或 Linux HAL。

- `core/HardwareBackend` 定义 MSR/MMIO/PCI 请求、CPUID 和后端诊断。
  `HardwareService` 持有后端并串行所有调用；无效地址空间、宽度、地址、PCI 字段和越界写值在分发前拒绝。
  保留原错误码；失败的寄存器值和 CPUID 输出清零，成功写入只回显提交值，不自动回读。
- `platform/linux_hwio.cpp` 负责现有 HAL、96 字节协议的调用及直接 CPUID 的线程亲和性恢复。
  HAL 与内核协议源码不变；Qt 层仅作输入解析、状态文本和异步控件接入。
- 新 OS 需实现 `HardwareBackend` 和构建时选择的工厂，提供目标逻辑 CPU 语义与错误映射。
  当前只实现 Linux 硬件适配器。独立核心在其他系统编译不代表对应驱动或完整 GUI 已移植。
- 锁的范围仍是单笔操作。NVL 等共享 mailbox 的多步事务、其余平台面板和真机验收仍待后续重构。
  基本信息页的 `/proc/cpuinfo` 和 `sysconf` 也仍属后续系统信息接口范围。

独立编译与回归（不需要 Qt、驱动或硬件）：

```sh
cmake -S gui/core -B build/portable-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/portable-core --config Release
ctest --test-dir build/portable-core -C Release --output-on-failure
```

新增 `portable core` Actions 工作流分别使用 Ubuntu 22.04/24.04、Windows MSVC 和 macOS Clang，
以 warnings-as-errors 编译，Release 测试强制保留断言。两组测试覆盖 PStates 的 7 个场景组，
以及硬件接口的 6 个场景组（含错误输出、所有权和 6 线程混合访问）。
既有 `portability` 工作流继续验证 Linux HAL、Qt 控件、静态 Qt 构建和十发行版包装/窗口。
本地缺工具链后已使用 GitHub Actions：`beb10b7` 的四环境核心与 Linux 23/23 矩阵全部通过，
EL8 基线及十目标各 13 项 Qt 回归通过。见[本次完整记录与产物哈希](validation/refactor-hardware-ci-beb10b7.json)。
