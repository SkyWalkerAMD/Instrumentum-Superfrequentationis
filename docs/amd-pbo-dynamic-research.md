# REA：动态内存与动态 EDC 的原始路径

2026-10-21：[REA 任务](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39545928793)
成功完成十个函数的查询、身份核对、证据导出和关闭。
工具固定为 REA 6.7.0 / Ghidra 12.1.4；原 ELF SHA256 为
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
[机器记录](validation/amd-pbo-limits-rea.json)保留函数地址、原字节哈希、反编译长度差异和证据哈希。

本次已实现的通用 PBO 四项限制见[功能说明](amd-pbo-limits.md)。下面两条原版路径仍未开放硬件写入。

## 动态内存切换

原 `AMD_PBO::toggle_dynamic_mem(bool)` 在 `2395b0h`，完整 ELF 符号长度 501 字节。
用完整原构造函数选择 Shimada 对象后，执行两种布尔输入及每个 PCI 写入故障位置，共 30 个实验。
每个实验都返回，执行相同的 14 次 PCI 写入，无完成状态读取：

1. B8/BC 写 `50200h=1`。
2. 清 response；写入布尔参数；再次清 response。
3. 向 response **主动写 1**；再写一次参数。
4. 发送 `92h`；最后把 index 指向 argument 并释放锁。

不能将原函数的返回或预先写入的 response=1 当作固件完成确认。
固定公开 Shimada 配置未提供该消息的独立语义、查询及版本约束，因此未加入运行时白名单。

## 动态 EDC

原 `dyn_edc::apply_clicked` 在 `7df630h`，完整符号长度 1944 字节。
这是 **EC bank 1 路径**，与新实现的 RSMU EDC 消息 `58h` 不同。
原 `refreshit` 和 `update` 也通过 EC 读取；`refreshit` 自身会写入选择寄存器，不能按函数名当作纯读取。

| 原对象 / EC 项 | 原指令观测 |
|---|---|
| C1 | 先读取并清 bit 0；提交后恢复保存值，启用输入可将 bit 0 置位 |
| C2 | 选择 0/1/2 的寄存器组 |
| C4 | 选择的策略，零或另一组合框索引加一 |
| Ui 向量 +70 / +88 | 输入原值的低 16 位，以高字节在前写 C6/C7、C8/C9 |
| Ui 向量 +A0 / +B8 / +D0 | 同样写 D0/D1、D2/D3、D4/D5 |
| 非所选寄存器组 | 本次有效选择实验中，两个字写入 65000，即 FD E8 |
| 用户反馈 | 先显示 Applied，再调用另一个刷新函数 |

原完整应用槽回放 281 个组合：216 个 UI 行/选择/策略/启用/控制位组合、
40 个输入边界、1 个基准与 24 个丢弃写入位置。输入 65536 的对应字被写成零。
合成 EC 丢弃一次写入后，原应用槽仍走到 Applied，刷新回调在本实验中明确作为替身；
这不声称该回调的真实读回成功，也不推定 EC 包装函数的实际错误返回约定。
EC 寄存器的物理单位、支持的主板和 EC 固件、选择寄存器竞争及事务协议仍需确认。

## 其它查询与边界

`get_THM` 是刷新后读取 PM 存储值，偏移来自对象 +3Ch，特定标志下改为 44h；
不能用它证明 `SetTctlMax` 的当前配置。三个 `TEST_SMC_MSG_*limit` 入口及
`set_to_shimada` 的不同 mailbox / 平台分支也已检查，未混入新 PBO 限制协议。
REA 对混合调用约定的伪代码会丢失 PCI/EC 参数，所以具体寄存器与字节顺序采用原指令证据。

可重放工具与固定字节随源码提供；不启动原 ELF，不访问设备或执行原 Qt/固件：

```sh
PYTHONPATH=build/reference-tools python3 analysis/tools/legacy-amd-dynamic-controls.py --output build/dynamic-controls.json
PYTHONPATH=build/reference-tools python3 -m unittest discover -s analysis/tests -p test_amd_dynamic_controls.py -v
```

[工具](../analysis/tools/legacy-amd-dynamic-controls.py)、[字节 fixture](../analysis/fixtures/legacy-amd-dynamic-controls.json)、
[三项回归](../analysis/tests/test_amd_dynamic_controls.py)在本机执行通过。
这三个新增分析回归产生于生产提交 `3b374a0` 之后，未计入该提交云端的 122 项分析测试。
原指令实验的设备、Qt、身份及定时器边界均为合成替身；没有真机操作。
