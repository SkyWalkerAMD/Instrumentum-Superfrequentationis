# 验证状态与输入证据

日期：2026-09-30。状态分为“输入包已有记录”“本轮实际检查”“尚未运行”，不混用。

GitHub Actions 已实际运行。第四轮提交 `a8a5f1e` 的
[run 36659550123](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36659550123)
十个 kernel job 全部成功，覆盖 11 个实际 kernel release。每个目标的 HAL、C 离线自测、
17 项 Python 测试、模块实编、签名试验和 DKMS 原生包安装通过；总门禁因 GUI 缺失仍失败。
镜像 digest、kernel release、签名分支与包 SHA-256 见
[机器可读证据](validation/actions-run-36659550123.json)。失败到修复过程见
[Actions 实测记录](actions-debugging.md)。

## 输入包

| 输入 | SHA-256 | 内容 |
|---|---|---|
| octool-linux-refactor.tar.gz | `43c8639802ddec1aae2d68eaae16b54655ddbb2738af6849ed3329fe29e342ac` | 用户提供的 port/ 与 analysis/ 源码基础 |
| octool-linux.zip | `02fd2ae0534c86aed23cb5470fb2520b0772f4421c6ed1c966665d171313b53b` | GUI ELF、旧 .ko、mylib、启动脚本，没有 GUI 源码 |
| OCTool0528.zip | 未读取 | 指定工作目录没有此文件 |

只解出重构包到工作目录的 `octool-linux/`。没有运行原 ZIP 的安装/启动脚本。
旧 GUI 只作为 ABI 检查样本提取到 build/，没有执行。

## 输入包已经记录的验证

保留 [原 port/CHANGELOG.md](../port/CHANGELOG.md) 与 [analysis/CHANGELOG.md](../analysis/CHANGELOG.md)。
其中记录模块曾在 11 个内核构建，HAL 在 glibc2.31–2.43 编译并过离线测试。
这不是本轮重新取得的证据，也不是十发行版完整 GUI/包安装测试。
原分析还指出部分 EL 构建使用源码树及异发行版编译器，不能直接等同于目标发行版原生 DKMS 安装。

## 本轮实际检查

- Windows Python 3.14.7：16 项 Python 回归测试通过；1 项依赖 Linux `octool_parity`
  可执行文件的坏 trace 回归跳过。跳过不算 Linux 离线测试通过。
- Python 文件 compileall 通过。
- Python 3.8 语法兼容检查、workflow YAML 结构检查通过；docs 相对链接与两次源归档
  内容一致性检查通过。归档文件名、脚本执行位、排除产物/私钥也已检查。
- Git Bash `-n`：10 个 shell 脚本、DKMS conf、GUI launcher 语法通过。
  RPM 模板提取的 post/preun 两个 scriptlet 语法也通过；这不是原生 RPM 事务执行。
- `check_elf.py` 在实际旧 GUI 上退出 1，按预期拒绝它充当 EL8 发行产物。
  读取到 GLIBC2.35、GLIBCXX3.4.29、ICU70、libjpeg.so.8；完整报告见
  [旧二进制 ABI 检查](validation/original-binary-abi.json)。
- `build_gui.py --preflight` 按预期退出 2：原 GUI 源码不存在。
- 新增七项云端 runner 的状态传播/源码解压回归，使用模拟执行器验证失败语义；
  没有启动 Linux 容器。完整 --plan 输出为 31 阶段，含十项内核与十项运行时；
  [cloud-plan.json](validation/cloud-plan.json) 明确记录 executed=false。
- 实际读取 Qt 5.15.18 官方 ImageFormats 配置，核实内置 TIFF/WebP 选项；
  核对 DKMS RPM 升级手册及实现。来源/文本哈希见 [云端指南](cloud-build.md#本次研究收敛的问题)。
- canonical ABI 与输入包 SHA-256 相同：
  `5f07bfefd2a96519756cad1567dfdf4b0c878db142f670e1b208cf39523800d8`。
- HAL C 文件与输入包相同：`ad7940bedb280ec1789831d360611ab473febb13cff1458c10aff1b1d3e45d29`；
  HAL 头文件相同：`d80a19e0e0551ba162e55653bde7975c50b74a1e811a08f6776071af43762dc1`。

初次 Python 测试因 Windows 沙箱无法访问 Python 创建的 0700 临时目录而失败；
获自动审批后在沙箱外重跑相同测试通过。Bash 的信号管道也受沙箱限制，语法检查采用相同方式完成。
这两项没有安装系统软件或访问硬件。

## 十目标当前状态

| 目标 | 本轮内核实编 | 本轮 GUI 编译/窗口 | 本轮 rpm/deb 安装 | 真机 MOK/对拍 |
|---|---|---|---|---|
| EL8 | 4.18.0-553.168.1.el8_10.x86_64 通过 | 待恢复/重构 | DKMS RPM 构建安装通过 | 未运行 |
| EL9 | 5.14.0-687.52.1.el9_8.x86_64 通过 | 待恢复/重构 | DKMS RPM 构建安装通过 | 未运行 |
| EL10 | 6.12.0-211.60.1.el10_2.x86_64 通过 | 待恢复/重构 | DKMS RPM 构建安装通过 | 未运行 |
| Ubuntu20.04 | 5.4.0-216-generic 通过，含签名试验 | 缺 GUI 源码 | DKMS deb 安装通过；GUI 包未运行 | 未运行 |
| Ubuntu22.04 | 5.15.0-194、6.8.0-138 generic 均通过 | 待恢复/重构 | DKMS deb 构建安装通过 | 未运行 |
| Ubuntu24.04 | 6.8.0-142-generic 通过 | 待恢复/重构 | DKMS deb 构建安装通过 | 未运行 |
| Ubuntu26.04 | 7.0.0-34-generic 通过 | 待恢复/重构 | DKMS deb 构建安装通过 | 未运行 |
| Debian11 | 5.10.0-46-amd64 通过 | 待恢复/重构 | DKMS deb 构建安装通过 | 未运行 |
| Debian12 | 6.1.0-53-amd64 通过 | 待恢复/重构 | DKMS deb 构建安装通过 | 未运行 |
| Debian13 | 6.12.111+deb13-amd64 通过 | 待恢复/重构 | DKMS deb 构建安装通过 | 未运行 |

以上仅模块包；GUI 包未产出。全部 artifact 已下载，本地分发行版包保存在 dist/packages/。
MOK 固件登记、加载及真实 MMIO 对拍未运行；容器签名试验不等于 Secure Boot 验收。
用户确认原 Linux GUI 源码丢失，授权检查 Windows 版后必要时重构。
当前提供的 Windows ZIP 路径仍不存在；EL8 Qt SDK 的独立编译正在运行。

以下为接入 Actions 之前的环境核实历史：本机 WSL 未安装，未发现 Docker/Podman；
当时当前工具没有远程 Linux 执行入口，Node 执行工具同样报告 win32。
用户随后明确纠正：要求使用 OpenAI/Codex 自带托管云端；此前按自有服务器索要 SSH 有误。
本轮进一步实测 Cloud CLI：可连接服务，任务列表为空，环境选择器没有具体环境可选。
没有提交云端任务、上传源码或创建云资源；详情见 [cloud-access.md](cloud-access.md)。
最后还在当前已连接的 GitHub installations 中只读检索了 `octool`，返回仓库列表为空。
这只说明当前连接范围内没有匹配项，不能推断用户在其他账号/组织没有该仓库。

机器可读的本轮检查摘要见 [local-checks.json](validation/local-checks.json)。

## 后续验证顺序

1. 使用已授权的公开 GitHub 仓库 Actions，修复并复测十项 kernel 任务。
2. 接入实际 GUI 源码，核对 qmake 工程、模块列表、第三方库、资源和标题，再运行完整矩阵。
3. 修复实测发现的包名/工具链/Qt 源码兼容问题，逐项记录日志，不豁免失败目标。
4. 确认十目标 GUI 和包安装全绿后，执行 [真机清单](hardware-acceptance.md)。
5. 收集每个目标实际日志，更新本表；没有真机证据的项目继续标未运行。
