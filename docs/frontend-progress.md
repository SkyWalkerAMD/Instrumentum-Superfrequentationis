# GUI / CLI 同步进度

两个入口共用 `gui/core`，Linux 硬件访问共用 HAL 与驱动。新增功能同时落到 GUI 页面、CLI 命令、
共同核心和两套入口测试；包以同一个 Git 提交标识交付。当前还原范围如下。

| 功能 | GUI | CLI | 共同边界 |
|---|---|---|---|
| AMD PStates | AMD PStates 页 | `amd-pstates` | Family 1Ah 定义读取，未恢复设置 |
| Intel RAPL / HWP / DTS | Power / performance | `intel-read` / `intel-set` | 型号/能力门控；含 HWP 活动窗口，13 项设置完整回读 |
| Intel core/cache offset、最大倍率、目标电压与模式 | Core / cache voltage and ratio | `intel-oc-read` / `intel-oc-set` | B7；[目标电压 + Adaptive/Override](intel-voltage-recovery.md)，锁、旧值与完整回读 |
| Intel V/F 点 | V/F points 查询、准备与设置 | `intel-vf-read` / `intel-vf-set` | B7，两域，候选 1..15 查询及单点 offset；[完整上下文核对](intel-vf-write-recovery.md)，不改变 override 模式 |
| Intel 睿频分组 | Turbo ratio groups | `intel-turbo-read` / `intel-turbo-set` | B7；P/E 两表，读取活动核心阈值，单组倍率设置与完整读回；不改阈值 |
| AMD SMU | AMD tuning | `amd-smu-probe/read/send` | 现有身份/命令白名单与原始参数 |
| AMD 曲线读取 | AMD tuning 曲线查询 | `amd-curve-read` | Shimada；显式固件 CCD/core；不推导 mV |
| AMD 拓扑 | AMD tuning | `amd-topology` | CPUID 拓扑，不作为固件目标映射 |
| AMD UMC | AMD UMC，打开/比较快照与差异筛选 | `amd-umc-read/decode/diff` | 212 字段、56 原始寄存器；GUI/CLI 双向离线导入与共用比较；无时序写入 |
| DMI / 传感器 / SPD | Memory / Motherboard | `inventory` / `spd-decode` | 读取现有系统节点与离线 SPD；不扫描未绑定设备 |
| 原始 MSR / MMIO / PCI | Register 编辑页 | `register-read` / `register-write` | 相同宽度、范围与对齐校验；明确目标，写入不自动回读 |

GUI 使用图形授权，CLI 使用终端权限。UMC 的 GUI/CLI 快照和 CLI decode 报告可双向导入，
两端支持同 bank/slot 的离线比较，区分字段变化、原始字变化与数据缺失。
[UMC 离线说明](umc-offline.md)记录格式、文件边界和比较语义。

双方共同未完成：AMD PStates 写入、曲线设置与物理目标映射、完整 PBO 高层限制、
Intel 其它型号的 VF 及 W790/W890 电压/逐核/fabric、Intel 内存训练时序、板级 PMIC/VRM/EC/时钟写入。
四台目标真机验收与 Secure Boot 实机签名部署也未完成。详见[还原状态](recovery-status.md)。
