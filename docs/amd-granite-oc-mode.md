# Granite Ridge 手动 OC 模式状态协议

Granite SMU `98.84.0` 的 `5Dh` / `5Eh` 控制固件的手动 OC 模式状态，
`6Fh` bit 16 读取同一个状态 byte。状态回读不测量实际频率、电压或硬件
限制。参考来源为 ASUS ProArt X870E-CREATOR WIFI BIOS 2503；目标
9950X3D 真机不可用，已安装 BIOS 未知。

SMU SHA256：
`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`。
REA/Ghidra 导出及逐条原字节检查与 [Scalar](amd-granite-pbo-scalar.md)
使用同一固定 body。固件 handler 与固定
[Zen5Settings](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings.cs)
继承的 [Zen4Settings 命令表](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen4Settings.cs)
一致；不自动套用其他同号命令。

| 证据 ID | 位置 | 已验证的状态含义 |
|---|---|---|
| GRO-01 | RSMU 表 `0x6fa4` | `5Dh → 0x2a688 / flags 0x101005`，`5Eh → 0x2a73c / flags 0x111005` |
| GRO-02 | `0x2c910(0)`、`0x1f36c` | 开启权限来自 fuse `0x2cd7`、平台条件及 suppression byte `0x13548`，对应 `6Fh` bit 0 |
| GRO-03 | `0x2a688` | 权限失败响应 FD，不修改模式或 feature 状态；成功写 byte `0xae68 = 1` 后响应 1 |
| GRO-04 | `0x2a73c` | 当前模式非零时清 byte `0xae68`；当前已关闭时直接响应 1；没有开启权限检查 |
| GRO-05 | `0x1f36c` | `0xae68 != 0` 产生 bit 16，与 bit 0 的可用性独立 |

开启时，若旧状态为零，以锁参数 `0x10000` 包围 `0x1d6fc(5Dh)`
跨处理器广播。它将 feature 字的部分位并入 pending / history 字，注册
callback `2Fh → 0x1eddc`，再设置模式状态。variant byte `0x8170 == 1`
时还清除 PPT 状态 byte `0x9e4f`、word `0x9e64` 及 byte `0xd0e7`。

`5Dh` 参数 bit 24 会额外改变 feature mask，因此应用只能提交全零
参数，不能把此位当作无需说明的默认选项。原状态已开启时，handler 仍
检查权限并注册回调；应用应把状态相同视为无须提交。

关闭时，将 pending feature 字合入恢复字并清 pending，调用 feature
政策刷新，锁内广播 `5Eh`，取消 callback `2Fh`，然后清模式状态。
variant 为 1 时，将 `0x9e4f` 与 `0xd0e7` 设置为 3。
关闭操作不要求 bit 0 可用；这使失去开启资格的已启用状态仍可显式关闭。
应用不应把开启权限检查错误地施加于关闭路径。

应用接入要求：严格 Granite 身份与精确 `98.84.0`；完整能力字快照比较；
显式开/关请求；仅开启时要求 bit 0；六参数全部为零。提交后重新检查
版本和能力字，确认 bit 16 为请求状态。错误、取消、超时或回读不一致
均不自动重试、恢复旧状态或改变其他参数。这里的 verified 只能指固件
模式状态回读一致，不能表示实际工作时钟、电压或稳定性已经验证。

## 原始指令实验

`analysis/tools/firmware-granite-oc-mode.py` 回放两个完整 handler、权限
helper、局部状态 helper 和能力 getter。锁、跨处理器广播、callback
注册/取消及 feature 政策刷新是显式边界，没有执行真实设备事务。

| 操作 | 组合用例 | 拒绝 | 跨处理器广播边界 |
|---|---:|---:|---:|
| 开启 | 648 | 540 | 36 |
| 关闭 | 648 | 0 | 432 |

覆盖平台条件、variant、fuse、suppression、旧模式 0/1/255、参数 bit 24
与无关位、不同 feature 字。读取前后 bit 16 与状态 byte 一致，其他能力
位保持不变；拒绝开启的用例没有 RAM 状态变化。此保持结论针对合成边界，
不保证真实跨处理器回调或外设没有其他副作用。

```sh
python3 analysis/tools/firmware-granite-oc-mode.py \
  --smu build/granite-study-input/smu-62-54-body.bin \
  --study build/granite-study-42432173976/smu/smu-study.json \
  --output build/granite23-oc-mode.json
```

本证明不验证实际 MP5 接受情况、物理运行状态、散热、电气范围或真机
稳定性；也不建立第二 rails 的独立设置协议。
