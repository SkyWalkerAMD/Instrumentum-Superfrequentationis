# Granite Ridge / 9950X3D APOB 交接与整表导入验证

2026-11-01，使用 REA 6.7.0 继续研究 ASUS ProArt X870E-CREATOR WIFI BIOS 2503 / SMU 98.84.0。GUI 和 CLI 同步支持导入完整 APOB 文件，并在整表检查通过后选择唯一拓扑记录。新增 502 个原指令回放案例，连接了缓存 HOB 列表、APOB 描述符和拓扑记录选择。**八行字段单位、PM 到物理/OS 核心的绑定和第二 rails 独立写入回读仍未完成。**

目标为 9950X3D + ASUS X870E Creator。目标机器不在用户手边，实际 BIOS 未知；参考固件、合成回放和虚拟机检查不代表真机验收。

| 验证 | 实际结果 |
|---|---|
| [共享核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41285375187) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 37 CTest、495 场景组；7 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41285382159) | 13/13；11 Linux 目标各 39 CTest、81 CLI 场景组、1584 JSON 报告；安装、卸载、重装及无 Qt/显示环境运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41285387207) | 26/26；各目标及 EL7/EL8 基线 244 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [REA 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41287057271) | 1/1；REA 分析六个 UEFI 模块，SMU/MP5 各 12240 项编码自检；20 组回放结果与本地逐项一致 |
| 新增 HOB/APOB 回放 | 两份固定哈希实现共 502 案例：432 个有效选择、56 个实例/重复案例、12 个不可用案例、2 个零长度循环的受限回放 |
| APOB 整表导入 | 每个截断长度、首记录位置、完整 48 字节头、零长度/超大长度、有效拓扑之后的损坏尾记录、重复实例与空选择；GUI 失败后清空、CLI 零后端创建 |
| 本机扩展 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

**44/44 最终云端任务通过：43 个应用任务和 1 个研究任务。** 应用使用提交 `89307187039a0db6eef8b4e01bdb6904a92adf74`，研究使用 `72439f25b00ef859bfd9a57032128d9a50352571`；两者之间仅修正一处研究脚本的记录头元数据，应用与构建输入一致；最终 main 提交只补文档及验证记录，未把它另算一次应用测试。源码核对覆盖应用/构建文件、27 个研究源文件和 195 份研究证据。其中 18 组既有回放脚本未改；拓扑协议回放修正 48 字节头的元数据后重跑 5348 个案例，执行结果与此前相同。新增 HOB/APOB 回放的 502 个案例也在本机、云端各执行并比对。前期 REA 检索批次保留记录，不重复计入 44 项。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。EL7 指 Oracle Linux 7.9；Windows/macOS 只验证共享核心。生成 33 个安装包，最终桌面矩阵保存 1639 张截图。直接检查 EL7 和 Ubuntu 24.04 的新整表视图，截图来自合成文件。QEMU 检查驱动集成，未验证目标机调参、设备侧电压效果或 Secure Boot。

CLI：`amd-topology-decode --file apob.bin`。GUI：AMD tuning → Open firmware topology file。可以使用单条 group 3/type 3 原始记录，或只含一条该类型记录的完整 APOB 文件。上限为 1 MiB；完整记录头是 48 字节（16 字节字段加 32 字节 HMAC），这修正了此前只把前 16 字节称作记录头的表述。HMAC 不作认证，解析成功不表示实际固件身份已核实。

REA 追到 HOB GUID `30b174f3-7712-4cca-bd13-d0b8a8801997` 的读取路径；合成回放中的 HOB 列表已缓存，未运行 UEFI 初始化、断言处理或记录生产器。AMD openSIL 的公开头文件支持 HMAC 头部及嵌套编号字段的结构解释，但不同处理器的最大跨度不同，不能直接扩大本项目的 ASUS 参考布局。详见[交接路径与边界](amd-apob-handoff.md)。

剩余八行仍是核心 **0/4/12**、CCD **0/1/2/6/8**：还缺模型标定、计数事件身份和独立时间基准。APOB 记录的来源与目标机采用的实现、PM 槽位到物理 CCD/OS CPU 的关系、第二 rails 的物理通道和独立设备回读仍未证实。既有 Fmax 读写与保存值回读保留；未把软件镜像或命令完成状态当作设备实际状态。

交付目录 `dist/gui-cli-amd-granite-8930718/` 含 33 个安装包及源码。根目录源码包保留云端测试快照；推荐 `updated-source/`，含最终文档和八项既有用户扩展。扩展只作本机可执行检查，不声称与云端源码完全一致。`SHA256SUMS` 覆盖全部交付文件；研究工具不打入 GUI/CLI 安装包。详细记录见[验证 JSON](validation/amd-granite-ci-8930718.json)和[研究 JSON](validation/amd-granite-research.json)。
