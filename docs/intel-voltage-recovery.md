# Intel 目标电压与模式还原

GUI 与独立 CLI 已共同实现 core/cache **目标电压 + Adaptive/Override 模式设置**。
入口仍限于 GenuineIntel family 6/model B7、具备 MSR 能力的 Raptor Lake-S client profile。
配置值不等于实测电压；固件可拒绝、限制或忽略请求。

GUI：Intel Controls → Core / cache voltage and ratio，先读取，再填写目标 mV 并明确选择模式。
输入无预设，切换 CPU/域或重新读取会清空待提交值。确认窗口显示取整后的目标和模式。

CLI 接口（`N`、`MILLIVOLTS` 是需自行填写的参数，不是推荐设置）：

```text
octool-cli intel-oc-set --cpu N --domain core|cache --field target-mv \
  --value MILLIVOLTS --mode adaptive|override --apply
```

接受整数 **1..2000 mV**，必须明确提供 `--mode` 与 `--apply`。
`--mode` 与 offset/ratio 字段组合会被拒绝，输入校验发生在打开硬件之前。
零值的运行时自动/默认语义尚未证实，因此没有开放。
Adaptive 配置 turbo 目标，Override 配置固定目标；二者都受 BIOS 与 CPU 限制。
这个输入范围来自配置定义，不是安全工作电压范围。

`intel-oc-read` 以及设置的 `before` / `after` 增加 `target_mv`、`target_mode`，失败为 `null`。
JSON schema 仍为 1，属于新增字段；`requested_mode` 仅随目标电压设置返回。
例如测试输入 1234 mV 会编码为 1234.375 mV，输出保留该差异。

## 证据与原版行为

固定原 ELF 的完整 Adaptive、cache Override 和字节组合函数已执行 82 个合成场景；
核心手动设置函数另外保留静态指令证据，其 CPU mask 映射没有宣称完成仿真。
函数字节、原数据常数和来源哈希见[证据清单](validation/intel-voltage-sources.json)，
[静态证据](validation/intel-voltage-static.json)与[执行记录](validation/intel-voltage-local.json)。
仿真只替换 MSR 和延时边界，没有运行 OS、驱动或真实硬件指令。

发现的原版行为：

- Adaptive 同时提交 core、cache 两域，保留 offset 与倍率。
- cache Override 重新组合数据；正常正数目标会清掉原 offset。
- 原换算常数为 `0.9766129`，随后截断；1000 mV 会得到编码 1023。
- 查询返回错误或 MSR 包装函数失败也继续修改，busy 循环无内部上限，没有完整回读校验。

[coreboot 固定版本](https://github.com/coreboot/coreboot/blob/cdd0aaaa8e0c2e52a57b515e801b339549e53283/src/drivers/intel/oc_mailbox/oc_mailbox.c)
定义 target 为 data[19:8]、每步 1/1024 V，mode 为 bit 20，Adaptive=0、Override=1。
[Intel Raptor Lake-S FSP 固定版本](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h)
分别定义 core/ring 模式与 0..2000 mV 目标范围。FSP 是启动配置结构，不是运行时寄存器地址；
coreboot 的定义与原版指令一起支持协议恢复，但不构成目标机器固件验收。

## 新实现与边界

共享核心按精确步长就近取整，**仅替换 data[20:8]，保留 offset[31:21] 与 ratio[7:0]**。
每次只修改明确选中的域，不复制原版跨域副作用或丢失 offset 的行为。
复用既有 OC 事务：快照有效性 → 身份/锁重查 → 完整旧值比较 → 单次提交 → 状态检查 → 完整回读。
同值不提交设置命令；失败不自动重试或回滚。`verified` 只说明寄存器回读一致。

新增核心测试覆盖全部 1..2000 输入、两模式两域、其它字段保留、重复请求、所有传输失败位置、
32 个回读位逐一损坏、身份/锁/旧值变化、取消和超时。GUI、CLI 分别测试输入、取消/确认、
错误呈现、模式选择及重复提交；CLI 还验证 JSON 中的取整值和错误空值。

V/F 点写入、逐核模式与 Xeon W790/W890 电压域仍未开放。
事务锁只覆盖单个进程；不能同时运行 GUI、CLI 或其它调参程序操作同一硬件邮箱。
四台目标真机尚未验收，本轮合成与云端测试不代表实际电压或稳定性验证。
