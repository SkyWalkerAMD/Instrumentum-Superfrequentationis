# 原 AMD 初始化：不可达比较与全局表更新

日期：2026-10-08；原ELF SHA `44598dc8…aff10`。本页补齐
[主板菜单报告](legacy-menu-routing.md)中Shimada缓存的来源，继续执行作者指定的离线研究。

## FamilyType返回域和调用者不一致

完整 `FamilyType()` 在`0x21e510`，156字节。调用原包装器
`CpuidTx(leaf=0x80000001, ..., helper_mask=0)`，取写入第一个输出指针的EAX，再计算：

```text
x = (eax >> 20) & 0xff
return x == 6 ? 0x0f : x == 8 ? 0x11 : x == 10 ? 0x13 : 0
```

原函数没有在这条路径验证vendor、基础family位、CPUID最大叶，也没有其他返回值。
此处只陈述原位运算，不据营销型号假定某台机器的CPUID原始值。
对全部256种x值执行原指令，返回集合严格为`{0,0x0f,0x11,0x13}`；高4位及低20位变体另行验证。

- `is_granite`（`0x225510`）却先比较返回值是否等于`0x1a`：这条直接接受分支不可达。
  非零返回立即为假；只有返回0才查询`1022:14d8`是否存在。
- `is_shimada`（`0x225560`）要求返回0，再查`1022:153a`；找到后调用`set_to_shimada`并返回真。
- `is_gpt`（`0x2255b0`）先查`1022:1122`，找到立即真；没找到才取FamilyType，比较`0x0b`。
  这个比较也不可能为真，所以后续第二次1122查询在当前原函数组合里不可达。
- `is_pheonix`（原符号就如此拼写，`0x21e290`）只查`1022:14e8`，成功改参数表并写`GLOBAL_IS_PHX=1`；
  失败返回0，却不清除此前的PHX标志。冷启动与重复调用不能混为一谈。

这证明原程序的判断之间有内部不一致。它不是EL8/EL9/EL10的glibc差异，也不能仅凭这点断言
9995WX或其他具体CPU已被误识别；需要真实CPUID/PCI输入才能作后一个判断，目前仍未采集。

## 主窗口中的调用顺序

原构造区域`0x8e4348..0x8e4389`，回到`0x8e42ab`之前依次执行：

```text
is_pheonix()                         # 可能修改表，结果不在此另存
GLOBAL_IS_GRANITE = is_granite()
GLOBAL_IS_SHIMADA = is_shimada()      # 可能再修改表
GLOBAL_IS_GPT = is_gpt()
if GLOBAL_IS_GPT: set_to_gpt()        # 最后再修改表
```

这是一串独立调用，不是互斥的else-if。因此合成输入同时含多个PCI设备时，可以同时置多个标志，
参数表则按顺序叠加更新。此类合成组合用于证明控制流，不宣称真实主板一定同时枚举这些设备。

## 三套软件参数表是部分覆盖

三个set_to函数只有对进程全局变量的立即数存储，不在函数体里进行硬件访问。
分别恢复pheonix64项、shimada81项、gpt64项赋值；关联的117个全局对象保留原文件初值/宽度。

| 原变量名 | pheonix赋值 | shimada赋值 | gpt赋值 |
|---|---:|---:|---:|
| `SMU_ARG0` | `0x3b10998` | `0x3b109c4` | `0x3b10998` |
| `SMU_ARG5` | `0x3b109ac` | `0x3b109d8` | `0x3b109ac` |
| `SMU_IOPORT` | `0x3b10578` | `0x3b1097c` | `0x3b10978` |
| `SMU_DATAPORT` | `0x3b10528` | `0x3b10930` | `0x3b10928` |
| `BIOSSMC_MSG_EnableOverclocking` | `0x57` | `0x24` | `0x57` |
| `BIOSSMC_MSG_SetOverclockFreqAllCores` | 不赋值 | `0x26` | 不赋值 |
| `BIOSSMC_MSG_SetOverclockVID` | 不赋值 | `0x28` | 不赋值 |
| `BIOSSMC_Message_Count` | `0x68` | `0x57` | `0x71` |

**变量名是原程序符号，不是本报告独立确认的硬件规范或可直接使用的寄存器定义。**
表中“不赋值”表示保留进入时的值，不表示设为0或功能不存在。
例如先Shimada后GPT时，SMU_IOPORT变成GPT值，但GPT未赋值的49个Shimada条目继续保留。
原指令实验已核对这一混合状态，不能简单把set_to_gpt理解为完全替换整个表。
Shimada的WriteSviRegister和RunCpoAgingBtc两个变量都赋值`0x11`，亦原样保留，不擅自修正。

## 有界实验与复现

公开fixture为八个完整函数及一个65字节构造片段，合计3,461原指令字节；SHA
`ea27159591a37b7513a721e9eedca1edbe4a59629a4911f00d539d52432399bb`。
385项包括256种返回域、14种高/低位变体、112种初始化组合、3种PHX旧标志保留条件。
用原存储指令与独立提取的立即数赋值表比较117个对象的最终状态；报告存状态SHA及标志/查询顺序。
另有专门回归确认两个不可达值、Shimada→GPT的部分覆盖、失败PHX探测不清旧标志。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-amd-initialization.py \
  --binary build/input-audit/octool \
  --export-fixture analysis/fixtures/legacy-amd-initialization.json \
  --static-output docs/validation/legacy-amd-initialization-analysis.json \
  --output docs/validation/legacy-amd-initialization-windows.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-amd-initialization.py \
  --fixture analysis/fixtures/legacy-amd-initialization.json --output build/amd-initialization.json
```

[静态地址/指令/表](validation/legacy-amd-initialization-analysis.json)及
[Windows观测](validation/legacy-amd-initialization-windows.json)均在源码内。
CPUID包装器、PCI枚举的边界全为合成；连宿主CPUID指令也未执行，更没有SMU/MSR/MMIO硬件写入。
入口采用原ELF数据初值，外加明确的PHX状态变体；未模拟构造之前所有修改，不把它当作完整启动实测。
目前恢复了软件表选择行为，真实消息定义、单位、固件返回和写入效果仍需独立证据。

`fa4c541 / 37766384139`完整23/23成功，Linux385项与Windows除environment完全一致；
分析23项测试无跳过，详见[云端记录](validation/legacy-initialization-ci-fa4c541.json)。
