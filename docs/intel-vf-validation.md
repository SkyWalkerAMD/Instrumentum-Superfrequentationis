# Intel V/F 查询：GUI / CLI 同步验证

生产提交 `544f0b6a2e32ac0d04fada577662b49629a3c64f`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/intel-vf-ci-544f0b6.json)记录任务、产物 SHA256、逐系统测试、源文件比对及限制。
[功能范围](intel-vf-recovery.md)与[前端对照](frontend-progress.md)区分读取、设置及未还原内容。

| 验证层 | 结果 |
|---|---|
| 共同核心 | Ubuntu 22.04 / 24.04、Windows 2022、macOS 14：4/4，七个 CTest、62 场景组 |
| Linux CLI | 12/12；十发行版各九个 CTest，六组命令场景、37 个打开设备前拒绝、65 份 JSON |
| GUI / 驱动 | 23/23；十发行版与 EL8 基线各 35 Qt 测试；11 套目标内核 VM |
| 研究门禁 | 68 项分析测试；新增 135 组原 V/F 指令实验 |
| 本地 Windows | 原目录 48 项：36 通过、12 因缺少 C++/Linux 工具链跳过；云端补齐执行验证 |

CLI 的新命令在不加载 Qt/显示服务的进程中测试；十发行版均完成全新无桌面环境安装、普通用户诊断、
卸载重装。GUI 验证目标变更后清空旧值、两域/单点读取、失败点留空、复制保留目标与单位。
下载归档 230 张截图（每系统 22 张实际窗口截图及一张模拟 VF 查询结果）。截图中的模拟数值不是实测电压。

这次最初提交 `590ec58` 的核心与 CLI 已通过；Linux 前置检查因仍期待旧的 14 组输出而失败。
修正为当前 20 组 Intel OC/VF 场景后，重新在 `544f0b6` 上运行三个工作流并全部通过。
没有通过删测试、跳过目标或放宽硬件检查来通过验证。

## 交付目录与使用

`dist/gui-cli-vf-544f0b6/` 包含 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、
Rocky 8/9/10 的十个目录。每个目录有 GUI、CLI、可选 DKMS 各一个原生 DEB/RPM。
目录顶层有对应源码包及 SHA256SUMS。版本仍为研究版 2.0.1；按提交目录区分本次构建，未验证跨版本升级。

- GUI 包：10,279,500–13,941,832 字节。
- CLI 包：140,892–176,012 字节，不含可选 DKMS 和系统依赖。
- 每个包及源码的校验值见 `SHA256SUMS` 和证据清单。

安装自己发行版的 `octool` 或 `octool-cli` 包，也可以同时安装；硬件访问是否需要 DKMS 取决于系统已有驱动。
GUI：Intel Controls → V/F points，选择执行 CPU、Core/Cache 与全部或单个点，点击读取。
CLI（CPU 0 为示例，应从 diagnose 选择可用 CPU）：

```sh
octool-cli diagnose
sudo octool-cli intel-vf-read --cpu 0 --domain core > vf-core.json
sudo octool-cli intel-vf-read --cpu 0 --domain cache --point 8
```

候选点被固件拒绝时，退出码为 3，失败值为 null；其余点仍可有有效数据。
`scan_completed` 表示请求完成，不代表所有候选点都受支持。详情见[CLI 手册](headless-cli.md)。

## 尚未证明的内容

这些结果证明软件构建、接口、错误处理和安装路径；没有测试四台目标真机的固件效果或调压稳定性。
Windows/macOS 仅测试共同核心，不代表可安装此次 Linux 程序。VF 写入、AMD PStates/曲线设置、
完整 PBO 高层参数、Intel W790/W890 和板级写入仍在恢复中。Secure Boot 实机部署和跨版本升级未验收。
