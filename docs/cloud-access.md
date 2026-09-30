# 云端测试意图与当前执行能力

更新：2026-09-30。

**后续决定：用户已要求创建公开 GitHub 仓库并使用 Actions 测试。当前执行路径见
[github-actions.md](github-actions.md)，不再等待 Codex Cloud 环境。**
下文保留此前核实记录，不能当成当前 GitHub Actions 的访问限制。

## 用户意图纠正

用户明确要求“你用你的云端来测试”，即希望使用 OpenAI/Codex 托管的云端执行环境。
前一轮把“我选2”理解为用户提供已有 Linux 服务器，并要求 SSH 信息，是助手的误解。
不应继续要求用户提供服务器、SSH 地址、私钥或认证材料。

已编写的 `port/ci/run-matrix.py` 是通用 Linux Docker 矩阵入口，可以保留；
它不负责创建 OpenAI 托管环境，也不能因为脚本存在就声称已经获得云端算力。
[cloud-build.md](cloud-build.md) 记录的是该 runner 的运行条件，不是用户承诺提供的服务器条件。

## 本轮实际核实

1. 当前 shell 的工作目录在 Windows F 盘；另一个可执行代码工具实际报告
   `platform=win32`、`arch=x64`，工作目录相同。
2. 当前注册工具没有直接创建/附加 Linux 云沙箱或切换本会话执行环境的入口。
   这只描述本会话工具，不否认产品提供托管云端功能。
3. 本地安装了 Codex CLI。实际执行 `codex cloud --help` 和 `codex cloud exec --help`，
   确认可以向已配置的 Cloud 环境提交任务，exec 要求 `--env <ENV_ID>`。
4. 在普通沙箱中，`codex cloud list --json --limit 5` 联网失败；获自动审批后执行相同
   只读命令成功，返回 `tasks: []`、`cursor: null`。这不是账户未登录或服务不可用的证据。
5. 进一步打开 CLI 的 Cloud 环境选择器，加载完成后只显示全局筛选项
   `All Environments (Global)`，没有具体环境可选。随后正常退出，没有提交任务或上传源码。
   这个观测仅限当前 CLI/登录范围，不能推断其他工作区或其他产品入口均没有环境。
6. 当前工程仍没有 Git remote，原 GUI 源码仍未出现。没有在这一轮构建 Linux 模块、
   Qt 或原生 rpm/deb，也没有把 Windows 检查标成云端测试。

## 正确的托管云端接入路径

OpenAI 官方文档给出的路径是：在任务的 `Work in > Cloud` 中创建或选择环境，
关联项目仓库、配置依赖和网络，完成环境准备/发布后开始任务。
依据：[Codex Cloud 官方指南](https://learn.chatgpt.com/docs/cloud)。

本地目录并不会因为用户说“用云端”而自动传入某个云环境。要继续真实测试，需先让
octool 的源码进入可用的托管环境并取得环境访问入口；不是让用户提供自有 SSH 服务器。
环境准备好后先运行 Debian11/12/13 的模块与 C 离线测试，再扩展十目标和 GUI。

还需在实际云环境核实容器运行时、发行版依赖下载和构建权限。
本仓库 runner 使用 Docker；官方文档说明存在托管环境，不等于当前已确认该环境可运行
嵌套 Docker。若不支持，应依据实际能力调整构建执行方式，不能伪造容器检查或跳过目标。
Secure Boot MOK、真实硬件加载与 MMIO 对拍继续按 [真机清单](hardware-acceptance.md) 验收。

原 GUI 源码缺失与云端环境缺失是两个独立问题；模块和 C 离线测试可以在云端先行。
当前源码归档仍是缺 GUI 的移植交接包，不能称完整可发布的 GUI 源码包。
