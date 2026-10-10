# Intel core / cache 最大 OC 倍频

在已有 Raptor Lake-S 电压接口上恢复核心域与 cache/ring 域的最大 OC 倍频设置。
入口在 Intel Controls → Core / cache voltage and ratio，先读取，填写独立的 Maximum OC ratio，再确认提交。
一个按钮只改变一个字段；原电压 offset、target、adaptive/override 和另一个域保持原值。

范围仍为 GenuineIntel Family 6 Model B7、具备 MSR 能力的客户端接口。
逻辑 CPU 用来访问邮箱，设置属于所选域；它不是逐核倍频、按活动核心数量的 turbo 表或实测频率。
没有扩大到 Xeon W790/W890，也没有根据主板名称猜测 CPU 编号或固件选择器。

## 原代码和公开定义

固定原 ELF 中四个完整函数被提取为[原字节 fixture](../analysis/fixtures/legacy-intel-ratio.json)：

| 原函数 | 地址 | 字节数 |
|---|---|---|
| Wr_150max_ring_ratio(int) | 0x375280 | 1298 |
| Rd_150max_ring_ratio() | 0x375860 | 415 |
| Rd_150maxratio() | 0x375a80 | 440 |
| Wr_150maxratio(int) | 0x375c40 | 943 |

[可复现实验](../analysis/tools/legacy-intel-ratio.py)执行完整函数的 client 路径，显式令 NVL/GNR 标志为零。
MSR 包装器和延时是替身，宿主文件、寄存器、固件均未访问。
108 个场景覆盖两域、读取/修改、负数和低字节截断、固件失败、传输返回失败与持续忙状态。
[静态证据](validation/legacy-intel-ratio-static.json)及[本机观测](validation/legacy-intel-ratio-local.json)分别保留。

原指令只替换 data[7:0]，命令分别为 10h 查询、11h 修改，core/cache 的 domain 是 0/2。
原 client 修改没有完成状态检查或修改后回读；输入 257 会截成 1，错误响应也继续写。
持续 busy 会停在实验的指令上限，原循环自身没有超时。新实现没有复制这些行为。

[coreboot 的字段定义](https://github.com/coreboot/coreboot/blob/cdd0aaaa8e0c2e52a57b515e801b339549e53283/src/drivers/intel/oc_mailbox/oc_mailbox.c)
与原字段位置一致；它本身不证明 Raptor Lake 固件已经验收。
[Intel Raptor Lake-S FSP 配置定义](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h)
分别列出 CoreMaxOcRatio / RingMaxOcRatio，范围 0–85，0 表示该配置入口的硬件默认值。
新运行时接口采用更窄的 **1–85**，不推断运行时写 0 的默认值语义，也不把 8 位容器范围当作可接受的倍频范围。
FSP 是配置结构，不是运行时寄存器地址；来源版本和文件 SHA 见[来源记录](validation/intel-ratio-sources.json)。
这个范围是接口约束，不是运行建议。

## 新实现与验证边界

`encodeIntelOcRatio` 校验范围，保留高 24 位；`applyIntelOcRatio` 与 offset 共用有界事务：
验证旧快照 → 重新确认身份与锁 → 比较完整旧值 → 单次修改 → 检查状态 → 完整 32 位回读。
输入越界、已锁定或旧值变化时不提交修改；同值不重复写；失败、超时、取消和回读不一致不显示成功。
即使倍频正确而电压模式改变，也不能通过回读验证。

新增四个核心场景组覆盖全部 1–85 编码、两域隔离、offset/ratio 连续修改、每个传输失败位置、
拒绝/静默忽略/电压字段被改变、旧值/锁/取消/持续忙。
新增两个 Qt 用例覆盖输入、取消、提交、域切换失效和锁定/未验证状态。
云端验证和安装产物另记录精确提交；本页的合成测试不是 Z790/14900KS 真机验收。
