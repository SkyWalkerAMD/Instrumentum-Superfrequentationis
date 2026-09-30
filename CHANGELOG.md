# CHANGELOG

## 2.0.1 — 2026-09-30（未发布，待完整 Linux/GUI 验证）

### 公开 GitHub 仓库与实际 Actions 接入

- 用户明确授权创建公开仓库 Instrumentum Super-accelerandi，立即用 Actions 测试，
  后续开发完成再考虑转私密；仓库标识采用 Instrumentum-Super-accelerandi。
- GitHub CLI 与连接器均确认登录账号 SkyWalkerAMD；本源码目录作为仓库根。
- 现有十目标 workflow 使用标准 ubuntu-24.04 托管 runner；为 kernel/desktop job 增加
  简明目标名称，artifacts 设置 7 天保留期。GUI 缺失仍使 baseline/总门禁失败。
- 根 README 使用用户指定标题，程序/模块/协议/源码包名不改。新增 docs/github-actions.md，
  记录仓库路径、Actions 结构、计费依据和以后转私密的影响；历史 Cloud 限制不再阻挡 Actions。
- 扩充 Git 忽略规则排除旧 ZIP、环境文件和签名密钥；源目录检查未发现密钥或访问令牌。
- 实际 run、编译失败与修复结果在下方续记，不把首次推送准备当成验收通过。

### 输入与范围

- 从用户的 octool-linux-refactor.tar.gz 恢复源码，在原 port/、analysis/ 上扩展。
- 当前工作目录没有 GUI 源码；octool-linux.zip 是预编译产物；OCTool0528.zip 未找到。
- 遵守“不改 GUI 调用点/协议”：canonical ABI、HAL 源码保持逐字节不变，MMIO dispatch 未改。
- 新增根 docs/ 知识入口、多发行版指南、逐项真机验收清单、验证证据记录。
  历史指南里建议的 GUI 全 HAL 集成不作为本轮授权范围执行。

### 修复导入源码的构建与验证缺陷

- port/kmod/Makefile 的 all 规则原本只有 `$1`，不会构建模块；恢复显式 Kbuild 调用与 KDIR 支持。
- class_create 保持能力探测，不按 LINUX_VERSION_CODE 分支；从固定头文件排版 grep
  改为使用目标内核 include/flags 的一参数/二参数编译探测。原因是 vendor backport 与
  Debian 分离头文件布局；两种都失败时立即报错，避免错误工具链被判作旧 API。
  未发现并宣称新的 API 版本敏感点；新探测仍须在 Linux 实编验证。
- 删除 ABI 头的重复副本，模块包装头改为引用 ../abi，DKMS payload 保留两级目录。
- MODULE_VERSION 更新为 2.0.1；DKMS/source archive 版本取根 VERSION，使用点号。
- hwio_smoke 增加正式 Makefile 目标；模块后端缺失时失败，核数与 sysconf 对照。
- parity 拒绝空/截断 trace，没有稳定可读地址时返回 INCONCLUSIVE/2；
  selftest 加入 acceptance exit-code 判定。parity-run 不吞掉 GUI 崩溃/非零退出。
- tests Makefile 增加 ABI/header 依赖；clean 不再删除真实采集 corpus。

### 运行时与原生打包

- 新增 EL8 Qt5.15.18 静态 SDK 构建入口，锁官方源 SHA-256；保留 AT-SPI/D-Bus、xcb。
  Qt 自带 JPEG/PNG/zlib/PCRE/harfbuzz，关闭 ICU；默认 EL8 原生 GCC，避免新 GLIBCXX 需求。
- 新增原 GUI qmake shadow-build 接入 manifest。缺源码/许可证就失败，不生成占位 GUI。
- 新增可在 Windows/Linux 运行的 ELF 解析门禁，核对 GLIBC2.28/GLIBCXX3.4.25/CXXABI1.3.11，
  拒绝动态 Qt、ICU、libjpeg、OpenCV 和绝对构建路径依赖。
- 新增 xcb launcher、desktop 文件及实际窗口冒烟；EL10 使用 mutter+Xwayland，不能回退到 Xvfb。
  冒烟要求真实进程拥有可见主窗口并保持 5 秒，早退/超时均失败。
- 在原 packaging/ 中完成 DKMS 源码 staging、原生 RPM spec、deb control/hook 模板和统一构建器。
  原 Debian debhelper 模板不完整且 compat13 不适合所有目标，改由 dpkg-deb 与 dpkg-shlibdeps 构建。
- DKMS 安装不吞错误；枚举已安装内核头并检查 installed/vermagic；支持根据 CONFIG_GCC_VERSION
  选择 gcc-N，内核自动升级构建也重新选择。没有新增内核版本号判断。
- 提供旧 DKMS sign_tool helper；现代 DKMS 使用自己的 MOK 设置。包不覆盖全局密钥配置。
  CI 使用临时证书检查 sign-file；真实 MOK 登记/加载必须真机完成。
- udev 限定 octool_hwio 子系统及 mydev*、root:root 0600；不自动加载模块。

### CI 与源码交付

- 新增 targets.json 十目标矩阵，补入 Debian11/12/13，Ubuntu22.04 同时取 GA/HWE 头文件。
- kernel jobs 独立于 GUI：loopback、parity selftest、HAL、Kbuild/modpost、签名、DKMS 包安装。
- baseline 在 EL8 产出 SDK/GUI；desktop jobs 对每个目标本地重编 GUI、生成包，
  在新容器测试发行/native GUI、包卸载/重装。任一失败/跳过均阻止总 gate。
- 保存镜像 digest、包列表、kernel release、编译日志、modinfo、ABI 和窗口日志。
- 源码归档入口生成 octool-2.0.1-src.tar.gz 与 SHA-256；不使用下划线版本名，
  排除二进制、旧 ZIP 和私钥。完整发布归档要求 GUI preflight 通过。

### 本轮实际验证与限制

- 第一轮 Python 回归 9 通过、1 个需 Linux 编译产物的测试跳过；Python compileall 通过。
- Git Bash 对 10 个 shell 脚本、DKMS conf、launcher 的语法检查通过。
- Python3.8 语法、workflow YAML 结构、docs 链接、ABI/HAL 冻结校验、源归档两次生成一致性通过。
  源归档脚本执行位、名称及二进制/私钥排除策略已检查。
- 旧 octool ELF 实际被新 ABI 门禁拒绝：GLIBC2.35、GLIBCXX3.4.29、ICU70、libjpeg.so.8。
- GUI preflight 实际因缺 GUI 源码返回 2；没有跳过后宣称成功。
- 当前已连接 GitHub installations 中检索 octool 返回空仓库列表，未取得替代源码或 CI 入口。
- Windows 临时目录/Bash 管道沙箱问题经自动审批在沙箱外执行只读/临时文件测试解决。
- 未运行 Linux C 离线测试、Debian/EL/Ubuntu 模块实编、Qt 编译、rpm/deb 构建安装、
  云端 CI、GUI 窗口、MOK 或真实 MMIO 对拍。不能称“十目标全部完成”。
- 详细状态见 docs/verification-status.md，真机验收步骤见 docs/hardware-acceptance.md。

### 选择方案 2 后的云端入口和补充研究

- 当时误把用户“我选2”理解为提供已有 Linux 构建机；用户随后明确要求使用 OpenAI 托管云端。
  不能再把 SSH 信息作为用户待提供项；本次纠正与环境核实见下节。
  本地再次核对仍只有 octool-linux.zip 与重构树，没有 OCTool0528.zip/原 GUI 工程。
- 新增 port/ci/run-matrix.py，直接在 Linux Docker 宿主执行与 Actions 相同的容器内入口。
  全矩阵 31 阶段；支持 --plan、内核分项和目标子集。部分成功不会报告 full_matrix_passed。
- 每次运行生成独立源码快照并记录 SHA-256，各阶段独立源码副本；镜像 pull 后记录 inspect，
  后续按同一 image ID 启动。每目标保存日志、产物、退出码、状态和耗时；中断/超时保持失败。
  超时只停止本次唯一名称的容器，不清理用户其他容器或旧构建证据。
- 缺 GUI/baseline 失败不影响十项内核结果收集；desktop/runtime 的依赖失败明确标 blocked。
  requested_scope_passed 和 full_matrix_passed 分开；hardware_acceptance 始终为 not_run。
- bootstrap 增加容器标记、目标 ID/os-release/架构和 mode 校验，避免用错镜像。
  修复 process substitution 隐藏 Python 目标读取失败；kernel 模式只安装内核/打包依赖，
  与 Qt、X11、Wayland 开发依赖分离。Actions 与手工命令同步传容器标记。
- 修正 RPM 升级时仅最终卸载清 DKMS 的问题：add/remove 配对使用 --rpm_safe_upgrade。
  上游通过父进程识别 RPM 事务，add 保持在 RPM scriptlet 内直接调用。
  同版本已注册的 add 退出 3 需核实状态才接受；其他错误不吞掉。
  注册 helper 强制重建/安装，避免同版本新 release 留用旧对象；CI 增加 MODULE_VERSION 核对。
  原生升级/降级仍待 Linux 实测，当前不宣称已验证事务完整性。
- 实际读取 Qt 5.15.18 的 ImageFormats configure.json，确认系统 TIFF/WebP 的自动选择条件。
  新增 -qt-tiff -qt-webp，ELF 门禁拒绝 libtiff/libtiffxx/libwebp/libwebpdemux/libwebpmux；
  保留输入分析已记录的 TIFF/WebP 图像支持。上游链接/文本哈希和 DKMS 研究来源放 docs/cloud-build.md。
- GUI manifest 拒绝绝对路径和带 '..' 的输入，避免资源 staging 路径绕出私有目录。
- 源归档改为剪枝遍历 build/dist，避免扫描云端任务中重复的源码树；树内文件链接物化，
  云端只解压普通文件/目录并拒绝越界路径、链接和设备节点。版本/包名风格不变。
- 新增七项云端调度/归档回归：31 阶段计数、缺 GUI、单目标失败、runtime 失败、
  子集成功、截断进度不误报全绿，以及归档路径/链接拒绝和正常解压。
  Python 回归现为 16 通过、1 个 Linux-only 测试跳过；模拟调度不是实编证据。
- 最终 Python3.8 语法/compileall、ABI/HAL 冻结、docs 链接、重复源归档 hash、
  12 个 shell/conf/launcher 和两个 RPM scriptlet 的 Bash 语法、workflow 结构检查通过。
  实际 --plan 确认完整 31 阶段、Debian 子集 3 阶段；不存在的 target 按预期退出 2。
- docs/cloud-build.md 记录云机接入、Debian 内核先行、全矩阵命令、日志结构和总状态语义；
  docs/README.md、multi-distro.md、verification-status.md 与机器可读摘要同步。
- ABI、HAL 和 MMIO dispatch 未改变。Linux C 测试、十目标实编/包安装、GUI、MOK/对拍
  仍未运行；继续保留未完成状态，不能把本地静态检查称作十目标验收通过。

### 云端归属纠正与入口实测

- 用户明确“你用你的云端来测试”；撤回此前要求自有服务器/SSH 的错误前提。
  README、docs 知识入口、执行指南、验证状态和机器可读摘要同步更正。
- 再次实际检查执行工具：Node 代码工具返回 win32/x64，本地 shell 在 Windows F 盘。
  注册工具没有直接创建/附加 Linux 云沙箱的入口。
- 检查已安装 Codex CLI 的 cloud/exec/list 帮助，确认可向已有环境提交任务，要求 ENV_ID。
  只读 list 在沙箱内联网失败；自动审批后执行相同命令成功，返回空任务列表。
- 进一步实际打开 Cloud 环境选择器，加载完成后仅有全局筛选项，没有具体环境可选，
  随后退出。未提交云任务、未上传代码；不能从空列表推断所有工作区没有云环境。
- 依据已读取的 OpenAI 官方 Cloud 文档，记录正确路径是配置/选择托管项目环境。
  Docker 嵌套执行能力仍需实测，不把服务可连接当成已具备十目标构建环境。
- 新增 docs/cloud-access.md，保留 Linux runner 作为通用实现；源包重新生成以携带纠正记录。
  本轮只更正文档/验证元数据，Linux 模块、GUI、原生包和真机验收状态仍为未运行。

## 导入的历史日志（保留原文）

- [硬件访问层与 MMIO 对拍开发日志](port/CHANGELOG.md)
- [二进制与发行版移植分析日志](analysis/CHANGELOG.md)

历史验证范围只按原日志解释，不向本轮的 GUI、包或 Debian 内核结果外推。
