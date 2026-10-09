# 原 AMD MP1 通道、局部标志来源及两个操作槽

日期：2026-10-09。接续[SMU传输](legacy-amd-transport.md)，输入仍是原 Linux ELF SHA
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
本页描述原指令的行为，不用函数名/营销型号给固件消息、地址或输出赋予硬件含义。

## 新闭环与EL兼容关系

1. `FindPciDeviceById2`不枚举PCI：第三参数直接被清零，只读domain0/BDF0的vendor/device。
   同一身份被反复读取来测试不同候选；不是每个候选去找另一块设备。
2. `MP1_C2PMSG`没有“完全未识别就拒绝写”的出口；候选全不匹配时仍回到BDF0继续传输。
3. 它只比较完整32位状态是否等于1，最多读21次；其他数值一律继续到次数耗尽。
   耗尽后仍读取输出、释放端口并返回最后状态。后端读失败的`ffffffff`也按这条路径传播。
4. `cpufunctions::on_per_ccx_oc_apply_3_clicked`的两个通道均不处理返回/输出，随后显示`Applied!`。
   此行为不能作为命令在硬件上成功的证据。
5. `_19`槽把Qt给出的32位有符号值扩展至64位，经原smu_cmd2/PCI路径传出两个dword；-1实际成为
   `ffffffff:ffffffff`。它判断第二次取文字的长度，未传入解析成功标志，没有结果提示。

私有运行库、MOK签名或更换`/dev/mydev`模块不会自动重写这些直接libpci/端口路径和反馈逻辑。
重构平台功能时须单独处理错误反馈；本次未改原GUI、生产GUI、HAL、模块或96字节ABI。

## 地址及执行范围

| 入口 | 原地址 | 范围 |
|---|---|---|
| `MP1_C2PMSG(unsigned,unsigned,unsigned&)` | `0x21f700` | 完整1019字节 |
| `FindPciDeviceById2` | `0x21dbb0` | 完整110字节 |
| `ReadPciConfigWord` / `libpci_read_word` / `pci_read_word` | 见静态报告 | 原ABI桥、对象与typed read；后端合成 |
| `cpufunctions::_3` | `0x750170` | 完整339字节；Qt消息框合成 |
| `cpufunctions::_19` | `0x7520b0` | 完整272字节；Qt取文字/解析结果合成 |
| `AMD_PM_LOG`构造器身份块 | `0x2cb08b`等五块 | 仅局部块，到`0x2cb0c9`停止 |
| `cpufunctions`构造器复制块 | `0x7629e9` | 仅26字节，到`0x762a03`停止 |

新fixture含2668字节原代码，复用此前固定1876字节的SMU/PCI/端口软件包装器。
fixture SHA：`45e108afa40e7925c76687e7f9c475ab329814a8c9ac5849c135e7e927f25b07`。
每段带父函数地址、父函数SHA及相关指令；不把截取块当作完整构造器执行。

## MP1选择及次序

按顺序比较以下vendor:device，所有比较都读取同一个`0000:00:00.0`：

`1022:1630 → 1002:1630 → 1002:1636 → 1022:14b5 → 1022:14e8 → 1022:1480 → 1022:1450`

前五个命中任一个，命令/响应/参数选择值为`03b10a20 / 03b10a80 / 03b10a88`。
后两个命中及后续回退时，GPT缓存标志非零也使用这组三值；否则参数值为`03b10a40`，
Shimada非零时命令/响应为`03b10924 / 03b10970`，为零则为`03b10524 / 03b10570`。
这些名称来自软件变量，仅表示分支条件，不表示具体CPU的实测身份。

前七项皆未命中时，Granite或Shimada非零直接保留BDF0；否则再测试`1022:14d8`、`1022:14a4`。
仍未命中则调用纯位拼接`find_pci_dev2(0,0,0)`，得到0。此处没有设备不存在的失败返回。

入口接收32位command、32位argument，与smu_cmd2低8位command/64位argument不同。
每次访问以PCI`0xb8`选择地址、`0xbc`读写数据，按以下顺序发送：

1. 获取原端口软件锁；等待耗尽后仍继续的行为沿用此前证据。
2. `0x50200 = 1`。
3. 响应选择值`=0`，参数选择值`=argument`。
4. 再写响应`=0`、参数`=argument`，保留原来重复写入。
5. 命令选择值`=command`。
6. 选择响应地址，请求一次100000微秒等待，然后读取状态。
7. 若不等于1，最多再读20次；循环内无新增sleep，不提前识别其他错误码。
8. 选择参数地址，读回并无条件写入输出引用，释放端口，返回最后状态。

合计14次PCI写、1到21次状态读、1次参数读。前12次写各请求2000微秒延迟。
身份比较有条件性1000微秒延迟，锁忙有独立延迟。这里只统计请求，不声称实际耗时。
写/延迟/输出读失败不会触发单独的上层错误恢复。合成后端失败时不写读缓冲，
由原`pci_read_word/long`产生`ffff/ffffffff`，不是在模拟器里替它们返回哨兵。

## `+0xf88`来源的局部证据

`cpufunctions`在`this+0x1f0`构造AMD_PM_LOG。后者初始qword写清`+0x78..+0x7b`；
局部身份判断命中`1022:14e8`时设`+0x79=1`，**GPT非零**也通过`word +0x78=0x101`设置它。
此处RIP目标是`GLOBAL_IS_GPT@0x1c7c501`，与邻近Shimada变量不同。初次手工推导混淆了两者，
原指令实验拒绝错误预期；现已按实际地址修正并加独立回归，不把相似变量名当作证据。

五块实验显式采用原初始化字节，覆盖九类身份、未知身份及三个缓存标志组合。
原复制块把`this+0x269`（嵌入对象`+0x79`）拷到`this+0xf88`；非零选MP1，零选smu_cmd2。
实验保留对象后执行复制和槽，证实这段局部数据流；未执行中间完整构造及refresh_monitor/get_pm。
仍须核对运行时其他写入者、别名/并发和四台机器实际标志；不从这段实验推断整条初始化成功。
非标准布尔字节2/255另测，同样选MP1，不赋予这些字节新平台含义。

## 两个槽的输入和反馈

- `_3`：`+0xf88=0`调`smu_cmd2(0x25,1,out)`；非零调`MP1_C2PMSG(0x18,1,out)`。
  两支即使后端全部读失败，仍走原`Status / Applied!`及information调用。只证明直接槽体；
  对应按钮的名字、连接、可见性及可点击状态尚未在本页恢复。
- `_19`：`this+0x1008 -> Ui+0x268`，第一次取文字后`toInt(ok=null,base=10)`，第二次取文字判断长度。
  第二次非空才调`smu_cmd2(0x56,sign_extend_64(parsed32),out)`，无范围限制/返回检查。
  覆盖两次空/非空组合、-2147483648/-1/0/1/255/256/2147483647、两种Qt引用计数。
  Qt解析器未执行，结果明确为合成输入；不声称任意文本到数值转换已验证。
  未知消息单位保持未知，没有把截图电压/电流或其他平台规则移入实现。

## 复现与门禁

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-amd-mp1.py \
  --binary build/input-audit/octool --export-fixture analysis/fixtures/legacy-amd-mp1.json \
  --analysis-output docs/validation/legacy-amd-mp1-analysis.json \
  --output build/amd-mp1.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-amd-mp1.py \
  --output build/amd-mp1.json
```

563项实验：80种MP1身份/标志组合、259种状态值、6个完成位置边界、20组参数宽度、
15项失败/通知、80项局部标志→复制→槽、2种非标准布尔字节、56项输入/引用计数、3项输入后失败，
以及42项身份索引/读取失败及回退。另5项关键回归，接入portability前置门禁。

所有PCI后端、端口、TSC入口、分配器、Qt及sleep止于合成边界。原指令PC白名单拒绝未知执行，
不执行宿主syscall/CPUID/IN/OUT/MSR，不加载模块或写真实硬件。已知缺陷符合原行为算刻画通过，
不算问题已修复。Windows与云端结果分别记录。

[逐指令证据](validation/legacy-amd-mp1-analysis.json)和[Windows观测](validation/legacy-amd-mp1-windows.json)
随源码保存。下一步仍包括真实Qt绑定、全部上层调用、固件含义、原PCI后端、完整构造影响及实机验收。

`7690e2a / portability37862775672`最终23/23成功；probe37862775686成功。Linux24项port、31项
分析无跳过；含本页563项的3512项原指令门禁通过。下载9份报告与Windows除environment完全一致，
作业/artifact摘要及逐报告SHA见[云端记录](validation/legacy-mp1-ci-7690e2a.json)。不包含随后NGU新实验。
