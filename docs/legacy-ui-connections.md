# NVL Controls 的控件、文字与信号连接

日期：2026-10-08；原 ELF SHA `44598dc8…aff10`。承接[Qt方法入口](legacy-qt-callbacks.md)及
[MSR失败链](legacy-msr-failures.md)。作者选择继续离线分析；没有访问四台真机或在云端传入设备。

## 已恢复的证据链

此前607项只确认Qt元方法索引与函数入口，不证明按钮指向哪个槽。本次用三份独立原字节交叉连接：

1. `Ui_intel_ctl_left2::setupUi` 的157个成员命名片段，直接执行成员存储→QString→setObjectName。
2. 完整 `retranslateUi`（`0x620540`，7,555字节）的67次标题/按钮/标签赋值，追踪实际widget指针。
3. `intel_ctl6` 构造内 `0x603f55..0x60451d` 的连接区域；Qt元数据及原metacall再确认目标入口。

构造的对象链为 `this+0x30 → intel_ctl_left2；left+0x30 → Ui_intel_ctl_left2`。
模拟从明确写出的局部前置条件开始，**不执行构造前半部分**，也不假装已经初始化了硬件。
setupUi共有158次setObjectName；其中父widget的条件命名（`0x62234a`）不在157成员片段内。

| Ui成员偏移 | objectName | 原英文文字 | 显式connect调用点 | 槽入口 |
|---|---|---|---|---|
| `0xb0` | `pushButton_21` | Apply GT | `0x6044d9` | `gt_clicked / 0x614f90` |
| `0x370` | `pushButton_18` | Apply NPU | `0x604510` | `npu_clicked / 0x6152e0` |
| `0x4c8` | `pushButton_17` | Apply NGU | `0x6044a2` | `ngu_clicked / 0x614cc0` |
| `0x460` | `xoc` | XOC | `0x6042a9` | `on_pushButton_14_clicked / 0x5fb060` |
| **同一`0x460`** | **同一`xoc`** | **同一XOC** | `0x604434` | `on_xoc_clicked / 0x5f8c40` |

这两条GT/NPU连接把原标签、对象成员、Qt方法及[MSR故障实验](legacy-msr-failures.md)接起来；
仍不把标签当作命令硬件定义，也不证明特定机器上整个构造已成功、按钮可见/可点击。

## 同一个XOC按钮有两条连接

两个连接的sender是同一Ui成员，signal均为原字串`2clicked()`，receiver均为intel_ctl6本身，
类型参数均为0；slot分别为上表两个方法。每个槽都分配`0x370`字节，以null parent调用
`nvl_xoc`构造入口`0x672b70`，之后到达show边界。第一个槽另设置属性数值`0x4c`和`0x37`为1；
本报告保留数值，不依靠记忆给属性起名。

因此这是两条各自创建窗口的显式连接，不能简单当作符号别名或同一个函数重复列出。
Qt5.15.2的QObject源注释说明，多槽连接按建立顺序激活；据此推论，**如果这些连接成功且未被
后续断开，一次点击会触发两个创建流程**。这仍是条件推论，没有运行真实信号/两个XOC窗口。
[Qt5.15.2源码中的连接说明](https://sources.debian.org/src/qtbase-opensource-src-gles/5.15.2%2Bdfsg-4/src/corelib/kernel/qobject.cpp/)

原连接区域有28个connect调用点；`this+0x78`为空时跳过`synch_cu1_v`，实际27个。
GT/NPU/XOC这四条在该局部分支两种输入下都执行。其他动态创建widget没有在本次恢复文字的，
报告保留`intel_ctl6 member+offset`，不把相邻字符串强行分配给它。

## 实验范围与复现

公开fixture含15,111原指令字节，SHA
`21863e0ef92b04b9a6f021fa2c993e597908560788810439629c703ad8203222`。
三个完整函数（retranslateUi、两个XOC槽）及158个局部片段，均核验母函数SHA与边界。
所有Qt调用都被截获；translate使用原英文source text，**不模拟真实语言包**。
QString合成引用计数-1及1分别验证保留/67次释放路径，两者控件文字观测一致。
XOC构造、show、属性调用仅记录参数，原窗口构造体不执行。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/audit-legacy-ui-connections.py \
  build/input-audit/octool --output docs/validation/legacy-ui-connections-analysis.json \
  --export-fixture analysis/fixtures/legacy-ui-connections.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/emulate-legacy-ui-connections.py \
  --output build/ui-connections.json
```

[静态片段与母函数证据](validation/legacy-ui-connections-analysis.json)、
[Windows163项观测](validation/legacy-ui-connections-windows.json)包含157命名、2翻译所有权路径、
2可选连接路径及2个XOC槽。连接表另通过原Qt路由逐项核对，不计入163个UI实验数。
未知执行边界、改变的fixture、模板不匹配均失败，不自动回退为成功。

这些证据没有执行全部setupUi、自动连接、完整控件生命周期、硬件初始化及真实鼠标事件。
它们用于恢复绑定规范、标出旧行为，不能据此宣称原GUI在EL8–EL10已完成主窗口验收。
