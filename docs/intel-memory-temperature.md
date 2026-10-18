# Intel DDR4 温度等级与刷新要求

GUI 的 Intel Controls → Memory timings 和 CLI `intel-memory-read` /
`intel-memory-decode` 共用 v6 核心，增加四个已占用 Rank 的 DDR4 MPR 温度等级。
当前限定裸机 Raptor Lake-S CPU 06_B7、主桥 8086:A700；共 22 个寄存器、83 个字段。

依据 Intel 官方 [743846-001](https://cdrdv2-public.intel.com/743846/743846-001.pdf)
§3.2.88，已逐页查看第 161–162 页的类别说明和位域表。
PDF SHA256：`714e415546d36e2d1bc8832bf94db817b48085224d54bebe252b16e1e48081f7`。

## 字段与含义

MCHBAR + E428h 为 32 位，复位值 01010101h。字段名
`DDR4_temperature_rank0` 至 `DDR4_temperature_rank3` 分别使用位 1:0、9:8、17:16、25:24。
它们反映控制器保存的 MPR 温度类别。程序不会触发新的 MPR 读取，也无法证明固件何时更新了该值。

| 编码 | 类别 | 温度区间 | 手册要求的刷新倍率 |
|---|---|---|---|
| 0 | cold | 低于 45°C | 1× |
| 1 | normal | 45–85°C | 1× |
| 2 | hot | 高于 85°C | 2× |
| 3 | 保留 | 未知 | 未知 |

类别不会换算成某个精确摄氏度读数。刷新倍率表示该类别对应的要求，不能据此判断
控制器当前采用的刷新率。零编码有效；复位值 normal 不能证明传感器已完成采样。
JSON 明确给出 `temperature_freshness_verified: false`。

GUI 显示类别、温度区间与刷新要求；JSON 保存 `encoded`、`unit: temperature_class`、
`temperature_class` 和 `required_refresh_multiplier`。Gear 覆盖标志不改变温度类别。
保留编码只让对应字段返回 ENODATA，其余时序和原始寄存器仍可查看。
Rank 未占用返回 ENODEV；DDR5 返回 ENOTSUP。这三种情况的派生值、单位、类别和倍率均为 null。
GUI 分别显示 `Reserved code`、`Rank absent` 和 `Not used`。

## 读取和快照

```sh
sudo octool-cli intel-memory-read --cpu 0 --controller 0 --channel 0 > memory.json
octool-cli intel-memory-decode --file memory.json
```

选择进程可用的逻辑 CPU；DDR4 每个控制器只使用 channel 0。
MC1 偏移仍为 +10000h；不将 Rank 编号解释为主板 DIMM 标签。
两次完整采样加三次主桥/MCHBAR 上下文检查，共 59 次寄存器读取、3 次 CPUID。
任何读取失败、两次采样变化或中途取消都会丢弃整份结果；一致采样不等于硬件原子快照。
整字全 1 和超过 32 位的数据依旧拒绝；温度字段之外的保留位不参与解码且完整保留。
没有新增写寄存器、训练、轮询、内核接口或权限要求。

新快照为 `octool-intel-memory-v6`。v1–v5 保留原始字段集和版本，不补读缺少的温度数据。
导入必须包含对应版本的完整寄存器集合，从原始字重新计算类别、错误和刷新要求；
外部声称的类别、倍率或新鲜度不能覆盖解析结果。GUI 与 CLI 双向互通，离线解码不创建硬件后端。

## 尚未恢复的部分

E424h 的 MR4 存储区有技术相关编码及 DDR5 拆分 Rank 的特殊映射，公开表不足以在本批
确定温度换算和物理 Rank 关系，因此未加入。实时温度新鲜度、其他 Intel 型号、
板级传感器映射、主动刷新策略修改、内存训练和目标真机验收仍未完成。

测试使用独立合成字，覆盖四种编码、全部非空 Rank 掩码、DDR4/5、Gear 覆盖、
保留位、六版快照互通与篡改重算、故障/变化/取消以及 GUI 旧值清除。
自动回归不替代目标机器的温度与刷新行为核对。
