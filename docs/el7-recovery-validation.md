# EL7 与同步功能恢复验证

2026-10-13 固定生产源码 `bc30c4f5166ac65f2bc827fd890026f1d3d4b846`。三个工作流合计 **43/43 任务通过**：
[核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611258)、[CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611273)、[Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611277)。
完整结果、原始日志路径、包及截图哈希见 [验证记录](validation/el7-recovery-ci-bc30c4f.json)。

本批恢复范围：

| 功能 | 支持范围 | 两端入口 |
| --- | --- | --- |
| [AVX2 倍率偏移](intel-avx-offset.md) | Intel 06_B7，0..31 档 | AVX2 offset / intel-avx-read、intel-avx-set |
| [TCC 温控](intel-thermal.md) | B7/8F/CF，有能力且未锁定时可改偏移 | Thermal limit / intel-thermal-read、intel-thermal-set |
| [限频原因与历史](intel-limits.md) | B7，11 组状态，按项或全部清除历史 | Limit reasons / intel-limits-read、intel-limits-clear |
| [最大非睿频倍率](intel-platform.md) | B7/8F/CF，只读，非实时频率 | Power / performance / intel-read |
| [封装热事件](intel-package-events.md) | B7/8F/CF，CPUID 能力检查，6 对只读状态 | Power / performance / intel-read |

另修复 Intel Controls 切换 CPU、确认期间变更目标及异步关闭的结果处理。

验证分层：

- 核心：Windows、macOS、Ubuntu 22.04/24.04，各 22 CTest、291 场景组；保留 ASan/UBSan 检查。
- CLI：11 发行版各 24 CTest，794 份 JSON；全新无桌面镜像安装、普通用户运行、卸载重装均通过。
- GUI：11 发行版及 EL7/EL8 Qt 基线各 135 测试无失败/跳过；真实显示服务窗口检查。
- 驱动：12 套匹配内核虚拟机，包括 Ubuntu 22.04 GA/HWE；实际模块加载、HAL/CPUID、能力查询、无效 CPU、权限拒绝和卸载。
- 原指令：91 项分析单元测试及固定字节模拟；本批 AVX 720、TCC 644、限频 274、基础倍率 644 场景。
- 本地用户目录：49 项移植脚本测试，37 通过、12 因缺 Linux/C++ 条件跳过；源码逐文件归档往返检查通过。
- 图像：保存 792 张测试与运行截图；新增模块截图已人工检查，测试值不是真机测量。
- 后续研究：ICCmax / VR topology 的 8 个原版函数通过本地 2,863 组指令模拟；该研究未进入上述云端二进制，也未开放新的调参入口。

固定提交源码归档约 4.58 MiB。各系统安装包大小如下（MiB，压缩包大小）：

| 目标 | GUI | CLI | DKMS |
| --- | ---: | ---: | ---: |
| el7 | 11.43 | 0.51 | 0.01 |
| el8 | 10.93 | 0.30 | 0.02 |
| el9 | 10.54 | 0.30 | 0.02 |
| el10 | 10.54 | 0.33 | 0.02 |
| ubuntu20.04 | 14.47 | 0.36 | 0.01 |
| ubuntu22.04 | 14.47 | 0.35 | 0.01 |
| ubuntu24.04 | 14.47 | 0.39 | 0.01 |
| ubuntu26.04 | 14.47 | 0.40 | 0.01 |
| debian11 | 14.47 | 0.35 | 0.01 |
| debian12 | 14.47 | 0.37 | 0.01 |
| debian13 | 14.47 | 0.38 | 0.01 |

交付目录为 `dist/gui-cli-el7-recovery-bc30c4f/`。目录中的源包与安装包对应上述同一生产提交；
之后补充的研究与验收文档保留在源码工作区，并未伪称进入该云端二进制。
`updated-source/octool-2.0.1-src.tar.gz` 另外保存最新用户工作区源码，含这些研究、文档和原有诊断工具扩展；
原始云端源包仍保留在交付目录顶层。263 个生产程序及构建文件与已验证源包一致。
用户工作区的 8 项历史工作流/原版诊断/归档工具差异已逐项记录，GUI、CLI 和驱动代码一致；
原版诊断的 EL7 容器补齐了引导脚本复制，但原版 ELF 的 EL7 运行仍未实测。
EL7 仅对实际测试的 Oracle Linux 7.9 环境作通过声明，不能外推所有 RHEL/CentOS 7 主机。

仍未完成：ICCmax/VR 域与单位映射、增强 TVB/Vmax、逐物理核心和 fabric 控制、AMD PState/PBO/曲线写入及物理量映射、完整原生 PM 遥测、主板 PMIC/VRM/EC/时钟和训练写入，以及真机调参/Secure Boot 验收。原版 52 MB ELF 的直接运行兼容性是另一项诊断，不等同于本批重构程序的兼容性。
