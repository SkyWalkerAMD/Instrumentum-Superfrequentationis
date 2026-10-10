# Intel core / cache offset 恢复

Intel Controls 内新增 Core / cache voltage 子页。它恢复 client OC mailbox 的显式查询、
单域 offset 修改和完整回读；不同时修改 core 与 cache，不清除目标电压、模式或 ratio。
只接受 GenuineIntel Family 6 Model B7（Raptor Lake-S）且 CPUID 声明 MSR 能力。
这个 profile 是按已取得证据限定的实现入口，不是 14900KS / 任意 Z790 BIOS 已通过真机验收的声明。
W790、W890、其它 Intel 型号、VF 点和逐核电压另有路径，本页不混用其编号。

## 来源

[固定来源记录](validation/intel-oc-sources.json)含原 ELF 中五个函数的完整静态指令/哈希、
官方开源仓库的固定 commit URL 和下载哈希。原函数此处为静态分析，不计作原指令动态执行。
`rd_offsetv_core`、`rd_offsetv_ring`、`justwroffset_voltage_390` 的 client 分支给出
MSR 0x150、命令 0x10/0x11 和 domain 0/2；原 `get_oc_support` 读取 MSR 0x194 bit20。

[coreboot OC mailbox](https://github.com/coreboot/coreboot/blob/main/src/drivers/intel/oc_mailbox/oc_mailbox.c)
核对 busy、返回状态、数据的有符号 11 位 offset / 12 位 target / mode / ratio 布局；
[公共 MSR 定义](https://github.com/coreboot/coreboot/blob/main/src/soc/intel/common/block/include/intelblocks/msr.h)
核对 lock 位。[Intel Raptor Lake-S FSP](https://github.com/intel/FSP/blob/master/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h)
明确区分 core/ring offset、adaptive/override 与运行时 undervolt protection。
FSP 的配置结构偏移不是 MSR 地址；没有用 FSP 数组位置推算任何硬件目标。
coreboot 在旧型号上的驱动使用也不作为 Raptor Lake-S 固件认证。

## 事务与错误处理

- 查询前读身份和 OC lock；查询本身写入 0x10 消息，不提交调压消息。构造/切页不访问硬件。
- 邮箱先等空闲，提交一次，再等完成。每次等待最多 100 次、间隔 1 ms，同时受整段事务期限/取消约束。
- 使用观察到 busy 清零的同一次读取作返回值，保留 bits39:32 固件状态。失败或超时不会被零值当作成功。
- offset 步长为 1000/1024 mV，按最近编码舍入；输入拒绝非有限值和超出表示范围的值，不截断回绕。
  界面提交前显示实际量化结果；编码边界不是建议电压或硬件保证。
- 应用前重新读取身份、锁和完整旧设置；旧值变化时停止。只替换 bits31:21，保留 bits20:0。
- 只对用户选择的一个域提交 0x11，之后用 0x10 回读全部 32 位；不一致、错误或超时均不显示成功。
  编码已经相同时只校验，不提交 0x11。写入失败不重发，不尝试自动回滚。
- 不修改 OC lock、undervolt protection 或 per-core override。固件可拒绝命令或静默忽略，二者由状态/回读识别。
- 互斥范围仍是本进程 HardwareService；其它程序/固件的并发访问不受此锁约束。

表格显示配置 offset、编码 target、mode、ratio、原始数据和锁状态。配置值不是传感器实测电压。
更换 CPU/domain、读取失败或应用未验证会清空旧结果；关闭页面取消尚未提交的后续操作。

## 验证范围

独立核心增加 10 组测试：穷举 2048 个 offset 编码、两个域的原消息格式、型号/能力拒绝、
每个查询和更新传输失败点、旧值/锁变更、保留位与其它域不变、无需写入的同值请求、
固件拒绝/忽略、busy 超时与取消。逻辑 CPU 使用 130，防止把高 CPU 编号截为旧掩码。
Qt 增加实际控件查询/清空、取消后提交、锁/错误与回读失败状态的回归。
云端结果必须记录本次代码提交，不能继承 UMC 版本的通过结论。
