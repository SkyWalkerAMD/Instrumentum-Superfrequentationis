# AMD PM 原版限制监控值

2026-10-26 更新：[逐核 / CCD 存储区](amd-pm-core-ccd.md)已由 MP5 原指令闭合边界并接入两端；PM 快照上限 1 MiB，公共显示字段由 24 项改为 22 项，撤回 VDDG 两项误映射。标签/单位/物理映射和双 rails 硬件写入仍待完成。以下保留此前阶段记录。

2026-10-18：GUI 与 CLI 同步增加原版 Shimada PM 公共监控区的 24 项值。
它们保留原版名称、字节偏移和 binary32 存储值，单位与物理目标尚未核实。
这部分不代表恢复了 PPT/TDC/EDC/PBO 设置，也不应作为 W、A、°C 或百分比使用。

## 使用与数据约定

GUI：AMD tuning → PM telemetry → Display → `Limit monitor (original mapping)`。
CLI 继续使用 `amd-pm-read` 或 `amd-pm-decode`，无需新增参数：

```sh
octool-cli amd-pm-decode --file pm.bin --version 0x730204 > pm.json
octool-cli amd-pm-decode --file pm.json
# 已安装兼容驱动时，也可读取其 sysfs 接口
octool-cli amd-pm-read
```

JSON 保留 `format: octool-amd-pm`、`schema: 1` 和已有的 24 项 `fields`，
新增 `original_limits` 数组及 `original_limits_units_verified: false`。
新数组每项的 `unit` 为 `unverified`，`clock` 为 false，`scaled_clock_mhz` 为 null。
手动 BCLK 修正只作用于已有时钟字段。名称中的 0/1 不能解释为 Linux CPU、CCD、插槽或确定的供电轨。

| 原版名称 | 相对表首的字节偏移 |
|---|---|
| Fast PPT Limit / Fast PPT Value | 0x00 / 0x04 |
| TDC0 Limit / TDC0 Current Value | 0x08 / 0x0c |
| TDC1 Limit / TDC1 Current Value | 0x10 / 0x14 |
| THM0 Limit / THM0 Current Value | 0x18 / 0x1c |
| THM1 Limit / THM1 Current Value | 0x20 / 0x24 |
| FIT Limit / FIT Current Value | 0x28 / 0x2c |
| VID0 Limit / VID0 Current Value | 0x30 / 0x34 |
| VID1 Limit / VID1 Current Value | 0x38 / 0x3c |
| EDC_LIMIT_0 / PEAK_TELEMETRY_CURRENT_0 | 0x1c8 / 0x1cc |
| PCC_LIMIT_0 / PCC_RESIDENCY_0 | 0x1d0 / 0x1d4 |
| EDC_LIMIT_1 / PEAK_TELEMETRY_CURRENT_1 | 0x1d8 / 0x1dc |
| PCC_LIMIT_1 / PCC_RESIDENCY_1 | 0x1e0 / 0x1e4 |

只接受[已有八个精确版本及对应长度](amd-pm-recovery.md#精确版本与显示字段)。未知版本、
错误表长、驱动元数据变化均不返回新旧字段。NaN/Inf 单字段标记 `-EDOM`、值为 null，
保留原始位和其它有效字段，CLI 返回 3。有限的零、负数、次正规数保留，不据此判断硬件正常。

GUI/CLI 相互导入，也兼容没有新增数组的旧 schema-1 快照。解码忽略输入中的显示值、单位、
验证标志和错误标签，只从版本与原始字节重算。原始字节本身没有可验证的表校验和。
64 KiB 文件、16 层 JSON、8192 节点的限制不变。

已有驱动读取依旧可能触发固件表传输；本次没有增加寄存器访问、原生 DMA、自动加载驱动或硬件设置。
GUI 切换 Display 只切换已有样本；改变输入、读入失败或元数据变化会清空两个视图，禁止复制旧样本。

## 原版证据与复现

- 原版 ELF SHA256：`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
- [固定指令与常量夹具](../analysis/fixtures/legacy-amd-pm-limits.json)：SHA256
  `9936d25c9a2ffb61def0035529223cea0710a04ffeb2a067c9356897880f3475`。
- `AMD_PM_LOG` 构造函数 `0x2c7910`（36961 字节）、`get_pm_patch(int)`
  `0x2e7360`（22281 字节）与 `floatFromBits(unsigned)`（9 字节）。每段代码有独立散列。
- [回放工具](../analysis/tools/legacy-amd-pm-limits.py)和[128 例报告](validation/legacy-amd-pm-limits.json)。

```sh
python3 -m pip install -r analysis/tools/requirements-emulation.txt
python3 analysis/tools/legacy-amd-pm-limits.py --help
python3 -m unittest discover -s analysis/tests -p test_amd_pm_limits.py -v
```

在 Unicorn 中执行原版构造函数和共同字段解析段。版本、拓扑、内存分配、C++ 字符串/向量、
固件/传输调用均由有界的合成接口代替；禁止执行 I/O、MSR、系统调用和真实 CPUID。
测试八个精确版本 × 八组模拟 CCD 数（1、2、4、8、10、12、14、16）× 两组浮点位模式，
核对原版生成的名称、偏移与值。总计 128 例，覆盖零、负零、负值、Inf、NaN、次正规数等。

回放将原对象校准完成标志设为 1；没有执行校准流程、固件传输或每核心解析。
这些合成拓扑输入只是分支测试，不证明实际拓扑、数据新鲜度或任何芯片支持情况。
原版其它监控组与公共来源存在偏移差异，本次没有将它们混入已验证的公共字段。
物理单位、轨身份、目标映射、剩余 PM 字段、调参协议与真机验收仍待完成。
