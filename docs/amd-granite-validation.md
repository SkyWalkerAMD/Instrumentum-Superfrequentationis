# Granite Ridge / 9950X3D 配置镜像与命令映射验证

2026-11-08，继续使用 REA 6.7.0 / Ghidra 12.1.4 研究 ASUS ProArt
X870E-CREATOR WIFI BIOS 2503。GUI、CLI、发行构建使用同一
[应用提交 5d886cc](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/5d886ccea933e2d46c90bb0afddb6317dd628fac)；
[REA 提交](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/1a474d3dd909778f0b6ff894ecb2cbc6f96e361d)
仅额外修正复制回放脚本与其说明，应用和构建输入逐文件核对一致。
最终发布提交仅更新文档和验证记录，应用与研究代码没有在验证后修改。

## 本轮实现与研究

- GUI / CLI 补齐固定固件目标 0/1 的四个配置值与一个本地配置镜像。
  新增目标 1 的两个字段，公共字段从 39 增至 **41**；GUI 新增配置镜像视图。
  前四项是平均值，最后一项直接复制。单位仍未确认，目标缺席也可能保留旧值。
- 恢复逐核命令的稀疏重编码过程：SMT 目标步长 32、非 SMT 步长 16，
  启用核心和非空目标会压缩编号。PM 保留每目标八个核心位置，不能直接把
  PM 槽位或 OS/APIC 编号当作命令索引。共享 JSON 和 GUI 提示同步说明该边界。
- 查明三路命令镜像启动时来自本地控制器状态码，后续由软件写入路径更新；
  这条启动读取路径没有设备查询包或完成位检查，不是独立运行时设备 getter。
- 补齐计时器整数换算、截止时间和延迟截断回放：零参数关闭超时，等于截止时间
  仍继续读取，超时没有独立返回状态；仍无足够证据确认绝对 Hz 或计数事件。
- 修正 MOVI.N 负立即数解释，加入 2,048 组 ISA 编码自检。原有 29 组固件
  回放本地重跑结果保持不变，新增四组后共 **33 组与云端结果一致**。

详细证据：[配置镜像](amd-granite-common-config.md)、
[命令索引](amd-granite-slot-binding.md)、[rails 来源](amd-granite-rail-routing.md)、
[计时器边界](amd-granite-unit-anchors.md)。
**本轮没有新增已确认的核心/CCD 物理单位，没有开放新的硬件写入命令。**

## 最终验证结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 可移植核心 | 4/4；每目标 37 个 CTest、508 组场景 | [core](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/42431610546) |
| 无 GUI CLI | 13/13；11 个 Linux 目标均 39 个 CTest、84 组 CLI 场景、1,630 条夹具 JSON | [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/42431622848) |
| GUI／发行包／内核 | 26/26；每个 Linux 目标 248 项 Qt 测试；33 个安装包、12 次内核虚拟机启动检查 | [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/42431635211) |
| REA／固件回放 | 1/1；六个 UEFI 模块，SMU/MP5 各 12,240 组编码自检，33 组本地/云端回放一致 | [REA](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/42432173976) |
| 合计 | **44/44 最终验证任务通过** | [完整记录](validation/amd-granite-ci-5d886cc.json) |

Linux 目标：el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13。EL7、EL8 基线均完成相同 Qt 回归；每目标收集 153 张
截图，共 1,683 张。实际检查 EL7 与 Ubuntu 24.04 的新配置视图，五行数值、
单位未确认状态和范围说明均可见。截图使用离线合成数据，不是硬件采集。

本地完成全部可运行的分析与移植测试；缺少 Linux 工具链的项目由云端补测。
原始日志和跳过项目的云端对应结果列入验证记录。额外 14 次配置字段 CLI
原始文件导入测试覆盖负零、非正规数、负值、Infinity 和 NaN，不计入 1,630 条
自检夹具 JSON。导入重新生成目标、来源、单位及信任标记，不采信伪造元数据。
ASan/UBSan 继续覆盖七个离线解析器。

首轮 REA 在信号 NaN 复制案例失败：验证脚本将 LSI/SSI 寄存器传送经宿主
浮点转换，Linux 会改变信号位。修正为不执行算术的原始位复制回放后重新验证，
没有修改应用解码器。另一本地既有 Intel TVB 回放在总套件中触及执行时限；
未改代码的两项隔离复测通过，同一案例也在云端分析套件通过。原始失败与复测
日志均保留，不计作新增功能或真机验证。

## 交付与未完成项

`dist/gui-cli-amd-granite-5d886cc/` 包含 33 个安装包、通过验证时的原始源码包、
`updated-source/octool-2.0.1-src.tar.gz`、`MANIFEST.json`、`VALIDATION.json`
和 `SHA256SUMS`。更新源码包包含最终文档及八项既有用户扩展；扩展单独保留，
不能因为云端主仓库通过就声称这些本地扩展也完成了相同云端验证。

仍未完成：核心 **0/4/12** 与 CCD **0/1/2/6/8** 八行物理单位；五个公共配置
值的物理单位；PM 槽位到物理 CCD／OS 核心的绑定；第二 rails 的独立写入与
设备回读。用户机器不可用，实际 BIOS 未知，本轮没有真机验收。
