# Intel BCLK 与最后一次 MRC 请求

GUI `Intel Controls → Clock information` 与 CLI 共用核心，只读 Raptor Lake-S
CPU family 6/model B7、主桥 8086:A700。当前实现不覆盖其它 Intel 型号。
此页补充时钟信息，不改变已有的 14 寄存器 / 43 字段内存时序快照。

## 使用

```sh
sudo octool-cli intel-clock-read --cpu 0 > clock.json
octool-cli intel-clock-decode --file clock.json
```

GUI 可读取一次、打开、复制和保存快照；两端支持相互导入完整 CLI 报告或独立快照。
离线解码不打开硬件，不需要 root、Qt 或图形桌面。输入必须是最多 64 KiB 的普通 JSON 文件。
CPU 是用于身份核对的逻辑 CPU 编号；此接口不接收控制器、通道或任意 MMIO 地址。

## 数据含义

依据 Intel [743846-001](https://cdrdv2-public.intel.com/743846/743846-001.pdf)
§3.3.42、3.3.43、3.3.51（202–204、209–210 页）。详细来源见
[来源记录](validation/intel-clock-sources.json)和[研究说明](intel-memory-frequency-research.md)。
三个寄存器位于共享 PCU 区域，地址为 MCHBAR 加偏移，不添加 MC/channel 步长。

| 信息 | 寄存器 / 位域 | 表示方式 |
| --- | --- | --- |
| 报告 BCLK | 64 位 5F60h / 31:0 | kHz；高 32 位保留，非零时不解释 |
| 最后一次 MRC Gear 请求 | 32 位 5E04h / 13:12 | 编码 0/1/2 对应倍率 1/2/4；3 保留 |
| MRC 参考时钟请求 | 5E04h / 11:8 | 编码 0/1 为 133.33/100 MHz 步进，其它保留 |
| MRC QCLK 比率 | 5E04h / 7:0 | 原始编码；0 为 PLL 关闭请求，1/2 保留 |
| MRC VDDQ TX 电压请求 | 5E04h / 26:17 | 5 mV 步进，显示 mV；不是实测电压 |
| MRC VDDQ TX IccMax 请求 | 5E04h / 30:27 | 4 位原始编码；手册示例与位宽矛盾，不换算安培 |
| 请求忙状态 | 32 位 5E00h / 31 | 忙时整次读取失败，不显示部分结果 |

手册明确指出，MRC 结束后 5E04h 仅代表最后一次请求，不代表当前内存频率。
连续两次相同读数也不能改变这一含义。Gear、PLL 关闭标志及电压均标为历史请求；
不将当前 BCLK 乘以历史倍率生成 MHz、MT/s 或时序 ns，也不报告实时内存频率。

## 错误与快照

成功读取有 3 次 CPUID 与 21 次只读 PCI/MMIO 请求：先核对 CPU、主桥类别、MCHBAR
启用位和基址，再读取两组 PCU 寄存器并重新核对上下文。读取失败、忙状态、数据或基址
变化会使整个快照失败；不自动重试、不启用 MCHBAR、不写入 BIOS 请求寄存器。
两次一致读取不是硬件原子快照；尚未通过目标真机验证。

完整捕获中的未知 BCLK/MRC 编码可以局部不可用，其原始字仍保留。BCLK 为零、低 32 位
全一或保留位非零时不作为有效频率。MRC 的保留位、Gear、参考时钟或比率不支持时，
整组 MRC 派生字段为空，独立的 BCLK 仍可用。任一寄存器全字全一时整次捕获失败。
GUI 在读取失败、导入失败或切换 CPU 后清空旧结果，禁用复制和保存。

快照 `octool-intel-clock-v1` 的 layout 固定为 `raptor-lake-s-743846-001`，必须包含三个
唯一的规范偏移；offset 为 8 位、raw 为 16 位十六进制字符串，避免 64 位 JSON 精度丢失。
导入只相信原始寄存器并重新推导数值，不信任文件中的派生数值、来源或在线标志。
导入后 `offline=true`，`origin_verified=false`、`atomic_snapshot_guaranteed=false`，
`current_memory_frequency_available=false`。JSON 分别报告 `bclk.error` 与
`last_mrc_request.error`，不可用数值为 null；完整捕获允许顶层成功而局部字段不可用。

自动化覆盖共享 PCU 地址、所有读取故障位置、保留编码、忙状态、数据变化、取消、
64 位精度、GUI/CLI 往返和界面生命周期。它不证明目标硬件、实时内存频率、其它型号、
BCLK 写入或时序/训练控制已经完成。
