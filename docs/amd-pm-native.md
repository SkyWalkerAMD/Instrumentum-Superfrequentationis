# 原生 AMD PM 表采集

<!-- amd-pm-ccd-mapping-current -->
新增 [PM 固件 CCD 槽位映射](amd-pm-ccd-mapping.md)：GUI 与 CLI `--map-ccd` 同步支持精确 SMU 115.46.0 的存在掩码查询、稀疏编号映射和前后复核。OS CPU/APIC 对应、逐核/CCD 物理单位及第二 rails 回读仍未完成。
<!-- /amd-pm-ccd-mapping-current -->

2026-10-26 更新：[逐核 / CCD 存储区](amd-pm-core-ccd.md)已由 MP5 原指令闭合边界并接入两端；PM 快照上限 1 MiB，公共显示字段由 24 项改为 22 项，撤回 VDDG 两项误映射。标签/单位/物理映射和双 rails 硬件写入仍待完成。以下保留此前阶段记录。

GUI 的 **AMD tuning → PM telemetry → Read native PM table** 与 CLI
`amd-pm-native-read --cpu N` 共用原生采集核心。它通过 OCTool 后端读取固件提供的缓冲区，
不依赖 `ryzen_smu` 的 sysfs 节点；原有驱动读取和离线导入仍可独立使用。

```sh
octool-cli diagnose
octool-cli amd-pm-native-read --cpu YOUR_ALLOWED_CPU > native-pm.json
octool-cli amd-pm-decode --file native-pm.json
```

选择进程允许访问的逻辑 CPU。它用于核验 CPU 身份与物理地址宽度，**不是 PM 表或物理插槽选择器**。
原生入口仅支持 AMD family 1Ah / model 08h、PCI 1022:153a、domain 0 / BDF 00:00.0；
CPUID 声明虚拟化、地址宽度缺失或不在 32–52 位范围时拒绝操作。
实际设备访问权限与 OCTool 后端相同。GUI 沿用 Information 页的已有授权流程；CLI 不弹出授权窗口。

## 采集合同

1. 核验目标身份，通过 B8/BC 的 RSMU `05h` 读取版本和返回长度。
   只接受现有八个精确 Shimada 版本，且第二个返回字必须与版本表长度完全一致。
   长度为零、不匹配或未知版本时停止，不发送传输命令，不读取物理内存。
2. RSMU `04h` 使用六参数 `{1,1,0,0,0,0}`，前两个返回字组成小端 64 位地址。
   要求地址至少 4096、四字节对齐，整个已知长度在 CPU 声明的物理地址位宽内。
   不截断高位，不清除标志位，也不扩大或裁剪范围。
3. 再次核验身份后，RSMU `03h` 的六参数清零，提交一次 PM 表传输并检查 response。
   不写 `50200h`、不设置 DRAM 地址、不主动写 response=1，也不重试失败命令。
4. 仅按四字节读取经过校验的范围，最多 11636 字节；每次访问都检查取消和事务期限。
5. 完整读取后重新核验 CPU/PCI/地址位宽、表版本、返回长度和缓冲区地址。
   任一访问失败或元数据变化，丢弃整份字节与解码字段。成功后调用现有公共解码器。

固件可能没有实现返回长度；此时本入口会拒绝采集，不能把静态版本表长度冒充固件实际返回长度。
可继续使用已有驱动入口；不会自动回退或加载驱动。

## 结果与前端

CLI `data.native` 保留阶段、版本、返回长度、有效地址、已读字节数、传输尝试/接受及元数据复查结果。
`data.pm` 沿用共享捕获格式。GUI 复制同样的 JSON 包装，两端均可离线导入。
导入重新从原始字节计算字段，不信任文件内的传输成功声明或显示值。
GUI 输入在异步读取中变化时丢弃旧结果；关闭窗口请求协作取消。已经进入内核的操作不能被强行中断。

NaN/Inf 只影响相应字段：元数据复查通过的完整原始捕获仍可导出，退出码为 3，阶段为 `decode`。
传输或复查失败的报告可复制诊断，但没有部分原始缓冲区可供误读。

固件接受传输和前后元数据相同不能证明采样时间、原子性、真实性或物理目标。
报告的 `origin_verified`、`atomic_snapshot_guaranteed`、`freshness_verified`、
`physical_mapping_verified` 始终为 false。事务锁只约束本进程。
没有新增调参命令、逐核/CCD 遥测解码或 rails 写入；现有 220 项字段及布局审查规则继续适用。

## 协议依据

固定 ZenStates-Core 提交 `869b7e82572337e5a673e92230edc92bddf0e44b` 的
[Shimada 配置](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)、
[GetTableVersion](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/GetTableVersion.cs)、
[GetDramAddress](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/GetDramAddress.cs)、
[TransferTableToDram](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/TransferTableToDram.cs)
共同提供 mailbox、TYPE_CPU4 参数与返回字定义。
新增 REA 研究追踪原版传输/地址函数和 VID 换算；反编译用于核对原版行为，不视作固件成功证据。
软件测试使用合成设备；真实机器的传输、返回长度和后端内存映射仍待验收。
