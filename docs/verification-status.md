# 验证状态与输入证据

更新：2026-09-30。源码、构建配置、容器测试和真实硬件验收分别记录，不能互相替代。

## 当前结果

原 GUI 源码丢失后，作者已授权重构，并先选择基础信息与 MSR/MMIO/PCI，随后提供四套平台截图。
可编译的真实 Qt5 GUI 已接入。作者进一步选择先恢复 AMD PStates 的只读频率和完整原始值。

最近完成的 [run 36666985989](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36666985989)
（18c18c5）通过 EL8 baseline、十个 kernel 和九个 desktop job。EL10 fresh runtime 因软件源没有
xwininfo 而失败，总门禁为红；[结果摘要](validation/actions-run-36666985989.json)保留每项结论。
修复使用独立 Xlib 窗口检查器，81ea801 的完整矩阵正在运行；新增 PStates 页尚待 Linux Qt 回归。
本文在取得最终结果后更新，不提前声称全绿。

## 验证范围

| 层次 | 已取得的证据 | 尚不能推出的结论 |
|---|---|---|
| EL8 baseline | 静态 Qt5.15.18、实际 GUI/Qt 回归编译、ELF ABI 检查通过 | 原 GUI 全部面板恢复 |
| 十个 kernel job | HAL、loopback/transport/parity selftest、17 项 Python 测试、Kbuild、签名试验、DKMS 安装/同版本重装/卸载再装 | 真正加载模块、真实 MMIO 等价 |
| 九个 desktop job | 各发行版原生 GUI 编译/Qt 回归、GUI+DKMS rpm/deb、新容器安装、发行和 native 两个真实窗口 | EL10 窗口与全矩阵完成 |
| Ubuntu26 GUI 视觉检查 | 下载并查看四页真实 Xwayland 截图，表单未裁切 | 真实硬件功能/电压/超频有效 |
| 新 AMD PStates | 已实现且接入工程，规格/静态检查完成 | 尚未执行的新 Qt 回归和 9995WX 实机通过 |
| MOK/live 对拍 | 有可执行脚本、签名辅助与详细清单 | 固件登记、模块加载、真实读写尚未运行 |

当前 GUI ELF 最高需求 GLIBC_2.28、GLIBCXX_3.4.15、CXXABI_1.3.9；没有动态 Qt/ICU/libjpeg/Wayland
客户端依赖。EL8 新 Qt SDK tar SHA-256：
`7009c060b85b329202028228fd64faddf643c9ed4ea69bef211abdf5cb54e85e`。
SDK 仅按匹配构建配方与 SHA 复用，GUI 每次重编。具体依赖策略见 [多发行版指南](multi-distro.md)。

## 十目标状态

以下内核 release 由已归档的成功 kernel 矩阵确认；每轮的包/镜像细节以对应 run artifact 为准。
GUI/装包列是 18c18c5 的结果，不将此前模块单独成功冒充 GUI 成功。

| 目标 | 已实际编译的内核 release | 原生 GUI 编译 | GUI+DKMS 安装/窗口 |
|---|---|---|---|
| EL8（Rocky） | 4.18.0-553.168.1.el8_10.x86_64 | 通过 | 通过 |
| EL9（Rocky） | 5.14.0-687.52.1.el9_8.x86_64 | 通过 | 通过 |
| EL10（Rocky） | 6.12.0-211.60.1.el10_2.x86_64 | 通过 | 测试依赖安装失败，待重跑 |
| Ubuntu20.04 | 5.4.0-216-generic | 通过 | 通过 |
| Ubuntu22.04 | 5.15.0-194-generic、6.8.0-138-generic | 通过 | 通过 |
| Ubuntu24.04 | 6.8.0-142-generic | 通过 | 通过 |
| Ubuntu26.04 | 7.0.0-34-generic | 通过 | Xwayland 通过 |
| Debian11 | 5.10.0-46-amd64 | 通过 | 通过 |
| Debian12 | 6.1.0-53-amd64 | 通过 | 通过 |
| Debian13 | 6.12.111+deb13-amd64 | 通过 | 通过 |

Rocky 结果不是 Alma/RHEL 的单独实测记录。RHEL 需要已订阅的软件源；三者共用 EL 构建入口，
分别验收要求见 [真机清单](hardware-acceptance.md)。容器使用目标内核头文件实编，但实际进程仍
运行在 GitHub runner 宿主内核上，不能把 GUI Information 页的宿主内核当作已启动目标内核。

## 输入包与已确认硬件

| 输入 | SHA-256 | 内容/检查方式 |
|---|---|---|
| octool-linux-refactor.tar.gz | `43c8639802ddec1aae2d68eaae16b54655ddbb2738af6849ed3329fe29e342ac` | 原 port/ 与 analysis/，沿其扩展 |
| octool-linux.zip | `02fd2ae0534c86aed23cb5470fb2520b0772f4421c6ed1c966665d171313b53b` | 原 ELF、旧模块和运行库；静态解析 |
| OCTool0528.zip | `02a50088a94000f651f8783b32bf1ff63a48fd4e6a3909983c7c1ed0a5a417a3` | 158 项；无 C++/qmake/UI/PDB；PE 静态解析 |

两个 ZIP 已从作者新提供的 E:/Download/Edge 路径读取，不再处于“文件找不到”状态。
没有执行旧安装脚本、Windows EXE、驱动或 BIOS。完整清单见
[reference-packages-20260930.json](validation/reference-packages-20260930.json)。

17 张参考图已逐张查看并原样保存。四组硬件由作者文字确认：i9-14900KS、
w5-2565X（18 核，已更正笔误）、Xeon 658X、Threadripper PRO 9995WX；BIOS 未知。
见 [平台记录](platform-recovery.md)及[只读 PStates 依据](amd-pstates.md)。

## 协议与本地检查

- canonical ABI 与输入包相同：`5f07bfefd2a96519756cad1567dfdf4b0c878db142f670e1b208cf39523800d8`。
- HAL 头未变：`d80a19e0e0551ba162e55653bde7975c50b74a1e811a08f6776071af43762dc1`。
- HAL C 修复 CPU user_id 被令牌覆盖和短传输误报，新哈希
  `ba115e258a3d2d346902a9da70c8139424fb5c603dd4a6c9f76a7d38f2ef6ea9`；不再声称与输入包相同。
  MMIO 命令和全部字段不变。详细影响见 [GUI 接入说明](gui-phase1.md)。
- Windows 本地：Python 16 项通过、1 项依赖 Linux parity 二进制的测试跳过；Linux job 跑完整 17 项。
  文档相对链接、Python3.8 语法、源归档一致性/命名/执行位/排除私钥和产物、Bash 语法已检查。
- 旧 GUI 实际 ELF 需要 GLIBC2.35、GLIBCXX3.4.29、ICU70、libjpeg.so.8，按门禁拒绝作为 EL8 发行物；
  [旧 ABI 报告](validation/original-binary-abi.json)保留这一历史诊断。

## 历史证据和交接

输入包原有 11 内核、glibc2.31–2.43 等记录保留在 [原开发日志](../port/CHANGELOG.md)，
不重新推导或冒充本轮实测。[第六轮模块证据](validation/actions-run-36660297759.json)记录全部
DKMS 事务通过；[初次 SDK 记录](qt-sdk.md)保留旧归档哈希，不与当前 SDK 混用。
[Actions 修复日志](actions-debugging.md)说明每个失败、修复及验证范围。

新 GUI 仍是分阶段恢复。完整 Intel/AMD 控制面板、MOK 实际登记加载、真实寄存器操作与旧模块
live 对拍都须继续按 [真机清单](hardware-acceptance.md)留证。没有相应证据就标未完成。
