# Intel Ring DownBin 恢复

GUI `Intel Controls → TVB & Ring DownBin` 与 CLI 共用配置事务，支持读取和单独设置
Intel Family 6 / Model `B7` 的 Ring DownBin。启用后，处理器可自动降低 Ring 倍频；
配置回读成功不代表实际 Ring 频率已改变。当前仍要求裸机、MSR 功能和固件成功应答，设置时检查 OC lock。

## 使用

从 `octool-cli diagnose` 的 `allowed_cpus` 选择逻辑 CPU；以下 `0` 仅为接口示例。

```sh
octool-cli intel-tvb-read --cpu 0
octool-cli intel-tvb-set --cpu 0 --field ring-down-bin --value 1 --apply
```

`ring_down_bin_enabled` 为配置状态；读取失败时为 null。`1` 启用 Ring DownBin，`0` 禁用。
原有 `intel-tvb-read/set` 命令名和两项 TVB 字段保持兼容，因为三个字段共用一个配置字。
GUI 增加独立行和编辑选项，确认时显示目标 CPU、旧状态和新状态。
这是共享处理器配置，逻辑 CPU 只用于访问，不是逐物理核心设置。
禁用 Ring DownBin 可能增加 CPU 电压；确认框对此作出说明。

## 证据与实现

[Intel Raptor Lake-S FSP 定义](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h#L1595-L1600)
描述了 Ring DownBin 开关及 Ring/Core 倍频关系。FSP 结构偏移不是硬件地址。
[华硕 Z790 BIOS 手册](https://dlcdnets.asus.com/pub/ASUS/mb/LGA1700/ROG_MAXIMUS_Z790_DARK_HERO/E22758_ROG_Z790_Series_BIOS_Manual_Intel_14th_EM_WEB.pdf)
第 55 页说明它控制自动 Ring 降倍率，使用 OC mailbox 设置命令 `0x19`，并提示禁用可能增加电压。
这两份来源不提供本实现的 data 位位置。

原版 ELF 的 `disable_ringdownbin(bool)`（`0x37FD80`，199 字节）先向 MSR `0x150`
提交 `0x18` 查询，再修改 **data bit 1**，以 `0x19` 设置。原版参数 true 表示禁用；
新接口的 enabled=1 对应清除此位，enabled=0 对应设置此位，其余 31 位保持。
源码：[核心](../gui/core/intel_oc.cpp)、[固定原版指令](../analysis/fixtures/legacy-intel-misc-ratio.json)、
[隔离仿真](../analysis/tools/legacy-intel-misc-ratio.py)、[来源记录](validation/intel-ring-downbin-sources.json)。

事务复用 [TVB 配置](intel-tvb.md) 的身份/OC lock 检查、有界 busy 轮询、完整 8 位固件状态、
整个旧配置比较以及完整回读。相同值跳过设置；失败不重试、不自动回滚。
不改变 Ring 最小/最大倍频寄存器 `0x620`，也不改变 TVB 温度表。

## Vmax 研究边界

同时固定并仿真原版 `disable_vmax(bool)`（`0x37FF10`，189 字节），确认其参数 true
设置同一配置字的 bit 4，false 清除此位。两项原版 setter 共 156 个固定场景，另测试全部低 5 位组合。
原版查询循环没有截止时间，不处理固件/传输失败，两个 setter 均不检查设置完成或独立回读。
生产代码使用已建立的有界事务，不照搬这些失败行为。

[Intel Comet Lake FSP](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/CometLakeFspBinPkg/CometLakeS/Include/FspmUpd.h#L1126-L1131)
说明了 Vmax Stress 的电压相关降频含义，但这是较早平台的定义。
已核对的 Raptor Lake-S FSP 头文件和 Z790 手册没有同名说明；这不足以证明 B7 不支持它，
也不足以确认 B7 的完整运行语义。本轮仍保留 bit 4，CLI 拒绝 `vmax` / `vmax-stress` 设置。
增强 TVB、其它 Intel 型号、板级调节等剩余功能继续保留在未完成范围。

生产提交 `c6e9599` 的 39 项云端任务全部通过；见 [验证交付](intel-ring-validation.md)和[完整证据](validation/intel-ring-ci-c6e9599.json)。所有合成测试与跨发行版安装均不能替代目标真机调节验收。
