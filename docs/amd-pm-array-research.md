# AMD PM 数组后续研究

2026-10-20。以下结果尚未接入 GUI / CLI，不增加本批 128 个显示字段的数量。
它继续使用固定原 ELF 的构造函数和公共 `get_pm_patch` 指令回放，覆盖八个精确版本、
八种合成 CCD 输入及两种浮点图样，共 128 组；每组核对 92 个原始存储字。

| 原版名称向量 | 名称 | 存储排列 | 字节范围 |
|---|---|---|---|
| 对象 `418h` | `GMI_FULL_WIDTH`、`GMI_HALF_WIDTH`、`GMI_QUARTER_WIDTH` | 各 12 个 binary32，按名称顺序连续存储 | `294h..320h` |
| 对象 `448h` | `DIMM_POWER`、`DIMM_TEMP` | 各 24 个 binary32，按名称顺序连续存储 | `324h..3E0h` |
| 对象 `478h` | `PCIE_ALLOWED`、`PCIE_ACTIVE`、`PCIE_UP_THRESHOLD`、`PCIE_DOWN_THRESHOLD`、`PCIE_IOHC_BANDWIDTH[4]` | 前四项各一个，末项四个 binary32 | `3E4h..400h` |

原指令中，GMI 的内层循环每次推进 `30h` 字节，DIMM 推进 `60h`；
PCIe 末项单独读取四个值。实验核对了原构造的名称、向量长度、每次读取位置及输出位模式。
这些是数组槽位，尚不能证明对应的物理 GMI 链路、已安装 DIMM、PCIe 端口、启用状态或单位。
读取数量固定也不能据此推断机器实际装有 24 条内存。

[紧凑记录](validation/amd-pm-array-probe.json)保留每组输出摘要以及完整报告 SHA256。
原 ELF 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`；
夹具为 `9936d25c9a2ffb61def0035529223cea0710a04ffeb2a067c9356897880f3475`。
用户目录 `build/pm-array-probe.py`、`build/pm-array-probe.json` 保留可重跑的探针与完整结果。

传输、拓扑与 C++ 库边界是合成的，校准假定完成，执行停止在逐核路径之前。
后续需要继续核对原 GUI 的数组选择方式、固件缓冲区契约，以及导出容量与跨版本兼容。
当前不把数组的原始槽号转换为物理目标，不增加硬件访问或设置。
