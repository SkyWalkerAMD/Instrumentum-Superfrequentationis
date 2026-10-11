# Intel 睿频分组活动核心阈值

GUI Intel Controls → Turbo ratio groups 新增 `Active-core threshold` 编辑项，
CLI 在已有命令中新增可选 `--field active-cores`。省略 `--field` 仍表示倍率，
也可显式写 `--field ratio`。两端共用核心的输入判断与事务，保持 B7 型号范围。

```text
octool-cli intel-turbo-set --cpu N --core-type p --group G --field active-cores --value COUNT --apply
octool-cli intel-turbo-set --cpu N --core-type e --group G --field active-cores --value COUNT --apply
```

只修改已有活动组中的一个中间阈值，输入为 1..255 整数，必须严格大于前一活动组、
小于后一活动组；第一组只要求大于零。最后一个活动组的阈值保持原值，不能启用或
禁用组，不能在一次调用中同时改倍率和阈值。这里的组编号不是物理核心 ID。
保持最后一项可保留原表的覆盖范围，但不能证明原表正确覆盖了实际 SKU。

原表必须满足非零阈值严格递增、对应倍率非零且不递增；未使用组的字节完整保留。
提交前重读 CPU 身份、混合架构能力、编程许可、锁以及两张完整表，检查任意旧位变化。
写入层再核对目标表旧值；只向 1AEh（P）或 651h（E）写入一次，随后重新核对上下文、
全部倍率与全部阈值。忽略、钳位、其它位变化、超时或传输错误均不报告验证成功。
相同值也重新检查，但不写入。失败不重试或回滚；GUI 清除旧编辑快照。

CLI JSON 新增 `field`，阈值请求输出 `requested_active_cores`；`expected_raw` 对应
本次选中字段所在的完整寄存器。旧的倍率请求仍输出 `requested_ratio`。
逐组 `editable` 继续表示倍率可编辑，新增 `threshold_editable` 表示阈值可编辑。
`verified` 仅代表配置完整读回，不代表实际频率、功耗或稳定性测量。
进程事务不能阻止其它工具或内核在读取和写入之间访问这些寄存器。

依据沿用[原程序 writer/reader 与 Intel FSP 成对数组](intel-turbo-recovery.md#原程序与公开资料)、
[固定来源哈希](validation/intel-turbo-sources.json)和 520 组原指令实验。
Intel Raptor Lake-S FSP 的八项 Ratio/NumCore 成对描述与原程序独立数量表 writer
共同支持这里的单字节操作；后续处理器手册中的地址不用于扩大 B7 支持范围。
末组保护是新实现的保守输入政策，不能当成 Intel 对硬件编码的额外规定。

共同核心新增 6 组测试：字节范围和边界、P/E 两表七个中间组、旧值与权限、
所有读/CPUID 故障位置、完整读回逐位损坏、取消与超时。GUI 验证选择/末组禁用、
取消/相同值/成功、过期与未验证结果清空；CLI 验证提前拒绝与 JSON 输出。
不包括末组/组数量重建、TVB、逐核 override、其它型号或目标真机调参验收。
