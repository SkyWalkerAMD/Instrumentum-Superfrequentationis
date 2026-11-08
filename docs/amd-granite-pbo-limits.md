# Granite Ridge PPT、TDC、EDC 与温度限制协议

参考 SMU `98.84.0` 的 RSMU `56h..59h` 支持四项独立限制提交。
原始指令建立了输入转换、各自的限幅和保存路径；响应 1 仅表示固件处理了
请求。此证明没有建立可供主机使用的当前限制回读，不能将接受报告为设置
一致或实际功耗、电流、温度已经改变。

参考固件仍是 ASUS ProArt X870E-CREATOR WIFI BIOS 2503，目标为 9950X3D。
body SHA256 为
`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`。
目标机器不可用、已安装 BIOS 未知；其他固件版本没有获得本证明的授权。

## 命令与单位交叉核对

| 证据 ID | 项目 | RSMU / handler / helper | 输入转换 | 固件保存位置 |
|---|---|---|---|---|
| GRL-01 | PPT | `56h / 0x26614 / 0x26578` | uint32 转 binary32 后乘 0.001 | 选定政策值 `0x9e30`；再乘系数传给消费者 `0x9e58` |
| GRL-02 | VDD TDC | `57h / 0x26928 / 0x268cc` | 同上 | 消费者 `0x9f20` |
| GRL-03 | VDD EDC | `58h / 0x27158 / 0x270e8` | 同上 | `0xa13c` |
| GRL-04 | 温度上限 | `59h / 0x26c7c / 0x26c60` | uint32 转 binary32，不缩放 | 消费者 `0x9f84` |

四项命令表均为 `0x6fa4 + 8*command`，flags 均为 5；比例 literal
`0x11420` 的原始位模式为 `0x3a83126f`。原指令验证编码，物理用途和
W/A/°C 标签还与固定版本的公开实现交叉核对：

- [Zen5Settings](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings.cs)
  继承 [Zen4Settings](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen4Settings.cs)
  的 TYPE_CPU4、RSMU 传输及四个命令定义。
- [SetSmuLimit](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetSmuLimit.cs)
  把前三项接口输入乘 1000；
  [SetTctlMax](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetTctlMax.cs)
  对 TYPE_CPU4 不缩放。没有采用公开实现中的 MP1 自动回退。

这确认了适配使用整数 W/A ×1000、整数 °C 原值的编码。现有接口的
`1..2000` W/A 与 `1..100` °C 是应用边界，不是固件熔丝上限、电气安全
范围或推荐设置。固件还会自行限幅。

## 限幅与状态

以下名称描述代码位置和作用，不声称状态来自目标机器。所有 cap、模式
和系数都作为显式合成输入；它们的真实值需运行时固件决定。

- **GRL-05 / PPT：** 当 byte `0x8197 != 0` 且 MMIO word
  `0x3210064 != 0x80000000`，先对 float `0x9e24` 取较小值；byte
  `0x139b7 != 0` 可撤销这一步，恢复请求。选定中间值还写入 `0xd188`。
  当 variant byte `0x8170 != 1`，再对 float `0x9e18` 取较小值。
  最终值保存到 `0x9e30`，置 byte `0x9e49 = 1`，乘 float `0x8b20`
  后传给共用消费者；过程以锁参数 `0x20` 包围。
- **GRL-06 / TDC：** variant 不为 1 时对 float `0x9f0c` 限幅；
  variant 为 1 时，仅当上述 byte/MMIO 条件成立才限幅。
  byte `0x9f54 = 1`，选定值传给消费者 `0x9f20`。
- **GRL-07 / EDC：** 和 TDC 使用相同的 variant/byte/MMIO 条件，
  cap 来自 float `0xa140`。选定值保存到 `0xa13c`，置 byte
  `0xa14d = 1`。variant 为 1 时以锁参数 `0x10000` 包围，调用
  `0x26208` 刷新政策；其硬件寄存器及间接回调在本实验中是外部边界。
- **GRL-08 / 温度：** 始终对 float `0x9f58` 取较小值，然后传给
  消费者 `0x9f84`，没有按 variant 绕过 cap 的分支。

共用消费者 `0x1c928` 保存输入 float，收紧其历史下界，并保存正输入的
倒数；零或负输入的倒数字段写 1。实验回放实际保存和选择指令，倒数
函数 `0x3f870` 使用显式结果，不模拟目标 FPU 近似或异常。

## 能力门与接受语义

**GRL-09：** `6Fh` 的 bit 1（mask `0x2`）和 `0x2c910(1)`
使用同一 fuse `0x2cd9` 与平台资格条件。手动 OC 是独立 bit 16
（mask `0x10000`）。四个 setter 没有自行检查这两个能力条件，应用应：

1. 检查 Granite CPU/PCI 身份、非虚拟机及精确 SMU `98.84.0`。
2. 查询并比较完整能力字，要求 bit 1 为 1、bit 16 为 0；提交前再次核对。
3. 初始化六个参数，只发送所选的一条设置命令，不自动改模式或切换接口。
4. 保留 `accepted` 与固件响应，明确 `verified=false`、当前值未知、
   回读不可用；错误或超时后不自动重试或回滚。

PPT 邻近 `0x26638` 和温度邻近 `0x26cac` 有读取状态的代码，
但本证明没有建立它们可从该 RSMU 接口安全到达的路由。已公开的
DC/DB 等命令读取 fuse，不是当前限制；不能据此补写后验证。
`6Ch → 0x26cc8` 读取另一状态地址 `0xa73c`，也不等于 `59h` 的
存储 `0x9f84`。这些 getter 不能凭邻近地址或类似数值开放。

## 可复现检查

`analysis/tools/firmware-granite-pbo-limits.py` 使用 REA/Ghidra 的研究
导出 `build/granite-study-42432173976/smu/smu-study.json`，逐条验证
指令原始字节，并执行有界合成状态检查：

| 检查 | 用例 | 被限幅 |
|---|---:|---:|
| 能力 getter 与权限 helper | 108 | 不适用 |
| PPT：cap、variant、特殊门、override 与系数 | 5184 | 2076 |
| TDC | 576 | 252 |
| EDC；其中 192 次触发刷新边界 | 576 | 252 |
| 温度；含整数精度边界 | 270 | 162 |

所有 setter 用例都响应 1，包括零值、被限幅及超过应用边界的合成输入。
这不要求应用接受这些输入，也不是建议在真机尝试。物理设备、真实 fuse、
外设锁、刷新副作用、当前限制读取与硬件稳定性均未执行。

```sh
python3 analysis/tools/firmware-granite-pbo-limits.py \
  --smu build/granite-study-input/smu-62-54-body.bin \
  --study build/granite-study-42432173976/smu/smu-study.json \
  --output build/granite23-pbo-limits.json
```
