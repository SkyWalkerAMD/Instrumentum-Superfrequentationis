# Shimada LN2 状态与逐核性能查询

GUI / CLI 共用 `readShimadaPboQuery`，限定 AMD family `1Ah` / model `08h`、
PCI `1022:153a`、域 0 的 `00:00.0`。仅提交查询；邮箱传输需要 PCI 写入，
但不发送电压、曲线、模式、频率设置，也不访问主板 EC。

```sh
octool-cli amd-ln2-read --cpu 0
octool-cli amd-core-performance-read --cpu 0 --firmware-index 7
```

`--cpu` 是执行身份检查的逻辑 CPU。`--firmware-index` 是显式固件索引，
本程序只接受 `0..127`；这是应用边界，不证明对应核心存在，不能把 Linux CPU 编号直接当成它。
GUI 在 AMD tuning 的 Shimada firmware query 行选择查询；性能查询需填写索引，
点击 Read firmware state。更换目标、查询或索引会清除旧结果，异步期间变化的结果不显示。

| 查询 | 固件消息 | 返回解释 |
|---|---|---|
| LN2 mode | RSMU `A6`，六参数为零 | 只有原始字 `0/1` 解码为关闭/开启；其它字保留供诊断并报格式错误，状态为未知 |
| Per-core performance word | RSMU `5C`，arg0 为索引，其余为零 | 保留不透明 32 位原始字；不赋予功耗、频率、排名、物理核心映射或 NaN 含义 |

每次查询检查平台、等待邮箱空闲、初始化六参数、提交一次、核对 response=1、读取 arg0，
再检查平台与取消/超时状态。传输或身份复查失败不发布原始字和解码值。
LN2 的未知编码与访问错误分开：前者 `raw_valid=true`、`valid=false`、`ln2_enabled=null`；
后者原始字为空。性能返回字即使为 `FFFFFFFF` 也保持原样，不虚构其特殊含义。
进程内互斥不能排除其它程序或固件并发访问。

命令和输入独立核对固定的 ZenStates-Core 提交
[`869b7e8`](https://github.com/irusanov/ZenStates-Core/tree/869b7e82572337e5a673e92230edc92bddf0e44b)：
[Shimada 配置](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)、
[LN2 查询](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/GetLN2Mode.cs)、
[逐核查询](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/GetCorePerformanceData.cs)。
`Cpu.cs` 的 LN2 包装将返回字与 1 比较；新实现另外要求已收到成功响应，且不把未知字解释为关闭。

这两个接口不等同于原版 EC `ln2_en()`、逐核/CCD 功耗表或 rails 回读。
逐核/CCD PM 布局、双 rails 写入以及真实硬件验收仍未完成。
