# GUI 源码恢复与重构范围

更新：2026-09-30。

最新：已从 E:/Download/Edge/OCTool0528.zip 读取 Windows 包，确认只有二进制。
作者选择先重构基础信息与 MSR/MMIO/PCI 原始读写，实现及新发现的传输问题见
[第一阶段](gui-phase1.md)。下文 F 盘不可读是此前历史，不再是当前阻塞。

## 作者的新输入

作者明确说明原 Linux GUI 源码已经丢失，要求先检查 Windows 版本，必要时重构。
因此后续工作不能继续把“必须找回原 Linux 源码”作为唯一选择。
允许恢复/重构 GUI 不改变以下要求：既有 96 字节请求、字段偏移、MMIO 操作码和 mmap 邮箱应答
继续兼容旧 GUI/旧 .ko；未知硬件字段、单位、寄存器位域和缩放必须问作者，不能猜测。

作者提供 Windows 包路径为 `F:\OpenAI\Codex\project\octool\OCTool0528.zip`。
本轮多次以完整路径读取、枚举项目目录，并在 Downloads 中按 OCTool 名称检索，均未取得该文件。
沙箱外再次查询完整路径及父目录，结果相同；父目录实际只有 octool-linux.zip 和 error.log 两个文件。
这只说明当前工具环境无法读取该路径，不代表 Windows 包本身损坏或不可恢复。
目前实际拿到的 `octool-linux.zip` 仍只有预编译产物。

## 已有证据和边界

[原始分析](../analysis/docs/linux-porting.md)记载曾分析 Windows Tool.exe、Tool_Win7.exe、
Qt5.12.12 运行库和应用 QObject 类，指出它比 Linux 版多出平台/自动化/厂商 SDK 功能。
这份文档是用户提供的历史证据；当前未重新读取 Windows 包，不把这些记录冒充新实测。
Windows SDK、驱动和 Qt DLL 不能直接链接进 Linux 构建。

## 文件可读后的处理顺序

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
成功完成；SDK 和配置证据见 [qt-sdk.md](qt-sdk.md)。下一步仍需可读取的 Windows 包来建立实际功能清单。
