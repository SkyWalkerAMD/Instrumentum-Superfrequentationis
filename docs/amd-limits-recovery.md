# AMD 六字段限制设置：完整槽执行

对固定 ELF 的 `cpufunctions::on_per_ccx_oc_apply_4_clicked`（0x7502d0，2196 字节）完成
882 组合成执行。源码 SHA 与前述 UMC/PStates 研究相同。
[原函数静态证据](validation/legacy-amd-limits-static.json)、[原字节 fixture](../analysis/fixtures/legacy-amd-limits.json)、
[本地完整观测](validation/legacy-amd-limits-local.json)和[可复现工具](../analysis/tools/legacy-amd-limits.py)分别保留。

这是槽体原指令实际执行，Qt 文本/解析和两个固件入口为显式替身；没有真实 Qt、CPU、PCI 或固件操作。
输入标签现已通过原 setupUi / retranslateUi 的布局调用贯通，仍未把它们升级为目标 9995WX 的 W/A/摄氏度规范。

## 恢复的路径

本槽使用 `this+0x1008` 的 Ui 对象，读取六个成员；每个非空项都执行十进制 `toUInt(ok=null)`。
`M` 表示 `this+0xf88` 或 GLOBAL_IS_GPT 非零，`S` 表示 `this+0xf89` 非零。
`S` 的构造来源已确认：GLOBAL_IS_GRANITE 非零，或原查找函数匹配 `1022:14d8` / `1022:14a4`。
`1022:153a`（Shimada 入口）和 `1022:14b5` 单独不会设置它。

| Ui 偏移 / 原标签 | 原参数变换 | M 为真 | M 为假、S=0 | M 为假、S非零 |
|---|---|---|---|---|
| 0x158 / PPT | 32 位乘 1000 | MP1 31/32/33/34 | MP1 53 | MP1 56 |
| 0x170 / TDC | 32 位乘 1000 | MP1 39/3a | MP1 54 | MP1 57 |
| 0x188 / EDC | 32 位乘 1000 | MP1 3a/3b | MP1 55 | MP1 58 |
| 0x1a0 / THM | 原值 | MP1 37/38 | MP1 56 | MP1 59 |
| 0x1b8 / FMax | 原值 | 不发送 | MP1 6d | MP1 70 |
| 0x1d0 / FIT | 原值 | 按 S 决定 | BIOS smu_cmd2 / FIT 表项 | MP1 5b |

消息编号为十六进制。最后一行无论 M 如何都由 S 决定；实验使用已有 Shimada 初始化表令 FIT 表项为 2f。
这不把上面其它 MP1 消息转换成 BIOS SMUIO 的 3c/3d/3e 命令。

## 不应移植的原行为

- 前三个输入没有乘法溢出检查，4294968 乘 1000 会截为 704，随后继续提交。
- M 分支中第二、第三输入连续使用同一个 3a 消息；输入分别为 10 和 20 时，原代码先提交 10000，后提交 20000。
  消息的具体硬件作用仍需确认，不能仅凭标签排除覆盖。
- 全部输入为空也显示 Applied；固件替身返回 0、fe、ff 或 ffffffff 时仍然继续剩余项并显示 Applied。
- 0x1b8 输入在 M 分支中被读取、转换后丢弃，没有提交对应请求。

因此，“所有限制乘 1000 后交给已有 BIOS 通道”没有原调用路径依据。
已完成的 BIOS SMUIO 读取/消息机制继续保持原编码接口；PBO/MP1 的隐含 `0x50200=1` 写入、
各固件参数语义、返回/回读契约仍须贯通后，才能提供对应高层调参。

## 标签与第二标志的新证据

[新实验](../analysis/tools/legacy-amd-limit-bindings.py)执行三个原函数中的四段原始区域：
六行控件创建/命名/同布局添加、六个翻译标签，以及平台标志的主路径与分支。
确认标签与输入处于同一个 QHBoxLayout 后才建立映射，未用“相邻成员偏移”猜测标签。
另执行 80 组厂商/设备/Granite 标志/查找失败组合；14b5 分支只保留另一个参数为 5，不设置 S。
构造片段以先前清零指令为前提，不代表执行了整个构造函数或中间监控代码。

[固定字节](../analysis/fixtures/legacy-amd-limit-bindings.json)、[静态范围](validation/legacy-amd-limit-bindings-static.json)、
[六行绑定与 80 组观测](validation/legacy-amd-limit-bindings-local.json)可单独核对。
Qt 和 PCI 查找是明确替身；没有原 Qt、设备访问或固件写入。

882 项覆盖八种分支组合、64 种空输入组合、六字段的零/边界/回绕值、固件失败返回和非布尔标志。
另有三项关键原行为回归随 `analysis/tests` 自动发现。当前记录为 Windows 本机运行；
不混入 Intel OC 生产提交 `90e16fb` 的 45 个核心场景组或 Linux 安装包通过数字。

```sh
PYTHONPATH=build/reference-tools python3 analysis/tools/legacy-amd-limits.py --output build/amd-limits.json
PYTHONPATH=build/reference-tools python3 -m unittest discover -s analysis/tests -p test_amd_limits.py -v
```
