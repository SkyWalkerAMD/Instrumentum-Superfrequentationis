# Granite Ridge / 9950X3D 公共电压请求与 APIC 枚举验证

2026-11-01，本轮使用 REA 6.7.0 / Ghidra 12.1.4 继续分析 ASUS ProArt
X870E-CREATOR WIFI BIOS 2503。应用、测试与研究脚本通过的版本均为
[e75556b](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/e75556b5e7863ddd9e5c2fbfe43426119422183f)。
其后发布提交仅整理文档和验证记录，未改变应用或研究输入。

## 本轮实现

- GUI、CLI 共用 PM 解码器新增 **PM 索引 19／偏移 0x4c：公共电压请求镜像，单位 V**。
  与索引 48 命令镜像、索引 49 SVI 电压遥测分别呈现，不把请求值当作设备回读。索引从零开始。
- 61h 请求路径完成选择、向上取整、编码上限、通道 0 路由及 PM 导出的连接；
  24,576 组请求案例、160 组另一分支案例、6,144 组相邻浮点边界案例通过。
- APOB 排序器、两套提供者、APIC 构造器与记录写出方贯通回放，864 组案例、
  13,104 条输出记录通过。确认传入构造器的是枚举序号；尚不能从这里直接绑定 PM 槽位。
- 导入时根据原始字节重建标记；伪造“已回读／物理电源已确认／可写”不会被信任。
  负零、负值、次正规数、非有限值的原始位保留，不额外缩放或钳制。

详见[公共请求路径](amd-granite-shared-request.md)及
[APIC 枚举连接](amd-apob-apic-enumeration.md)。本轮新增的是一个公共软件电压字段，
**剩余八行中没有新增已确认单位**，也没有开放新的硬件电压写入。

## 最终云端结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 可移植核心 | 4/4；Ubuntu 22.04/24.04、Windows 2022、macOS 14；每目标 37 个 CTest、496 组场景 | [core](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41289101995) |
| 无 GUI CLI | 13/13；11 个 Linux 目标均 39 个 CTest、81 组 CLI 场景、1,584 条夹具 JSON；另增 14 组请求值进程测试 | [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41289106398) |
| GUI／发行包／内核 | 26/26；每个 Linux 目标 244 项 Qt 测试，EL7/EL8 基线各 244 项；33 个安装包、12 次内核虚拟机启动检查 | [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41289113688) |
| REA／固件回放 | 1/1；六个 UEFI 模块，SMU/MP5 各 12,240 组编码自检，22 组本地与云端回放结果一致 | [REA](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41289121712) |
| 合计 | **44/44 最终任务通过** | [完整机器可读记录](validation/amd-granite-ci-e75556b.json) |

Linux 目标：el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13。EL7 为 Oracle Linux 7.9 基线。
Windows/macOS 是核心测试，不是 Linux 安装包或真机调参验收。
内核虚拟机检查未覆盖真实调参寄存器或 Secure Boot。
本机源码镜像的 50 项移植测试为 37 通过、13 项因缺工具链跳过。云端编译验证使用上表仓库提交；
八项既有本机扩展单独保留，未作为整套扩展提交云端验证。
ASan/UBSan 的七组离线解析器检查通过，发行矩阵还通过 142 项分析工具及 34 项移植检查。

首轮新增用例通过后，测试末尾的场景总数断言仍为 16；已改为 17 后重跑。
最终结果仅统计上表版本。被替代的运行及原因保存在验证记录中，没有计入通过数量。

## 交付与边界

本机交付目录为 `dist/gui-cli-amd-granite-e75556b/`，含 11 组 GUI／CLI／DKMS 安装包。
`updated-source/octool-2.0.1-src.tar.gz` 包含已验证代码、最终文档和八项既有用户扩展；
根目录源码包保留云端测试时的原始快照。文件来源、字节数、哈希和检查范围见
`MANIFEST.json`、`VALIDATION.json` 与 `SHA256SUMS`。
本机源码镜像已核对，保留既有扩展和文档 UMC 链接别名。
EL7 和 Ubuntu 24.04 的公共请求/命令镜像截图已逐张检查，相关哈希保存在验证记录中。

仍未完成：核心第 **0/4/12** 行、CCD 第 **0/1/2/6/8** 行单位；PM 槽位与物理
CCD／OS 核心绑定；第二 rails 独立写入与设备回读。原指令回放说明当前软件模型的
计算路径，不替代缺失的计数事件、时间基准、模型系数校准和真实固件选择证据。
目标 9950X3D 不在用户身边，安装的 BIOS 未知，本轮没有进行真机验收。
