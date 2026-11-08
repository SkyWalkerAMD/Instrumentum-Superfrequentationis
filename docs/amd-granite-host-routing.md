# Granite mailbox 路由与第二 rails 的证据边界

本轮找到此前 `0x330b0` guarded SVI register writer 的固件表入口：
它是 **mailbox 0 的 `70h`**，不是 RSMU `70h`。RSMU `70h` 仍是
已还原的 Fmax 设置。这个路由区分解决了入口归属，但没有确定 channel
1/2 对应的物理 rail、独立电压回读或安全写入参数，因此不开放新的
rails 写入。

参考固件为 ASUS ProArt X870E-CREATOR WIFI BIOS 2503，SMU 98.84.0。
SHA256：`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`。
目标 9950X3D 不可进行真机测试，已安装 BIOS 未知。

## 原始分发器与表身份

**GHR-01：** `0x136c` 调用 `0x14b8(mailbox)` 读取消息号。`0x14b8`
从 `0x6abc + 12*mailbox + 8` 取得消息寄存器地址；mailbox 27 是另一路
特殊编码。实验覆盖本页使用的普通 mailbox，未借特殊路径扩展主机权限。

**GHR-02：** 分发器从 literal `0xc20 = 0x7d28` 出发，读取
`base + 4*mailbox + 0x1c4` 的命令表指针和 `+0x234` 的命令数量。
因此指针数组在 `0x7eec`，数量数组在 `0x7f5c`。
消息号必须小于数量，且对应 handler 不为零，否则响应 FE。

| mailbox | 表 / command 数量 | 参数 / 响应 / 消息：固件局部坐标 | 本次确认的区别 |
|---|---|---|---|
| 0 | `0x6c0c / 0x73` | `0x30109c4 / 0x301057c / 0x3010530` | 70h 指向 `0x330b0` |
| 1 | `0x6fa4 / 0xe9` | `0x3010a40 / 0x3010570 / 0x3010524` | 当前产品使用的 RSMU 路径；70h 为 Fmax |
| 2 | `0x6fa4 / 0xe9` | `0x3010a60 / 0x3010574 / 0x3010528` | 同表，参数及寄存器坐标不同 |
| 5 | `0x7ab4 / 0x1d` | `0x3010988 / 0x3010984 / 0x3010508` | 1Bh 指向邻近温度状态 getter `0x26cac` |

mailbox 0 的寄存器与固定
[Zen4Settings 的 MP1 定义](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen4Settings.cs)
相符，该表由 [Zen5Settings 继承](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings.cs)。
上表保留固件局部地址；并不证明所有 mailbox 都可经当前应用的主机后端
访问，也不允许自动切换接口重试。

**GHR-03：** RSMU 1/2 的有效消息号仅为 `00h..E8h`。后续相邻表中的
指针，不能通过减去 `0x6fa4` 就命名为更大的 RSMU 消息。
例如 `0x7b8c` 中的 `0x26cac` 实际是 mailbox 5 / `1Bh`；把它称作
RSMU `17Dh` 会在分发器范围检查处被拒绝。该 getter 还未获得可用于
产品的主机传输合同，所以 [PBO limits](amd-granite-pbo-limits.md)
仍明确没有当前限制回读。

## 对 rails 还原的实际意义

**GHR-04：** mailbox 0 / `70h` 表项为 `0x330b0, flags=6`。
已有[guarded writer 证明](amd-granite-rail-routing.md)建立了 channel
小于 3、register `20h..2Ch` 排除 `22h`、global byte `0xc5aa` 等
检查；成功路径调用 `0x32cbc`，部分 register 20h 写入还更新局部缓存。
新证据为该函数补上了分发器表归属。

这不是所需的第二 rails 电压设置与回读合同：

- host 表入口没有证明 channel 的物理 CPU/SOC/MISC 身份。
- register 写入、包编码、完成响应及软件缓存均不是独立电压测量。
- 启动时的 `0x32d38` 局部状态读取仍不能据此冒充可供主机调用的独立
  getter；其作用和 freshness 限制见已有 rail-routing 研究。
- 本次只执行到分发表选择与 flag 提取；任务排队、间接 handler 调用、
  调度 gate、外设权限和设备响应没有被本实验执行。

## 可复现检查

`analysis/tools/firmware-granite-host-routing.py` 使用 REA/Ghidra
`build/granite-study-42432173976/smu/smu-study.json`，逐条验证原字节。
它对初始固件的全部 28 个 mailbox 描述符执行有界分发表选择：
703 个非空表项、96 个空项拒绝、84 个越界拒绝，另有 32 个原始
mailbox 读取用例。原始描述符是显式初始镜像输入，不声称排除了运行时
RAM 对表指针或数量的修改。

```sh
python3 analysis/tools/firmware-granite-host-routing.py \
  --smu build/granite-study-input/smu-62-54-body.bin \
  --study build/granite-study-42432173976/smu/smu-study.json \
  --output build/granite23-host-routing.json
```

未执行硬件 mailbox、队列、SVI 设备读写；没有扩大应用写入允许列表。
