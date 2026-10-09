# 原始内存写入、NVL查询及64位fallback

日期：2026-10-09。原ELF SHA
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
以下是原指令在Unicorn中执行的行为，不是依据函数名称推测。Qt解析、文件接口、内核应答及映射
都由合成边界提供；原/dev/mem分支内的store仅写模拟器内存，没有打开宿主设备。
不修改原GUI调用点、重构GUI、HAL、模块或96字节线级协议。

## 原始内存页写入

完整`rw_memory::on_pushButton_2_clicked()`位于`0x69d4f0`，1110字节。
对象`this+0x68`指向Ui，Ui+0x18为地址输入，Ui+0x10为数据输入。顺序如下：

1. 任意输入文字为空，显示`Nothing to Write!`，不解析、不断开timer、不访问后端。
2. 两输入非空，断开`this+0x70`的timer连接。两次`QString::toLong(ok,16)`依次解析地址、数据。
   提供非空ok指针，却未检查返回的bool；实验的解析值/成功标志是合成输入，未运行真实Qt解析器。
3. 地址保存在`this+0x58`和`this+0x48`。后者正是嵌入的RW_MMIO对象`this+0x30`的基址成员+0x18。
4. 数据高32位为0时调用32位Wr_MMIO，否则调用64位Wr_MMIO64；两者传入的地址参数都是**0**。
5. 两个成员写方法都不读取对象基址。模块路径最终请求data0地址为0；opcode分别为0x0d/0x0b。
   数据`ffffffff`走32位，`100000000`及负数64位位型走64位。保存输入地址没有改变这一步。
6. 后端返回后显示`Applied!`、恢复连接并以2000ms启动timer。原内核邮箱无限等待时停在断开之后，
   没有恢复连接或Applied。合成write=-1但done=1时，因原包装器忽略返回值仍可完成。

例：地址输入合成解析值`fed10000`，数据`1234567887654321`，实际96字节请求前四个QWORD为
`[0x0b,71,0,0x1234567887654321]`。不是请求`fed10000`；该结论同时覆盖32位数据。
本页完整执行槽体及包装器，但未验证此按钮真实Qt绑定、构造完成和事件触发。

## 64位写路由：模块与/dev/mem差异

`RW_MMIO::Wr_MMIO64(unsigned long,unsigned long)`位于`0x231600`，21字节：
忽略this，将地址/数据传入原`Write_MMIO64@0x36f4e0`（389字节）。
模块标志非零时调用原Write_MMIO64_kernel，**完整保留64位地址和数据**。

标志为0时进入原/dev/mem路由，使用原open标志`0x101002`，取页大小，按页对齐目标，映射两页。
本轮合成页大小4096，连同跨页偏移4095进行测试。真正的store指令前为`mov eax,ebp`，随后
`mov qword ptr [...],rax`：写入宽度虽然是8字节，值却是**原数据低32位零扩展**。
例如`1234567887654321`变为`0000000087654321`；`100000000`变为0。

所有open/sysconf/call_sys_mmap/munmap/close均为模拟器内的替身；只执行原路由中的地址运算与store。
没有执行call_sys_mmap的NASM/syscall体、真实映射或内核权限策略。当前仅刻画映射成功的路径，
未将其误算成/dev/mem权限、映射失败恢复或真实EL机器的运行结果。

原始内存槽使用fallback时仍传地址0；它的32位store保留低32位，64位store同样只保留低32位。
因此“去掉新模块、改用/dev/mem”不能自动修复该原槽地址或数据问题。

## NVL名为read的helper仍写命令

`NVL_MEM_CFG::read_BIOS_MAILBOX_DATA@0x245820`，184字节，完整执行范围：

- 最多读取101次`base+5da4`的busy位；耗尽仍继续。
- 经原32位Wr_MMIO向地址`5da4`写`(command & 1fffffff) | 80000000`，没有加base。
- 再最多读取101次`base+5da4`；最后读取`base+5da0`并填输出引用。
- 返回最后status，包括仍busy的`80000000`；持续busy不会阻止输出数据。

本页的“查询”不是只读硬件操作。预/后busy与内核邮箱done是两个条件，实验让内核done=1才能
到达101次循环上限。任何真实对拍采集/只读名单都不能因函数名包含read就允许整个helper。
尚未定义这些命令在各平台的硬件含义，也未自动移入重构GUI。

## 范围与复现

新fixture1704字节，SHA
`2b317894c2c83a9184622cf318a9b17cb1d941d3d66d1596c725319c758b5a69`，
复用[NGU fixture](legacy-intel-ngu.md)、固定MSR叶函数和旧64位kernel写包装器。
168项刻画覆盖72种原槽正常输入、空输入、ok=false、两宽度邮箱错误、NVL读的忙/命令掩码，
以及直接/成员64位写两路的地址/数据。8项预期永久等待由指令预算停止，不能叫成功恢复。
另4个回归固定零地址、忽略ok、NVL忙后仍写读以及64位fallback截断。Windows本地全部通过，已加入CI。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-mmio-clients.py \
  --output build/mmio-clients.json
PYTHONPATH="$PWD/build/reference-tools" python3 -m unittest discover \
  -s analysis/tests -p 'test_mmio_clients.py' -v
```

静态：[原函数证据](validation/legacy-mmio-clients-analysis.json)；
动态：[168项记录](validation/legacy-mmio-clients-windows.json)。
前一NGU提交的十目标23/23云端成功、35项Linux分析测试及3778项原指令结果独立保存于
[a9de46a云端记录](validation/legacy-ngu-ci-a9de46a.json)，不拿该结果冒充本轮新增门禁已跑。
继续核对剩余Wr_MMIO调用者，包括MailboxRead/PollMailboxReady、FIVR、AMD FCH与I2C。
