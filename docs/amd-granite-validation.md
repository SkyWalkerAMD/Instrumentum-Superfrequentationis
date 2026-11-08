# Granite Ridge / 9950X3D 调参与映射验证

2026-11-08，继续使用 REA 6.7.0、REA.SKILL 和 Ghidra 12.1.4，
由三个子代理分别推进字段、映射和协议，集中集成 GUI / CLI 并验证。
参考固件为 ASUS ProArt X870E-CREATOR WIFI BIOS 2503 / SMU 98.84.0。
用户机器不在手边，实际 BIOS 未知，本轮没有真机验收。

[应用代码 991e7ab](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/991e7ab19dc7891d880b7a2778f7fab93d0b4397)
用于 CLI、GUI、发行包和驱动验证。核心和 REA 使用此前提交；差异仅为
CLI 帮助文字及 GUI 十六进制显示测试，核心和研究输入已逐文件核对未变。
[REA 代码](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/e91d2236cd834bc3ce8e9139ed466abb1a55a641)
与各次运行、产物哈希记录在报告中。发布前逐文件核对源码；最后的文档提交
不改变经过验证的应用代码。现有八项本地用户扩展单独保留。

## 本轮可用功能

| 功能 | GUI 与 CLI 共用行为 | 验证边界 |
|---|---|---|
| Granite Scalar | 读取、1–10 整数倍率设置，完整能力字与旧值检查，写后回读 | 只验证存储策略；被限幅即报告不一致；手动 OC 时 getter 返回零 |
| PPT / TDC / EDC / 温度限制 | 逐项设置，精确版本和能力检查 | 命令应答，不宣称有效限值或电气效果 |
| 手动 OC 模式 | 开启、关闭、相同状态无需写；读取模式位确认 | 不自动设置频率、电压或重置 Scalar |
| 全核 Curve Optimizer | Granite −50..+30 计数；Shimada 保持 −50..+50 | 仅主机应答，未证明每个目标实际应用；不是 mV |
| Curve Shaper 保存表 | 读取 5×3 signed8 count，完整保留 −128..127 | 不包含隐藏 baseline、fuse 锚点或插值后的效果；不开放写入 |
| 离线映射诊断 | 显式版本、presence/core masks 和 SMT，输出 PM 槽位及命令索引 | 不读硬件，不将命令编号当作 OS 核心或物理 CCD |
| 配置镜像说明 | 同步揭示 local C 全 32 位步进目标与低 8 位提交的差异 | 软件镜像不是设备回读；73h 写入仍关闭 |

接口与示例见[Granite 调参总览](amd-granite-controls.md)。新增硬件路径
要求 family 1Ah / model 44h、PCI 1022:14d8、非虚拟机及精确 98.84.0；
不会试探另一套消息通道，也不会在失败后自动重试或回滚。

## REA 和原始指令证据

新增九组回放，连同原有 33 组，共 **42 组本地与云端 JSON 完全一致**。
原有 33 组输出保持不变。所有回放均使用合成内存、显式服务边界或寄存器
响应；原始指令字节与固定输入哈希核对，没有模拟成真机效果。

- [Scalar](amd-granite-pbo-scalar.md)：setter 限幅、权限和原始 binary32 回读。
- [四类限制](amd-granite-pbo-limits.md)、[OC 模式](amd-granite-oc-mode.md)、
  [全核 CO](amd-granite-curve-all.md)：范围、参数、状态与应答边界。
- [消息通道](amd-granite-host-routing.md)：28 个 mailbox，区分 MP1 与 RSMU
  的同号命令；相邻表不能当作 RSMU 的高编号扩展。
- [映射输入](amd-granite-mapping-diagnostic.md)：49,152 个原始重编码表项，
  18 个固定样例由原字节回放独立生成，C++ 核心、GUI、CLI 分别核对。
- [UEFI 获取路径](amd-granite-map-acquisition.md)：304 例，经指定协议和
  读取实现到 SMN/PCI，记录读取错误被忽略的边界；运行时提供者未认证。
- [Curve Shaper](amd-granite-curve-shaper.md)：保存表、baseline 叠加及二维插值，
  证明单档位写入还会改动其它档位；产品本轮只读。
- [本地配置 C](amd-granite-local-config.md)：主机 73h、逐步写入、原始
  端口和另一初始化路径；32 位目标与实际低字节提交可不一致。

REA 实际分析七个固定 UEFI 模块。本轮增加 register-service 模块和 provider A
的新入口。SMU/MP5 仍使用单独的静态 Xtensa 解码器。新增 SEXT 语义按
QEMU 的原始立即数解码核对；393,216 个 CO 符号扩展样例检查真实指令。

另保留仅在本地验证的原版 Windows 消费器与 UEFI 掩码消费器研究：
旧版列名和列数会造成 CCD 数据错位，不能据此给八行补单位；2,048 例
掩码消费回放也未证明与 SMU enabled mask 等价。脚本、结果与输入哈希
记录在研究报告的 `supplemental_local_research` 中，未计入 42 组云端证明。
后续 2,984 例本地传输回放还确认 MP1 `10h` 会导入并修改整块 1,428 字节
配置，包含 Curve Shaper baseline 和 runtime 区，不能当作只读 getter。
真实 DMA 完成、baseline 内容与安全读取入口仍未证实，产品未开放该路径。

## 验证结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 可移植核心 | 4/4；每目标 42 个 CTest、554 组场景 | [core](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/42437280691) |
| 无 GUI CLI | 13/13；11 个 Linux 目标各 44 个 CTest、93 组 CLI 场景、1,706 条夹具 JSON | [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/42437815223) |
| GUI / 安装包 / 内核 | 26/26；各目标 265 项 Qt 测试；33 个安装包、12 次内核虚拟机启动 | [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/42437815227) |
| REA / 固件回放 | 1/1；七个 UEFI 模块、SMU/MP5 各 12,240 个解码自检、42 组回放一致 | [REA](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/42437281041) |
| 合计 | **44/44 正式验证任务通过** | [完整记录](validation/amd-granite-ci-991e7ab.json) |

Linux 目标：el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13。EL7、EL8 构建基线运行相同 Qt 测试；每个 Linux
目标收集 159 张截图，共 1,749 张。实图检查 EL7 与 Ubuntu 24.04 的六个
新增界面，结果标签、输入状态和映射说明与合成数据一致。截图不是真机采集。
另检查 EL10 内部时钟视图的全部行可见性。首轮发现的 CLI 报告总数断言
和 Qt 6 截图窗口高度问题，以及 Curve Shaper 测试的十六进制大小写差异，
均已修正后重跑；完整数值和可见性断言仍保留。
ASan/UBSan 覆盖八个离线解析与映射测试。

云端原有分析测试 142 项、移植测试 34 项通过。本地分析全套有一项既有
Intel TVB 的一秒模拟时限中止；不改代码隔离重跑其两项测试通过，云端全套
也通过。原失败和复测日志均保留。其余本地跳过项目均有云端对应验证。
本地用户目录 50 项移植测试，37 项通过、13 项因本机工具链缺少而跳过。

## 交付与未完成项

`dist/gui-cli-amd-granite-991e7ab/` 包含 33 个安装包、云端原始源码包、
`updated-source/octool-2.0.1-src.tar.gz`、`MANIFEST.json`、`VALIDATION.json`
和 `SHA256SUMS`。更新源码包包含最终文档及八项既有用户扩展；不能据此
把本地扩展也宣称为经过相同云端测试。

仍未完成：核心 **0/4/12** 与 CCD **0/1/2/6/8** 八行物理单位；五个公共
配置值的物理单位；PM 槽位到物理 CCD / OS 核心的绑定；第二 rails 的独立
写入和设备回读；Curve Shaper baseline、有效插值参数及写入；其它尚未验证的
Granite 调参入口。新增回放没有将这些
未知项标成已完成，实际硬件稳定性与效果也未验证。
