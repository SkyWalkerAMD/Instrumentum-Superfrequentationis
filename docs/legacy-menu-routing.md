# 原主板与时序菜单的实际分派

日期：2026-10-08。输入原Linux ELF SHA `44598dc8…aff10`；本文是继续离线逆向的结果。
没有DMI/PCI真机采集，不据CPU型号断言用户四台机器已走过哪条分支。

## W790 / W890：菜单标题不参与识别

原 `MainWindow::on_actionW790_MB_triggered`（`0x8f0120`，671字节）行为为：

```text
if check_if_amd(): warning("Not Supported!")
else if GLOBAL_IS_GNR_SP: create(w890_mb); create(w890_vrm_module)
else: create(w790_mb); create(w790_vrm_module)
```

函数不读取DMI，也不读取CPU营销型号；非AMD分支甚至没有再次要求PCI vendor=Intel。
模拟vendor=`0xffff`仍按缓存标志选择面板，这是槽体行为，不是声称未知硬件受支持。
`GLOBAL_IS_GNR_SP`此前已追到原PCI列表`8086:3258`计数大于1的谓词，详见
[平台判断](legacy-platform-dispatch.md)。本次保持它为显式合成缓存输入，没有重复执行启动构造。

W790 ACE / w5-2565X与W890E-SAGE SE / 658X的作者身份保留，但不能用该身份代替这个标志的实机值。
每条正常分支调用两个构造器并各show一次；模拟仅记录这些调用，不执行主板/VRM面板体。

## TRX50 / WRX90：先匹配主板名

原 `on_actionAM5_MB_2_triggered`（`0x8ee5f0`，1,044字节）先通过`check_if_amd`，
然后取`getmobo()`，用**区分大小写的子串查找**，顺序如下：

| 条件，按先后顺序 | 调用的面板构造器 |
|---|---|
| 非AMD谓词 | Not Supported提示；没有面板 |
| DMI产品名含`WRX90E`，否则再查`TRX50` | 只有`tr5_mb2` |
| 上述均不匹配，且`is_tr5_es()`或缓存`GLOBAL_IS_SHIMADA`为真 | `tr5_mb`及两个`am5_vrm_module` |
| 上述均不匹配，产品名同时含`850`、`AYW`、`OC` | `am5_mb4` |
| 其余AMD输入 | `am5_mb3` |

`is_tr5_es`（`0x21e210`，36字节）实际调用`FindPciDeviceById2(0x1022,0x14a4,0)`，
返回值不等于-1即真。函数名中的ES不是本报告额外确认的CPU工程样品语义。
`is_it_ayw_oc`（`0x3b22a0`，193字节）依次查找`850`、`AYW`、`OC`，没有要求相邻或固定顺序。
第三个原字符串只有两个字节；本次从引用地址直接读出`OC`，未依赖早期“至少3字符”的清单。
本次菜单实验中`GLOBAL_IS_SHIMADA`明确作为合成缓存输入；随后已在
[AMD初始化报告](legacy-amd-initialization.md)恢复其赋值链、CPUID返回域与表更新副作用。

TRX50名称匹配优先于14a4查找及Shimada；其分支不额外打开两个VRM窗口。
在另一个TR5分支里，两个VRM对象分别设置标题`TR5 VCore0 Tuners`/`TR5 VCore1 Tuners`，
对象`+0x38`分别写入原数值`0x84`/`0x90`再show。只记录数值，不将其擅自定义为SMBus地址或电压单位。

因此，如果真实DMI产品名包含大写`TRX50`，这个菜单按原函数会走`tr5_mb2`。
截图文件夹“TRX50 SAGE”不是实际DMI采集结果；没有把条件结论改写为四机实测。

## 客户端主板与 Memory Timings

`on_actionADL_MB_triggered`（`0x8e6550`，448字节）的优先级：

- 缓存NVL为真：DMI含大写`VZEDC`只创建`nvl_mb2`；否则先`nvl_mb`，再`nvl_tuners`。
- 否则缓存ARL为真：`arl_mb`。
- 否则：`adl_mb`。这个槽内没有AMD拒绝，也没有再调用`isit_adl`。

`on_actionMemory_Timings_triggered`（`0x8ebd10`，646字节）的优先级是
**NVL→ARL→GNR**，分别创建对应memtime；与Controls的NVL→GNR→ARL次序不同。
三标志均假时，RKL或ADL谓词为真会先创建`intel_memtime`；随后再次检查ADL，若真再创建
`adl_timings`。这意味着ADL路径原本就有两个窗口创建调用，不能一律把多窗口判作重复连接缺陷。
其他输入显示Not Supported。

## 原指令验证与边界

新增六函数共3,038字节，fixture SHA
`3f937b9bd8f5cd2d6fc4fed40227ee7a26924134d4f258648e71c121e92f9719`；
复用已固定SHA的PCI谓词fixture，不把这些谓词替换为按CPU型号猜出的bool。
本地592项：W790菜单120、AMD主板336、客户端主板40、内存时序96。
覆盖未知vendor、大小写/子串、DMI短字符串/堆分配、PCI返回-1/0/非0、多个缓存标志同时为真。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/audit-legacy-menu-routing.py \
  build/input-audit/octool --output docs/validation/legacy-menu-routing-analysis.json \
  --export-fixture analysis/fixtures/legacy-menu-routing.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/emulate-legacy-menu-routing.py \
  --output build/menu-routing.json
```

[原地址/指令报告](validation/legacy-menu-routing-analysis.json)与
[592项Windows报告](validation/legacy-menu-routing-windows.json)保留构造顺序、参数、大小、
show/属性调用及警告文字。所有面板构造器均是明确的空合成边界，便于继续观察第二/第三窗口；
没有执行Qt库、真实窗口、EC/VRM/MSR/PCI硬件访问。也未把未运行的构造函数视为“支持/初始化成功”。
原GUI/模块/96字节协议保持原样，报告作为恢复规格而非启动平台伪装器。

`fa4c541 / 37766384139`已包含本页改动并完整23/23通过，Linux592项观测与Windows除environment一致；
见[完整云端记录](validation/legacy-initialization-ci-fa4c541.json)。
