# Intel 电压 / VF 剩余路径核对

后续已接入[client core/cache offset 与回读](intel-oc-recovery.md)，下文保留接入前的静态研究。
W790/W890、VF 点与逐核 override 的剩余限制仍然适用。

[证据文件](validation/intel-oc-followup.json)保存固定原 ELF 中六个完整函数的静态指令、代码哈希，
以及 coreboot 官方源码的取得日期和内容哈希。这次没有执行真实 Intel 电压写入，也没有把静态分析
计入新 UMC / 拓扑的核心测试数。

原 `rd_offsetv_core` / `rd_offsetv_ring` 的通用分支使用 MSR 0x150，分别提交高 DWORD
0x80000010 / 0x80000210。`justwroffset_voltage_390` 的通用分支并非只改一个域：
先读写 core，再读写 ring，使用命令 0x10/0x11。

在原地址 0x38e4d8 / 0x38e558，读取数据只保留低 8 位，再加入偏移及符号位。
这会丢弃原低 DWORD bits20:8。
[coreboot 的 OC mailbox 实现](https://github.com/coreboot/coreboot/blob/main/src/drivers/intel/oc_mailbox/oc_mailbox.c)
将这些位定义为 12 位目标电压和 1 位 adaptive/override 模式。
因此不能把该旧函数直接包装成“只改 offset”的新功能；新实现需要保持其它字段并独立校验回读。

同一来源明确区分低 DWORD 的数据和高 DWORD 的命令接口：偏移为有符号 11 位定点电压，
分辨率 1/1024 V；接口 busy 在 bit63，完成状态在 bits39:32。
这些接口定义不是所有 CPU 型号都支持某个电压域或命令的证明。

原 VF 写入还会调用 `percoreoverride_dis`；不能把该附带设置当作无副作用的参数准备。
GNR 路径从 `GNR_SP_MEM_CFG` 的目标表取 13 位选择器，装入接口的另一个位域；
NVL 又使用自己的对象表和按核心函数。它们不能套用 client 的 core/ring 域编号，
也不能凭 W790/W890 板名共用一套地址。

后续实现需分别绑定 client、SPR/W790、GNR/W890 的型号、目标表来源与返回状态；
必须给邮箱忙等待设置期限，保留传输错误，提交后不盲目重试，不用无条件成功提示。
当前 Intel Controls 的已验证范围仍为 RAPL/HWP/DTS；此记录没有将电压 / VF 标为已完成。
