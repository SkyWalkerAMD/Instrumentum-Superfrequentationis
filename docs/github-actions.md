# GitHub 仓库与 Actions 测试

更新：2026-10-08。

## 授权与项目命名

用户明确要求在 GitHub 创建公开仓库 `Instrumentum Superfrequentationis`，立即用于构建测试；
开发完成后计划改为私密。仓库标识使用 `Instrumentum-Superfrequentationis`，标题保留空格。
已通过 GitHub CLI 和连接器双重确认当前账号为 `SkyWalkerAMD`。
仓库地址：<https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis>。

本源码目录是 Git 根，`.github/workflows/portability.yml` 位于根目录。
GUI 程序名、模块名、96 字节 ABI、MMIO 协议、源码包名沿用原名称。
不把 ZIP 中的旧可执行文件或 build/dist 临时产物作为原始源码上传。
现有许可头保留；新 GUI 沿用已链接 HAL 的 GPLv2，旧二进制/第三方许可没有改动。

## Actions 执行方式

push、pull_request 和 workflow_dispatch 触发 portability 工作流。
仅使用标准 `ubuntu-24.04` GitHub 托管 runner，内部运行十种目标发行版容器。
这条 GitHub Actions 路径不要求 Codex Cloud 环境或用户提供 Linux 服务器。

- matrix：本地可执行的发布门禁、源码归档一致性与已审阅 ABI/HAL 哈希检查。
  包括 3 个真实 ELF/ET_REL fixture 测试，以及 56 组原 MMIO 指令有界模拟；公开函数样本先校验固定 SHA。
  模拟器观察符合预期不代表原 GUI 错误处理已兼容，见 [邮箱研究](legacy-mailbox-contract.md)。
- kernel：十个独立目标，编译模块、loopback/transport/parity selftest、签名、DKMS 包生命周期。
- baseline：EL8 构建静态 Qt SDK 和当前 GUI，执行 Qt 控件回归与 ELF ABI 下限检查。
  SDK 按配方哈希缓存、恢复时校验 SHA-256；每次重新编 GUI。SDK 失败不放行 desktop。
- desktop：依赖 baseline，分别重编 GUI/Qt 回归、打包，在新容器安装并冒烟测试发行与 native 窗口。
  各页截图、显示日志和安装后 ldd 与 DKMS 记录一同上传；EL10/Ubuntu26 使用 Xwayland。
  window-probe 通过 Xlib 检查真实窗口的 PID、标题和 IsViewable；错误 PID 负向检查必须失败匹配。
  它在各目标编译，仅用于 CI，不依赖 EL10 已移除的 xwininfo，也不进入产品包。
- gate：任何必需阶段失败、取消或跳过都失败；不豁免 GUI 缺失来制造全绿。

上传的 Actions artifacts 保留 7 天；发布验收所需的日志/包应另行归档到正式交付记录。
邮箱模拟的 JSON 证据例外保留 14 天，同时将验收结果保存到源码 docs/validation。
早期 GUI 缺失时总门禁保持失败；现已接入作者选择的基础信息及原始寄存器 GUI，
该范围成功不代表后续平台监控/调参面板或真机已验收。
另提供手动 `Qt SDK diagnostic` 工作流，独立验证 EL8 静态 Qt 工具链并保留 SDK/配置/日志；
它调用同一 `in-container.sh sdk el8`，不以 SDK 构建代替 GUI 窗口或总门禁。
新增手动 `legacy mailbox investigation` 使用仓库只读权限和独立受限容器，运行同一公开八函数样本。
没有草稿读取令牌，不下载完整原 ELF；`legacy-runtime` 的既有 GUI 启动诊断保持独立。
实际 run URL、提交 SHA、目标结果和修复记录写入 [Actions 实测记录](actions-debugging.md)、
[验证状态](verification-status.md) 与根 CHANGELOG，不把配置文件当成运行成功证据。

## 计费和以后转为私密

公开仓库使用标准 GitHub 托管 runner 的运行时间免费；大规格 runner 始终收费，
artifact/cache 存储有独立的额度与计费规则。转为私密后，标准 runner 使用账号的私密仓库
免费分钟额度，超出额度可能计费。本项目不调整账号账单、付款方式或预算。
依据：[GitHub Actions billing](https://docs.github.com/en/billing/concepts/product-billing/github-actions)。

当前按用户要求保持公开，不自动提前改私密。以后转换时，公开期间产生的 fork 不会随之
变为私密；已被复制的公开内容也不能通过可见性转换收回。
依据：[GitHub repository visibility](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility)。
