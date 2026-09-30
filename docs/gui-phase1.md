# GUI 重构第一阶段：基础信息和原始寄存器

更新：2026-09-30。作者在收到 Windows 包检查结果后，选择先重构基础信息、MSR/MMIO/PCI
原始读写，再恢复平台面板。这个阶段有实际硬件访问实现，不是用于绕过 CI 的示例窗口；
其成功也不代表原 OCTool 的所有监控、时序、供电和自动调参面板已经恢复。

## 输入与实现依据

现已读取 `E:/Download/Edge/OCTool0528.zip`，SHA-256 为
`02a50088a94000f651f8783b32bf1ff63a48fd4e6a3909983c7c1ed0a5a417a3`，158 项。
没有 `.pro`、`.pri`、C/C++、`.ui`、`.qrc`、PDB 或 Visual Studio 工程。
Tool.exe / Tool_Win7.exe 都是 x64 PE，COFF 符号计数 0，debug 目录只有类型 13，没有 CodeView/PDB。
包内 license.txt 是 GIMPS/Prime95 说明，不能当作 OCTool 源码许可证。
新位置 Linux ZIP 与此前包 SHA-256 相同，仍只有 ELF/模块/运行库。

只进行静态解析，没有运行 Windows EXE、驱动、BIOS 或旧 Linux 安装脚本。
Linux ELF 保留符号和 Qt 元对象；`MainWindow` 有 RW_MSR、RW_Memory、RW_PCI 菜单槽，
两平台均有 `rw_msr`、`rw_memory`、`rw_pci` 字符串表。这些证明界面入口存在，
不能恢复原始 C++ 语句，也不能证明未知寄存器的单位、含义或写入范围。

新代码位于 gui/，沿用 port/hal 和 canonical ABI，首批只使用 QtBase 的 Widgets/Concurrent。
新增 GUI 文件沿用已有 HAL 的 GPL-2.0 许可；gui/LICENSE 保存 GPLv2 正文。
这不改变旧 Windows/Linux 二进制或其捆绑第三方组件的许可。
QtBase 的 GPLv2 选项见其
[QApplication 源文件许可头](https://github.com/qt/qtbase/blob/v5.15.18-lts-lgpl/src/widgets/kernel/qapplication.cpp)。
尚未链接 QtCharts 或 Windows SDK。

## 已实现范围

- 基础页：/proc/cpuinfo 的 model name 原文、sysconf 在线逻辑 CPU 数、系统/内核/架构/版本。
- MSR：十进制逻辑 CPU、32 位十六进制寄存器号、完整 64 位值。
- MMIO：64 位物理地址，8/16/32/64 位宽度，沿用原 0x0a–0x11 命令及 mmap 邮箱。
- PCI：0000 域、bus/device/function、首 256 字节配置空间，8/16/32 位宽度。
  原模块通过 CF8/CFC，不能把扩展配置空间 0x100–0xfff 默默截断后访问。
- 严格解析无符号整数；拒绝负数、溢出、宽度不匹配、未对齐地址和越界 BDF。
- 启动时不扫描 MSR/MMIO/PCI，不自动写入或周期读寄存器。每次写入显示具体目标/值，
  默认取消；提交成功不冒充回读验证。读结果随输入变化清空，失败不显示伪造零值。
- QtConcurrent 后台任务与单 HAL 互斥，窗口关闭后由任务保有访问对象，避免悬空引用。
- 各页对象名保留 rw_msr/rw_memory/rw_pci，主窗口为 MainWindow；没有声称像素级恢复旧 UI。

## 接入时发现并修复的基础缺陷

`hwio_rdmsr` 等把 CPU 放到 user_id，但真实 mod_submit 无条件改为 per-open token。
旧 loopback 注入点在 mod_submit 之前，所以不能发现这个错误。
修复只让非 CPU 命令回填令牌；MSR、CPUID、TSC 保留调用者 CPU。MMIO 的令牌和全部字段不变。
模块的 CPU 命令原来对无效/offline CPU 回退到当前 CPU；现返回 EINVAL，避免写错 CPU。
该改动仅作用于本重构新增的非 MMIO 命令，不修改旧 GUI 使用的 MMIO 分派。

另一问题是短 read/write 且 errno=0 时被当成成功。设备 write、直接 MSR/PCI pread/pwrite
现在对短传输返回 EIO，负返回保留原 errno，关闭 fd 前保存错误。
HAL 头和 ABI 头仍逐字节相同；HAL C 不再宣称与输入包相同，新基线 SHA-256：
`ba115e258a3d2d346902a9da70c8139424fb5c603dd4a6c9f76a7d38f2ef6ea9`。
validate-local.py 固定该已审阅实现的哈希，历史证据中的原 HAL 哈希保持原义。

## 验证门禁

`make -C port/tests check` 增加 transport：通过链接器 syscall wrap 执行真实设备传输代码，
验证 CPU 3/0/5/7 不被令牌 71 覆盖、MMIO 仍回填令牌、错误码和短传输失败。不访问真设备。
原 loopback 和 parity selftest 继续保留。

每次 build_gui.py 在真实目标 Qt 上额外编译并运行 gui/tests/regression.pro，使用 offscreen
插件测试界面输入、异步读取、失败清空、取消写入零请求、确认写入恰好一个请求。
该测试使用明确的内存 transport；正常 GUI 和 X11/Xwayland 窗口冒烟不使用假硬件。
编译时测试失败就停止打包；正式窗口仍须有可见窗口、正确进程 PID 并保持 5 秒。

EL8 Qt SDK 添加构建脚本/依赖脚本哈希缓存，每次恢复先核对归档 SHA-256。
缓存缺失重新编译；GUI 总是从当前源码构建，没有跳过 GUI、包或窗口门禁。
首轮 Linux GUI 构建结果尚待 Actions 实测，不能把本页的实现清单当成已通过的验收结果。

## 后续范围

尚未恢复 Intel/AMD 传感器解码、温度/电压/频率换算、平台供电/时序/曲线、压力测试、自动调参。
后续需要作者给出 CPU/主板/BIOS 与优先面板；不根据数值相近或类名推断硬件语义。
实际 MSR/MMIO/PCI 读写、MOK 加载和旧模块 live 对拍继续按 [真机清单](hardware-acceptance.md)验收。
