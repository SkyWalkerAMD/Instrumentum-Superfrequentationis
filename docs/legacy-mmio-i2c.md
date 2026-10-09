# 原I2C状态机及10个MMIO直接调用者覆盖

日期：2026-10-09；原ELF SHA
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
完整`CpmReadWriteI2CBytes5@0x230f40`的1664原指令字节，在模拟器中接到既有RW_MMIO、路由和
96字节请求包装器。没有执行宿主I2C/设备、原完整Qt或原/dev/mem分支；原程序和生产代码未改。

## 参数、表及目标地址

按寄存器及字节load/store恢复六参数：bus低8位、slave低8位、接收计数64位、接收缓冲指针、
发送计数64位、发送缓冲指针。名称是依据本函数实际使用方向，不从平台营销名推断总线拓扑。

GLOBAL_IS_SHIMADA非零时直接选择mI2CConfiStp；否则调用is_tr5_es，真时选择同一表，假时选择
mI2CConfigRplRmb。平台谓词在本实验是明确的合成bool边界，未冒充本轮完整硬件身份检测。
两张表各72字节、6个12字节条目，本函数只使用条目+8的32位基址。原文件中两表这一列相同：
`fedc2000/fedc3000/fedc4000/fedc5000/fedc6000/fedcb000`。其他字段保留原字节但未猜其物理意义。
原初始化字节是实验前提，不代表真实进程运行到此时表必定没有被改过。

取bus低8位直接索引，原函数没有6项边界检查。bus=6/7/255的实验由**模拟器**在原越界load前
停下，结果out-of-table-index不是原程序会报错；没有给表外内存虚构一个地址。bus=256截为0。
合法条目取基址后，构造RW_MMIO，再覆盖其base成员。所有读发往base+offset；全部14处写CALL
均把offset原样发往低地址（0/4/10/14/18/30/38/3c/6c），仍不加该base。

## 初始化及传输顺序

1. 写6c=0，轮询base+9c的bit0直到0。
2. 写0=63、4=slave低8位、14=285、18=357、30=0；读取base+40、base+54；写38=0、3c=0、6c=1。
3. 轮询base+9c的bit0直到1。尚未依据这些数字声明具体控制器型号或时序单位。
4. 先发送所有tx字节，再提出rx读取。向低地址10写普通tx字节；无接收且为最后tx字节时OR200。
   rx命令为100，最后一个为300。读取base+10的低8位逐字节写入接收缓冲。
5. 结束时等待base+70的bit5清零；正常路径返回1。所有系统应答在实验中为合成，不能当作真实I2C成功。

每次传输前检查base+70的bit3和base+74：前者置位或后者非零时，只调用usleep(10)后回到外层。
此回路不递减发送/接收计数，也没有总次数/时间预算。持续输入下，110项实验中的两项由指令预算
截停；它们已穿过成功的初始化，不能与底层内核邮箱未完成混为一谈。

## 返回值、超时和部分输出

| 路径 | 实际原读取/预算 | 返回和缓冲 |
|---|---|---|
| disable持续bit0=1 | 201次读取，200次usleep(100) | 打印I2cDisable诊断，却返回**1**；尚未开始传输 |
| enable持续bit0=0 | 201次读取，200次usleep(100) | 诊断后返回0 |
| base+34的bit6持续置位 | 首次+10次重读，共11次；重复间usleep(100) | 再读base+54、诊断、返回0 |
| 接收未就绪 | 单字节首次+201次重读；最后重读之后先检查累计计数，再检查ready | 诊断后返回0，已接收字节保留 |
| idle持续bit5=1 | 202次常规读取、201次usleep(100)，超时分支额外读1次，共203 | 诊断后返回0，已收到的数据仍保留 |

接收重试计数r15在整个调用开始时清零，**不随每个字节重置**。三个字节分别等待100、100、0次
可成功；等待100、100、1次时第三字节失败，即使最后一次合成应答已ready。
实测输出仍为`a0 a1 cc ...`，前两个字节已写、其余保持实验哨兵；调用者不能把返回0理解成缓冲未改。
诊断中的iostream/ctype/endl由合成对象与边界提供，测试覆盖ctype已初始化/待初始化两种常规分支，
未执行C++异常处理、真实终端、locale自定义虚方法或iostream内部。

另一接口反例：rx_count=ffffffffffffffff、tx_count=1的总和按64位回绕为0，跳过传输而仍返回1。
这是对函数64位参数的实验；没有证据说实际UI会提供这么大的长度，不拿它冒充可实际点击的漏洞。

内核邮箱done错误/无应答/短写仍在第一笔6c写下层无限等待；4项由指令预算截停。
write=-1但合成done=1时，原包装器忽略write返回而继续正常流程。没有修改协议来伪造成功。

## 10个直接调用者的范围已逐点核对

新增110项I2C刻画（6项预期持续等待、4项表外load前停止）及9项跨调用者探针，共119项。
跨探针从各入口执行原函数，在Wr_MMIO入口记录CALL返回地址，并保存最终96字节请求；
与原fixture的CALL清单逐个比较，当前10函数28处直接CALL均已覆盖：

| 原调用者 | 直接CALL处数 | 规格 |
|---|---:|---|
| NVL write_BIOS_MAILBOX_DATA | 2 | [NGU/MMIO](legacy-intel-ngu.md) |
| rw_memory写槽 | 1 | [原内存页](legacy-mmio-clients.md) |
| set_ngu_ratio | 4 | [NGU/MMIO](legacy-intel-ngu.md) |
| AMD Wr_FCH_MISC | 1 | [公共邮箱/FCH](legacy-mmio-services.md) |
| MailboxRead | 1 | [公共邮箱](legacy-mmio-services.md) |
| Wr_intel_fivr_spread | 1 | [FIVR](legacy-mmio-services.md) |
| Wr_intel_fivr_fsw | 1 | [FIVR](legacy-mmio-services.md) |
| NVL read_BIOS_MAILBOX_DATA | 1 | [原NVL读](legacy-mmio-clients.md) |
| SET_DYNAMIC_FREQ | 2 | [公共服务](legacy-mmio-services.md) |
| CpmReadWriteI2CBytes5 | 14 | 本页 |

这些调用点都执行过传offset的路径，但该结论只针对固定ELF、这一32位成员重载、声明函数中的
对齐直接CALL。tail jump、间接调用、内联代码及其他MMIO API不在28处分母里，也不等于10个面板
所有合法输入/初始化/并发/故障分支已完毕。AMD update尾调用的合成STOP返回地址单独标为非CALL，
不把它算成额外原CALL指令。上层面板与实际平台地址定义仍须继续恢复。

## 证据与复现

fixture SHA `91c77a2bee73d5a1fb4eab6dfec63eb7ee9d596e34792c67136204b896ab0618`，
包含1664原代码字节、两张表/flag初始化字节、字符串及两项stream relocation说明。
5项回归固定disable错误返回、接收预算/部分输出、外层无界等待、索引及计数回绕。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-mmio-i2c.py \
  --output build/mmio-i2c.json
PYTHONPATH="$PWD/build/reference-tools" python3 -m unittest discover \
  -s analysis/tests -p 'test_mmio_i2c.py' -v
```

[完整静态证据](validation/legacy-mmio-i2c-analysis.json)；
[119项原指令观测及28处覆盖](validation/legacy-mmio-i2c-windows.json)。Windows已通过并接入CI。
没有为EL发行版关闭硬件访问策略，没有将这些原函数中的错误语义照搬进新GUI，也没有更改原ELF。
