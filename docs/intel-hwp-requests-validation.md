# HWP 请求与 PECI 还原批次验证

2026-10-13，生产提交 `fb09aeaf05c055c3320a50bb90c48dd1e3750c3a`，分支 `refactor/platform-recovery`。
GUI / CLI 同步增加最多 **25 项 HWP 封装请求、PECI 覆盖、选择标志和通知/历史信息**，
并核验现有本地编辑的外部覆盖状态。[实现与来源](intel-hwp-requests.md)、
[完整证据](validation/intel-hwp-requests-ci-fb09aea.json)。
DDRIO 的 68 字段/v4 快照、BCLK 自适应电压、PL4、能量计数及既有功能均包含在本批安装包。

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38351311553) | 4/4；各 24 CTest / 316 场景组，其中平台控制 24 组 |
| 快照内存检查 | 五个 ASan/UBSan 目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38351311569) | 13/13；11 Linux 目标各 26 CTest / 38 CLI 场景组 / 911 份 JSON |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38351311623) | 26/26；各发行版及 EL7/EL8 Qt 基线均 151 项 GUI 回归；33 移植测试、101 分析测试 |
| 目标内核 | 12 套匹配内核 QEMU 启动，含 EL7 3.10；模块装卸、能力、非法 CPU 和非特权拒绝通过 |
| 真实硬件调参 | 未执行；本次合成测试不能替代物理效果和稳定性验收 |

43/43 云端任务均对应上述最终提交。首轮发现的计数断言和 MSVC 测试类型警告已修复，
没有把早期失败的任务当作最终通过。CLI 包在全新无显示环境中完成安装、运行、卸载和重装，
无 Qt/X/Wayland 运行依赖。保留 847 张自动截图；人工视觉核对的具体文件列在证据中。
原版 PECI 读取入口另经 138 个固定指令回放场景核对。

`dist/gui-cli-intel-hwp-requests-fb09aea/` 含 **33 个包**，每个目标 GUI、CLI、DKMS 各一：
Oracle Linux 7.9、Rocky Linux 8/9/10，Ubuntu 20.04/22.04/24.04/26.04，Debian 11/12/13。
EL7 范围仍为 Oracle Linux 7.9 x86_64，不能据此宣称所有 RHEL/CentOS 7 配置已验收。

根目录的 `octool-2.0.1-src.tar.gz` 为精确云端源码；`updated-source/` 中的同名源码包保留
用户本地扩展和最新研究/验证文档。269 项生产源码/构建文件已与云端产物核对，保留差异明确列出。
`SHA256SUMS` 和 `updated-source/MANIFEST.json` 用于校验和区分这两套源码。
版本号仍为 2.0.1，升级同版本时应使用发行版的本地包重装方式；此前交付目录保持不变。

本批新增查询均按 CPUID 门控；PECI/封装数据为存储请求，性能索引不代表 MHz。
不增加 PECI、封装请求、通知、极性或历史清除写入；选用封装控制时仍保守禁止本地编辑。
Windows/macOS 验证共同核心，完整硬件后端目标仍是 Linux。
ICCmax/VR、部分 AMD 和主板控制等[未还原内容](recovery-status.md)与真机/Secure Boot 验收仍待完成。
