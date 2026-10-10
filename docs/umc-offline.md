# UMC 离线快照与跨系统比较

GUI 与 CLI 共用 `gui/core/umc_capture.cpp`，对两种原有 JSON 格式重新解码
212 个字段。该路径不创建硬件后端，不要求 root、桌面、Qt 或内核模块（CLI）。
这补齐了“GUI 保存 → CLI 分析”方向；原来的“CLI 采集 → GUI 打开”继续可用。

```sh
# 在受支持机器上采集；本步访问硬件，需要相应权限。
sudo octool-cli amd-umc-read --cpu 0 --bank 0 --refresh-slot 0 > ubuntu.json

# 下列步骤完全离线，也能读取 GUI 的 Save snapshot 文件。
octool-cli amd-umc-decode --file ubuntu.json > decoded.json
octool-cli amd-umc-diff --file ubuntu.json --compare other-system.json > difference.json
```

`decoded.json` 可以再次导入 CLI 或 GUI。`difference.json` 是比较报告，不是可导入的
采集快照，也不能用于应用设置。字段名以 ID 区分，例如两个显示为 Tcl 的字段
仍分别为 ID 32 和 203。导入报告自带的 `fields` 不参与计算，以原始寄存器为准。

GUI 的 UMC 页先使用 Read once 或 Open snapshot，再选择 Compare snapshot。
默认只显示有变化或单边缺失的字段；取消 Only differences 可查看全部 212 行。
Clear comparison 保留当前快照。比较失败也保留当前快照，并清除旧比较结果；
更改硬件目标或重新读取会清空旧结果。Save snapshot 始终保存当前快照。

比较要求 bank 与 refresh slot 相同，CPU/PCI 编号可以不同以适应不同系统的编号。
**这些地址索引相同不代表同一物理通道**；文件来源、平台身份和采集真实性均未经验证。
比较结果保留原始编码，不把它们冒充时序周期、MHz 或电压，也不证明平台兼容性。

| CLI 状态 | 含义 |
| --- | --- |
| `unchanged` | 两边都有数据，原始字与字段值均相同 |
| `changed` | 字段编码发生变化，`encoded_delta` 为 after − before |
| `raw_only` | 同一寄存器其他位变化，此字段编码相同 |
| `missing_before` / `missing_after` | 一侧缺少对应寄存器；空值和差值为 null |
| `missing_both` | 两侧均缺失，不记为值相同 |

`counts` 统计字段数，合计 212。字段共享一个寄存器时，原始字变化可能影响多行。
比较成功的退出码为 0，即使发现差异；参数错误为 2，文件、格式或目标不匹配为 3。
不匹配时不发布 `fields`，不输出可以误读为成功的部分比较。

输入仅接受普通文件，最多 65536 字节 UTF-8（允许开头的 UTF-8 BOM），最多 16 层嵌套、
8192 个 JSON 值。拒绝所有层级的重复键、无效 Unicode、重复/未知寄存器、越界和非整数
数字；数值使用精确十进制检查，不经浮点舍入。兼容整数形式的 `.0` 和科学计数法。
GUI v1 及成功的 CLI decode 报告允许 1～56 个寄存器，缺失字段保留空值；成功的 CLI
read 报告必须有 56 个不同寄存器、CPU 和 domain 0 的有效 PCI 目标。不完整采集不能
作为成功读取导入。GUI 导入自身格式时保留其原始附加元数据；CLI 报告转存为 GUI 格式
时保留 bank/slot/CPU/PCI/原始字，并明确标注来源未验证。

验证覆盖核心解析器的截断、逐字节 NUL 注入、Unicode、重复键、资源上限、精确数字、
16 种 refresh slot、六种比较状态；CLI 检查实际进程双向导入、文件边界、FIFO 拒绝和
零硬件后端创建；GUI 检查比较、筛选、导出保留、失败清理及两种输入格式。
测试使用合成快照，不是物理内存时序或调参验收。
