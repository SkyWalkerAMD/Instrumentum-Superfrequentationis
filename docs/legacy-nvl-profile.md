# 原NVL配置导入：长度和读取结果门禁

日期：2026-10-09；原ELF SHA
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
这是追踪第三个set_ngu_ratio调用者得到的文件导入路径，不是另一个单字段NGU按钮。
原Qt连接证据已经确认Load Profile（Ui+360、objectName=load_profile）连接到
`intel_ctl6::on_pushButton_12_clicked@0x6182d0`，Save Profile连接到`_11@0x615630`。
本页未执行真实对话框、完整配置应用或设备，只在原指令模拟器中验证文件门禁到第一笔MSR请求。

## 固定导出长度与只有下限的导入检查

原保存函数在`0x6177ca..0x6177e2`的24字节片段向ostream.write传：
buffer=rbp-1930，length=18f2，即**6386字节**。本片段直接执行到合成write边界，固定宽度已验证。
完整保存函数有11413字节，其字段生成和硬件读取目前仅在静态报告中，不称完整动态恢复。

加载函数先用QFile.open(11h)，再通过C++ filebuf独立打开文件，tellg、seek到末尾、tellg、关闭，
以两次位置的差值作为length。原比较`cmp length,18f1; jbe reject`，因此只拒绝<=6385。
之后以另一filebuf重新打开，直接执行`istream.read(rbp-1930,length)`；没有上限夹值或精确6386检查。

| 合成报告的length | 原加载行为 |
|---:|---|
| 6385及以下 | 显示File wrong size，不读配置、不到MSR |
| 6386 | 请求6386字节，继续应用入口 |
| 6387、6392、6393、8192等正值 | 原指令将完整length传read；本实验在该边界停止，不执行超长复制 |

本函数栈canary在rbp-38，与read目标相距18f8=6392字节；6386字节导出区域之后只有6字节间隔。
如果真实read完整交付6393个或更多正长度字节，就会触及canary区域。报告保留原地址/运算，
**没有在宿主或模拟器实施这种越界复制**，也没有生成可用的攻击文件或推断任意代码执行。
原栈保护检查在返回时；仅有该检查不能作为导入长度验证的替代。

tellg失败的返回值同样未单独检查。若合成first=0、last=-1，差值按64位形成ffffffffffffffff，
通过无符号下限判断并传到read。该位型作为C++ streamsize又是-1，本页不推断标准库会执行
超长复制；这是原调用者缺少错误判定的证据。两次tellg均-1时差值0，则走小文件拒绝路径。

## 短读仍进入原MSR命令

报告长度6386时，给原istream.read合成交付0、1、4、6385或6386字节，再让其正常返回。
原槽没有检查gcount、failbit或读后的流状态，close后立刻调用`percoreoverride_en@0x3741c0`。
本轮实际执行该原函数的前缀及原Wrmsr叶函数，直到其libc write边界，记录：

```text
path = /dev/cpu/0/msr
lseek offset = 0x150
write count = 8
bytes = 00 00 00 00 14 00 00 80
```

即使交付0字节，仍到达这一请求。不能把“流read调用返回”当成配置读取成功。
为清晰观察短读，实验预先把局部配置区设置为5a或a5，并使用00/ff两种合成内容；未交付部分
保持受控初始值。这里不是声称真实进程的未初始化栈总为这些值，后续字段如何使用仍另行研究。
任意内容的精确长度文件在首个硬件请求前没有内容签名、版本、字段或校验和验证。

所有open/lseek/write和Qt/C++流均为合成接口，没有打开真实MSR文件。执行停在第一笔write之前，
没有运行该命令、后续忙轮询、bit3更新、其余配置写入或Applied提示。
percoreoverride_en静态后续确有Rdmsr busy循环、额外读取、OR8和80000015写命令，但本页未把这部分
计算为动态通过。不能把第一笔命令写请求误称为已经改变了某个真实硬件配置。

QFile打开失败、第二次filebuf打开失败、过小文件均单独覆盖，走消息/析构后返回，不到MSR。
filebuf.close返回失败则只设置流错误位，仍可能继续；该合成组合也到达首个MSR边界。
Qt取消是空路径和QFile.open=false的明确合成前提，不是本轮真实点击对话框的结果。

## 后续格式恢复入口与本轮范围

新fixture含加载函数6161字节、percoreoverride_en的196字节、24字节保存片段，共6381字节，
复用固定Wrmsr fixture。SHA
`70f812290be3721799ac44f142ee74ac91941096ea5be27d5d3422ba20ce8a9d`。
42项门禁刻画和4项回归已在Windows通过并接入CI；报告保留读请求、流事件、原始缓冲哈希和MSR请求。

静态报告还列出配置栈区的直接RBP相对操作数及调用点，供下一步恢复字段：
例如NGU从rbp-2f9取一字节，即配置偏移1637h（5687），于619093调用set_ngu_ratio；
这条导入路径限定该参数为0..255，与单字段toUInt→signed int的入口不同。
索引数组、别名指针、全字段定义和Save/Load对称性尚未核完，不将这份操作数清单当成完整格式。
后续还包含每核MSR、四组ratio列表、时钟发生器、UFS、VF点及OCTVB对象操作，不能由本页门禁测试
宣称恢复；单位、CPU型号及寄存器合法范围不从字段名推断。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-nvl-profile.py \
  --output build/nvl-profile.json
PYTHONPATH="$PWD/build/reference-tools" python3 -m unittest discover \
  -s analysis/tests -p 'test_nvl_profile.py' -v
```

[静态加载/保存/调用证据](validation/legacy-nvl-profile-analysis.json)；
[42项原指令观测](validation/legacy-nvl-profile-windows.json)。生产GUI目前没有此配置导入功能。
将来恢复时需独立定义精确格式、完整读取、字段/平台验证及确认后应用；不在内核协议中补救文件错误。
