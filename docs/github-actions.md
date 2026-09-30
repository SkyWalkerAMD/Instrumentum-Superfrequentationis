# GitHub 仓库与 Actions 测试

更新：2026-09-30。

## 授权与项目命名

用户明确要求在 GitHub 创建公开仓库 `Instrumentum Superfrequentationis`，立即用于构建测试；
开发完成后计划改为私密。仓库标识使用 `Instrumentum-Superfrequentationis`，标题保留空格。
已通过 GitHub CLI 和连接器双重确认当前账号为 `SkyWalkerAMD`。
仓库地址：<https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis>。

本源码目录是 Git 根，`.github/workflows/portability.yml` 位于根目录。
GUI 程序名、模块名、96 字节 ABI、MMIO 协议、源码包名沿用原名称。
不把 ZIP 中的旧可执行文件或 build/dist 临时产物作为原始源码上传。
现有许可头保留；创建公开仓库不擅自给尚未收到的 GUI 源码添加许可证。

## Actions 执行方式

push、pull_request 和 workflow_dispatch 触发 portability 工作流。
仅使用标准 `ubuntu-24.04` GitHub 托管 runner，内部运行十种目标发行版容器。
这条 GitHub Actions 路径不要求 Codex Cloud 环境或用户提供 Linux 服务器。

- matrix：本地可执行的发布门禁、源码归档一致性与冻结 ABI/HAL 检查。
- kernel：十个独立目标，真实编译模块、运行 C 离线测试、签名试验、构建并安装 DKMS 包。
- baseline：EL8 重编原 GUI；当前缺原 GUI 源码，会明确失败。
- desktop：依赖 baseline，分别重编 GUI、打包，在新容器做安装和窗口冒烟。
- gate：任何必需阶段失败、取消或跳过都失败；不豁免 GUI 缺失来制造全绿。

上传的 Actions artifacts 保留 7 天；发布验收所需的日志/包应另行归档到正式交付记录。
GUI 源码未出现前，先收敛十目标 kernel 阶段，保留完整工作流的失败状态。
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
