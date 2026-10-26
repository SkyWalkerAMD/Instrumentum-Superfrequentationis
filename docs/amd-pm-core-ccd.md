# 逐核 / CCD 功耗表存储区

GUI 和 CLI 现在共用 SMU/MP5 固件证据支持的数组解码，覆盖八个精确 `73xx04h` 版本。
恢复的是原始字、浮点存储值、行号和槽号；槽号不等于 Linux CPU 号，活动核心、物理 CCD
映射和真实功耗单位仍未验收。沿用的原版标签加 `Legacy` 前缀，其语义尚未独立认证。

## 布局

固件公共区固定 `634h` 字节。N 为该精确版本的存储宽度，依次为 2、4、6、8、10、12、14、16；
它来自固件的版本/长度选择和初始化参数，不从主机线程数推导。

| 区域 | 行数 | 每行槽数 | 第一字节 |
|---|---:|---:|---|
| Core 存储区 | 18 | 8 × N | `634h` |
| CCD 存储区 | 13 | N | `634h + 240h × N` |

每槽是小端 binary32，总长度精确为 `634h + 274h × N`。最大版本新增 2304 个 core 值和
208 个 CCD 值。原公共显示中的 `714h/71Ch` 实际落在 core 数组内，已撤销其
`CLDO_VDDG_IOD/CCD` 电压解释，保留到数组的原始字中。
旧公共显示余下 218 项；加上新数组，每份捕获有 532–2730 个存储字段。

第一至十五个 core 行、第一至十一个 CCD 行保留原程序的候选名称；其余五行仅标数字。
所有新数组都使用 `unit=unverified`，不应用 BCLK 缩放，不根据零值推断禁用核心。
非有限值保留原始位、显示不可用，其它槽继续解码；任何错误不会补零或扩大读取长度。

## 使用

GUI：AMD tuning → PM telemetry → 读取或打开捕获 → Display 中选择
`Core stored arrays (18 rows)` / `CCD stored arrays (13 rows)`。

CLI 使用既有命令，结果新增 `firmware_core_arrays`、`firmware_ccd_arrays` 和
`firmware_ccd_storage_slots`：

```sh
octool-cli amd-pm-decode --file pm.bin --version 0x731004
octool-cli amd-pm-read > pm.json
octool-cli amd-pm-native-read --cpu 0 > native-pm.json
```

三个来源采用同一解码器。GUI / CLI 快照互通，导入时只信任原始字节和精确版本，
重新计算所有数组和验证标志，不能通过修改 JSON 中的标签或目标认证字段伪造结果。
PM JSON 上限调整为 1 MiB、65536 个值、16 层嵌套；其它捕获格式保留原来的限制。
旧 schema-1 捕获仍可导入，其显示值会按新证据重算。

`amd-pm-layout-audit` 保留用于复现旧默认构造函数的错误，不代表当前解码器的布局。

## 独立证据

[ASUS 固件研究](amd-smu-firmware-research.md)固定主 SMU 和配套 MP5 的输入哈希。
MP5 `37DD2h–37E52h` 的整数指令执行三个循环：18 × 8 个 core 字、11 个 CCD 字、再 2 个 CCD 字。
目标地址的行步长使用初始化参数 N，CCD 序号只选择各行内的存储位置。

[研究脚本](../analysis/tools/firmware-pm-copy.py)逐条核对导出指令与固件原字节后，在合成 RAM 中
回放整数复制片段。八种宽度共 72 个序号案例覆盖完整范围，无重复写、空洞或越界；
首尾偏移与八组固件长度相符。实验不执行浮点归一化，也不证明单位、物理映射或真机效果。
Ghidra 伪代码曾省略零开销循环，因此没有仅凭伪代码实现新布局。
