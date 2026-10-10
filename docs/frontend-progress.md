# GUI / CLI 同步进度

两个入口共用 `gui/core`，Linux 硬件访问共用 HAL 与驱动。新增功能同时落到 GUI 页面、CLI 命令、
共同核心和两套入口测试；包以同一个 Git 提交标识交付。当前还原范围如下。

| 功能 | GUI | CLI | 共同边界 |
|---|---|---|---|
| AMD PStates | AMD PStates 页 | `amd-pstates` | Family 1Ah 定义读取，未恢复设置 |
| Intel RAPL / HWP / DTS | Power / performance | `intel-read` / `intel-set` | 已支持的型号与能力；设置返回提交结果 |
| Intel core/cache offset、最大倍率、目标电压与模式 | Core / cache voltage and ratio | `intel-oc-read` / `intel-oc-set` | B7；[本轮新增目标电压 + Adaptive/Override](intel-voltage-recovery.md)，锁、旧值与完整回读 |
| Intel V/F 点 | V/F points | `intel-vf-read` | B7，两域，单点/候选 1..15，查询 |
| AMD SMU | AMD tuning | `amd-smu-probe/read/send` | 现有身份/命令白名单与原始参数 |
| AMD 曲线读取 | AMD tuning 曲线查询 | `amd-curve-read` | Shimada；显式固件 CCD/core；不推导 mV |
| AMD 拓扑 | AMD tuning | `amd-topology` | CPUID 拓扑，不作为固件目标映射 |
| AMD UMC | AMD UMC | `amd-umc-read` | 212 字段、56 原始寄存器；无时序写入 |
| DMI / 传感器 / SPD | Memory / Motherboard | `inventory` / `spd-decode` | 读取现有系统节点与离线 SPD；不扫描未绑定设备 |

入口差异保留明确记录：GUI 有原始 MSR/MMIO/PCI 编辑与专用 UMC 快照导入；CLI 尚未暴露这些编辑命令，
CLI 的 JSON 也不能直接当作 GUI UMC 快照导入。GUI 使用图形授权，CLI 使用终端权限。

双方共同未完成：AMD PStates 写入、曲线设置与物理目标映射、完整 PBO 高层限制、
Intel VF 写入及 W790/W890 电压/逐核/fabric、Intel 内存训练时序、板级 PMIC/VRM/EC/时钟写入。
四台目标真机验收与 Secure Boot 实机签名部署也未完成。详见[还原状态](recovery-status.md)。
