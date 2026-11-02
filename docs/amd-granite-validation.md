# Granite Ridge / 9950X3D 四状态 DPM 策略验证

2026-11-02，继续使用 REA 6.7.0 / Ghidra 12.1.4 核对 ASUS ProArt
X870E-CREATOR WIFI BIOS 2503。GUI、CLI 与发行构建基于
[72f349e](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/72f349eff6b9e65647bd3e7d0273cf1c2d2094af)。
[REA 研究提交](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/fe552d51c79f5ed8ee22a2c43586dd9e4df53d45)
与该版本只相差测试计数及核心测试环境配置，研究代码与输入逐字节一致。
[核心测试环境提交](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/edb0338a75305f7d3fbb20901128bdfc1a364e0b)
将 GitHub 已退役的 macOS 14 节点改为 macOS 15。核心、CLI 使用的较早提交与
最终应用提交仅有不参与各自构建的 GUI 视图计数／其他工作流配置差异，已逐文件核对；
GUI 和发行矩阵使用最终应用提交。
最终发布提交仅增加文档和验证记录。

## 本轮实现

- GUI/CLI 同步显示四个 DPM 状态的 20 个公共字段：4 个采样占比、
  12 个频率策略和 4 个电压策略。公共字段从 23 个增至 39 个。
- GUI 新增 **Granite Ridge DPM policies** 视图；CLI 继续使用
  `amd-pm-decode` 和 `amd-pm-native-read --platform granite-ridge`。
- 频率为标称 100 MHz 参考下的 MHz 策略值，FCLK/UCLK/MCLK 身份保留候选标记；
  电压为软件策略 V，未绑定物理 rail；采样占比为 %，不是时间驻留率。
- 四个状态全部导出，包括零占比状态；策略值不取采样平均，零电压覆盖码保留旧策略。
  导入时从原始字节重新生成单位、状态编号和证据标记。
- 回放新增 1,152 组频率策略、256 个分频码、744 次模拟熔丝返回、620 次插值、
  128 组电压覆盖和 140 个最终导出字。修正解释器的跳转退出循环处理，
  28 组既有回放重新运行且结果不变；加上新回放共 29 组与云端一致。

详见[字段、用法与原字节证据](amd-granite-dpm-policy.md)。
**本轮未确认核心/CCD 剩余八行物理单位，未完成 PM 物理绑定，未开放第二 rails 写入。**

## 最终验证结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 可移植核心 | 4/4；每目标 37 个 CTest、506 组场景 | [core](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41363791769) |
| 无 GUI CLI | 13/13；11 个 Linux 目标均 39 个 CTest、84 组 CLI 场景、1,630 条夹具 JSON | [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41363468718) |
| GUI／发行包／内核 | 26/26；每个 Linux 目标 247 项 Qt 测试；33 个安装包、12 次内核虚拟机启动检查 | [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41363987011) |
| REA／固件回放 | 1/1；六个 UEFI 模块，SMU/MP5 各 12,240 组编码自检，29 组本地与云端回放结果一致 | [REA](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41363176238) |
| 合计 | **44/44 最终验证任务通过** | [完整记录](validation/amd-granite-ci-72f349e.json) |

Linux 目标：el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13。EL7 是 Oracle Linux 7.9 基线。
Windows/macOS 仅运行核心测试；内核虚拟机检查不涉及真实调参寄存器或 Secure Boot。
发行矩阵还通过 142 项分析工具和 34 项移植检查；七组离线解析器的 ASan/UBSan 检查通过。
CLI 另执行 14 组新增 DPM 异常原始值导入，覆盖两个布局的全部 20 个字段。
EL7/EL8 基线各通过 247 项 Qt 测试，主矩阵保留 1,672 张截图。
EL7 与 Ubuntu 24.04 的新 DPM 视图已人工查看。

本机源码镜像移植检查为 37 通过、13 项因工具/平台条件跳过；使用工作区 TEMP。
这 13 项同名检查已在云端逐项通过，并核对测试源码一致；详见 `local_skip_cloud_coverage`。
八项本机既有扩展保留，未作为整套扩展提交云端验证。

## 交付与边界

本机交付目录 `dist/gui-cli-amd-granite-72f349e/`，含 11 组 GUI／CLI／DKMS 安装包。
`updated-source/octool-2.0.1-src.tar.gz` 含已验证代码、最终文档和八项既有扩展；
根目录源码包保留云端构建时快照。大小和校验值见 `MANIFEST.json`、`VALIDATION.json`
和 `SHA256SUMS`。

仍未完成：核心 **0/4/12** 行、CCD **0/1/2/6/8** 行单位；PM 槽位到物理
CCD／OS 核心绑定；第二 rails 独立写入与设备回读。公共索引 259/261/269
也保留未确认单位。目标机器不可用、实际 BIOS 未知；上述均不是目标真机验收。
