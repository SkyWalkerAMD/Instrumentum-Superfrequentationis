# 项目知识入口

更新日期：2026-09-30。接手时先读这里，再读根目录 `CHANGELOG.md`。

本轮在用户提供的 `octool-linux-refactor.tar.gz` 上继续修改，没有重写 GUI 或硬件协议。
当前尚未达到十目标全部通过的发布标准；用户已授权公开 GitHub 仓库并启动 Actions 实测。

- [多发行版构建、运行和打包](multi-distro.md)：十目标、构建入口、依赖方案、CI 门禁。
- [GitHub 仓库与 Actions](github-actions.md)：当前测试路径、公开状态与计费边界。
- [Actions 实测修复记录](actions-debugging.md)：软件源、Kbuild 探测与 EL headers 的实际失败及处理。
- [GUI 恢复与重构](gui-recovery.md)：源码丢失后的作者授权、Windows 输入现状与恢复边界。
- [首批 GUI 重构](gui-phase1.md)：作者选择的基础信息和原始读写、传输修复与测试范围。
- [EL8 静态 Qt SDK](qt-sdk.md)：成功 run、产物哈希、依赖配置及复用边界。
- [云端接入状态](cloud-access.md)：用户要求 OpenAI 托管云端；当前工具/CLI 的实际核实结果。
- [Linux Docker 执行指南](cloud-build.md)：独立 runner、日志和失败判定。
- [真机验收清单](hardware-acceptance.md)：安装、MOK、insmod/modprobe、MMIO 对拍、GUI。
- [验证状态及输入证据](verification-status.md)：本轮实际做过什么、哪些未做。
- [原始重构指南](../port/docs/refactor-guide.md)和[原始对拍指南](../port/docs/parity-verification.md)：历史结论及工具设计。
- [第一阶段分析](../analysis/docs/linux-porting.md)：原二进制与内核兼容性分析，保留原始验证范围。

## 项目约束

1. 版本使用点号，源码包名为 `octool-x.y.z-src.tar.gz`。
2. 持久知识放源码树 `docs/`，开发日志放根目录 `CHANGELOG.md`。历史日志保留在原路径。
3. 本任务不修改 GUI 现有调用点或 MMIO 线级协议，不按内核版本号选择 API。
4. 不确定的硬件字段、面板含义、单位、缩放系数必须询问作者，不能从相似值推断规则。
5. 需要作者选择时，在回复末尾使用带选项的编号列表。

## 当前缺失输入

- 用户随后确认原 Linux GUI 源码已经丢失，授权先检查 Windows 版，必要时重构。
  已提供 `F:\OpenAI\Codex\project\octool\OCTool0528.zip`，但当前环境中该文件不存在；
  重构不得猜测硬件字段、单位或面板含义。
- 原 GUI 源码：当前树没有 `.pro`/GUI C++ 源码。`port/gui/build.json` 的
  `gui/octool.pro` 是明确的接入位置，**不是已经存在的工程**。
- 用户最初提到的 `OCTool0528.zip` 不在给定工作目录；已读取的 Linux ZIP 只有二进制。
- 当前本地执行环境为 Windows；用户现在授权使用公开 GitHub 仓库的 Actions Linux runner。
  真实结果以仓库 run 和验证文档为准，写好 workflow 不等于已经通过。
- 用户要求使用 OpenAI/Codex 托管云端，不是提供自有服务器。Cloud CLI 可连接服务，
  当前改用用户明确指定的 GitHub Actions 路径，不再以 Codex Cloud 环境作为测试前提。
- GUI 工程依赖、实际窗口标题、资源位置、发行许可证须拿到源码后核对。
  此前二进制分析可作为排查线索，不能冒充源码验证。

缺输入时 CI 会失败；不会用一个示例 Qt 程序或原 Ubuntu 二进制替代作者 GUI。

## 范围和未决事项

CI 的 EL 三项默认使用 Rocky 对应大版本镜像。Alma/RHEL 使用相同构建入口，
但 Rocky 结果不是 Alma/RHEL 实测证据。RHEL 还需要已订阅的软件源，见真机清单。
当前只支持 x86_64/amd64，未提出 ARM 或 32 位支持。

MOK 签名解决“内核是否接受模块”。在不修改旧 GUI 调用点的约束下，旧 GUI 的
`iopl`、`/dev/mem`、MSR 直接访问仍可能被 lockdown 限制；本轮没有把它们暗中接到 HAL。
历史指南提出的全 HAL 集成不是本轮已完成工作，也不能据此宣称 Secure Boot 下超频全功能通过。

收到 GUI 源码后，首先核对启动时驱动装载逻辑、权限不足的处理、窗口标题、资源位置和
实际 Qt 模块需求；如果无模块时 GUI 不能运行，先向作者确认允许的处理方式。
