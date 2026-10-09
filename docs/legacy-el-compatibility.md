# 原版 ELF 的 EL8–EL10 兼容性追踪

2026-10-08 后续：[启动权限、模块接入与失败路径](legacy-el-privilege-analysis.md)。
新证据区分 MMIO 线级兼容与旧 GUI 的模块初始化握手；不能仅凭预载模块和设备节点存在判定接入成功。
同日继续研究还确认[MMIO 错误完成字不兼容](legacy-mailbox-contract.md)，已有构建/成功请求检查没有覆盖该条件。

更新：2026-09-30。作者本轮要求继续反汇编并解决 EL8–EL10 兼容性。
本页专门记录**源码已丢失的原版 GUI**。基础版重构 GUI 的十目标全绿记录仍见
[verification-status.md](verification-status.md)，不能用它代替原版验证。

当前结论：原文件不变，私有运行库已在 EL8/9/10 实际完成装载并显示 Qt 对话框，EL10 使用
Mutter/Xwayland；原主窗口仍被 PCI/DMI 硬件检查拒绝。重构版新增栈属性门禁后十目标仍全绿。
没有把原版的 Not supported 窗口算成成功，也没有用仿真硬件或猜测寄存器值替代验收。

## 样本与复现

原始 `octool-linux.zip` 中 `octool` 的 SHA-256：
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
不改其 ELF、调用点、寄存器操作或 96 字节 / mmap 协议。
分析工具只读文件；原始二进制不进 Git，也不进公开 CI 产物。

```sh
python3 -m pip install --target build/reference-tools -r analysis/tools/requirements-reference.txt
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/elf-runtime-audit.py \
  build/input-audit/octool --el8-focus --site-limit 8 \
  --output docs/validation/legacy-el-runtime.json
```

[工具](../analysis/tools/elf-runtime-audit.py)通过 `.gnu.version_r` / `.gnu.version` 取符号版本，
通过 PLT 指令中的 RIP 相对 GOT 重定位找导入项，不假定 PLT 顺序或固定项长。
扫描 42,115 个去除地址/长度重复的函数范围，未出现解码不完整；共 1,195 个未定义动态符号。
[证据](validation/legacy-el-runtime.json)保留 111 个相关导入、完整引用计数、每项最多 8 个位置，
以及 19 个启动/硬件函数的地址、代码哈希、直接调用和字符串引用（首轮为 14 个，随后补充识别路径）。
省略 `--site-limit` 与 `--el8-focus` 可生成完整报告。
直接引用不等于运行时可达；虚调用、函数指针、回调和运行中加载的库不据此推断。

## 新定位的障碍

| 障碍 | 反汇编证据 | 处理方向 |
|---|---|---|
| EL9 缺 `hypot@GLIBC_2.35` | `QLineF::length` 的 `0x13f9a27`、`QLineF::unitVector` 的 `0x13f9b83` | 属于静态 Qt，不是某个超频面板；原文件有 NOW 绑定，不能靠“不点该页”避开 |
| EL8 缺 `GLIBC_2.34` | `_start` 调 `__libc_start_main`；Qt QThread、QLibrary 还需要新版本的 pthread/dl 符号 | 只捆绑 ICU/jpeg 不足以启动 |
| EL8 缺 `GLIBC_2.33/2.32` | Qt 文件系统、旧模块加载函数用新 stat/fstat；共享指针及 UART 代码引用 `__libc_single_threaded` | 包含数据符号，不把所有依赖都当作可随意别名的函数 |
| EL8 缺 `GLIBCXX_3.4.26/28` | UART 的 filesystem；Qt 字体 fallback 与 QStringList 的 PMR 分配器/析构/vtable | 换 soname 或只补异常函数不够 |
| EL8 缺 `GLIBCXX_3.4.29` | `__throw_bad_array_new_length` 有 1,087 处静态引用 | 广泛分布于应用容器代码，不能按单个页面修补 |
| ICU70 / JPEG8 | 动态导入含 `_70` 后缀；JPEG 还要求 `LIBJPEG_8.0` | 需要真实匹配的库；EL 的 JPEG62 不能用软链冒充 JPEG8 |
| 加载器改变程序路径 | `initilize_kernel_driver` 在 `0x36defe` 读 `/proc/self/exe`，再 `dirname`/`chdir`；`load_kmod` 也读取它 | 私有加载器与原 ELF 同目录，记录实际 `/proc/PID/exe`；模块由目标 DKMS 安装/外部加载，不能依赖三个旧 `.ko` |
| 启动时硬件访问 | MainWindow 在 `0x8e41d7` 调 `iopl`；`Rdmsr` 在 `0x36f812` 格式化 `/dev/cpu/%d/msr`，随后 open/lseek/read | 运行库修复不改变权限/lockdown 语义，也不能使这些调用自动转向新模块 |

这些是原样本观察，不是新增硬件寄存器定义。仍保留原有 MMIO 对拍和 HAL 自测。

## 私有运行库实验

原源码无法重编时，先验证应用目录内的匹配加载器 + 完整依赖闭包：

- [build-runtime.sh](../port/legacy/build-runtime.sh)只在可丢弃 Ubuntu22.04 容器内安装发行版库，
  用加载器 `--list` 解析原 ELF 的传递依赖，复制实际文件并记录来源、大小、SHA-256、包版本。
  不使用旧包 `mylib` 的过时 libc，也不执行旧 `run_lib.sh`。
- [run.sh](../port/legacy/run.sh)显式调用私有 `ld-linux-x86-64.so.2 --inhibit-cache --library-path`，
  原 ELF 与加载器同目录，glibc/libm/libstdc++/ICU70/JPEG8/其余依赖配套。
  不导出全局 LD_LIBRARY_PATH；拒绝继承 LD_PRELOAD / LD_AUDIT / LD_LIBRARY_PATH。
  采用原版已有的 xcb 插件，禁用本实验中的 GL 集成，EL10 通过 Mutter/Xwayland。
- [独立诊断 workflow](../.github/workflows/legacy-runtime.yml)手动触发，三条 Rocky 容器各自执行。
  旧 ELF 经仓库的**未发布草稿附件**鉴权下载，原文件 SHA 固定，令牌不传入容器。
  仅上传文本证据和实际窗口截图；原 ELF、运行库、核心转储和凭证都不上传公开 artifact。
- 测试进程 UID10001，无任何 capabilities、无宿主硬件设备、禁止网络、不模拟寄存器。
  校验 DT_NEEDED 全部来自私有目录，再检查实际 `/proc/PID/maps` 中没有混入宿主 `.so`。
  记录 loader/Qt 输出、窗口所有者 PID、标题、OS、镜像、宿主内核和实际可执行路径。
- `Work Tool` 主窗口持续可见 5 秒才算窗口门禁通过。只显示 Error 等对话框会单独记录为
  `dialog-only` 并使任务失败，不能把不支持硬件的提示当作主窗口成功。

原 ELF 草稿附件上传曾被自动审批拒绝，理由是云端测试授权未明确涵盖该文件外传；
作者随后明确选择“允许上传并测试”。已上传到 draft release 399746966 / asset 600130240，
GitHub asset digest 与原文件一致；匿名 HEAD 查询 release 和 asset 均为 404。
保持草稿状态，不发布这份输入。
私有库只解决用户态装载；没有解决 iopl/MSR、硬件寄存器正确性、Secure Boot 或原全部页面。
字体/区域设置、OpenGL、外部程序、动态 NSS/驱动模块还需逐项验证。容器使用云端宿主内核，
不能把 EL8 容器窗口成功描述成已在 EL8 的 4.18 内核完成硬件验收。
正式重构版仍走 EL8 静态 Qt 基线与系统 glibc，不把这套实验运行库塞进已有 RPM/DEB。

## 云端实测 36674306980：装载通过，主窗口未通过

[首轮运行](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36674306980)
使用提交 8854069。真实 ELF fixture 测试成功；三个旧 GUI 任务均因 `dialog-only` 失败，
没有放宽窗口门禁或修改原 ELF。

| 目标 | `/etc/os-release` | 私有加载器 | 实际 Qt 窗口 | 结果 |
|---|---|---|---|---|
| EL8 | Rocky8.9 基础镜像 | 72 个库文件，返回 0，无外部 `.so` | Error，Xvfb | 主窗口未通过 |
| EL9 | Rocky9.3 基础镜像 | 同上 | Error，Xvfb | 主窗口未通过 |
| EL10 | Rocky10.2 | 同上 | Error，Mutter/Xwayland | 主窗口未通过 |

容器部分包会由 dnf 更新，基础镜像 OS 字符串不能代替每个包的版本；后续补充 rpm 版本记录。
宿主内核均为 `6.17.0-1022-azure`，UID10001，实际 `/proc/PID/exe` 为私有目录里的加载器。
三目标的私有 libc/libm/libstdc++ 哈希相同，所有可见映射的 `.so` 都在私有目录。
这证明原始 ELF 已跨过装载/Qt 显示阶段，**尚未证明进入主窗口或任何硬件功能**。

原生日志保留缺失的 GLIBC/GLIBCXX/SONAME；此处发现 `ldd` 在打印这些错误时也可能返回 0，
因此诊断新增错误行检查，不把 `ldd` 零退出码当作依赖通过。
EL8/9 还报告 `Fontconfig error: Cannot load default config file`，EL10 没有；新增随运行库复制
字体配置并设置应用内 FONTCONFIG_PATH/FONTCONFIG_FILE。三者都有 PCI 配置和 `/dev/mem`
权限/设备错误。Error 的具体内容要由截图确认，不能只根据标题推断。
后续诊断新增只读 XGetImage 截图，不发送按键/鼠标，不绕过硬件识别。

## 云端实测 36674725439：字体修复、确定硬件检查分支

[第二轮运行](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36674725439)
使用提交 19d38f9，三个目标的 Fontconfig 错误均消失，加载器仍返回 0、无宿主 `.so` 混入。
已查看三张真实截图，均为 **Not supported!**，主窗口门禁仍失败。

![EL8 原版真实对话框](validation/legacy-el8-36674725439.png)
![EL9 原版真实对话框](validation/legacy-el9-36674725439.png)
![EL10 Xwayland 原版真实对话框](validation/legacy-el10-36674725439.png)

| 运行库 | EL8 原生 | EL9 原生 | EL10 原生 | 三目标同一私有运行库 |
|---|---|---|---|---|
| glibc | 2.28-251.el8_10.40 | 2.34-275.el9_8 | 2.39-121.el10_2 | 2.35-0ubuntu3.15 |
| libstdc++ | 8.5.0-20.el8 | 11.4.1-2.1.el9 | 14.3.1-4.4.el10 | 12.3.0-1ubuntu1~22.04.3 |
| ICU / JPEG | 原生加载缺 ICU70/JPEG8 | 同左 | 同左 | libicu70 70.1-2 / libjpeg-turbo8 2.1.2-0ubuntu1 |

原生诊断镜像只装显示环境，并非已安装原程序全部依赖；例如缺 hwloc/xcb 工具库可由发行版包补齐，
不能把日志里所有 `not found` 都说成 soname 不兼容。glibc 版本与 ICU70/JPEG8 是另行验证的根本障碍。

### 不能靠函数名判断硬件识别规则

[带指令的证据](validation/legacy-platform-gate.json)保留 5 个函数的解码结果、地址和哈希。
可复现命令：

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/elf-runtime-audit.py \
  build/input-audit/octool --el8-focus --site-limit 1 --instructions \
  --functions '^(_Z12check_if_amdv|_Z8isit_adlv|_Z17ReadPciConfigWordjj|_Z16libpci_read_wordiiii|_ZN10MainWindowC2EP7QWidget)$' \
  --output docs/validation/legacy-platform-gate.json
```

- `check_if_amdv` 位于 `0x4b9e10`，**不是 CPUID 检查**。它调用
  `ReadPciConfigWord(0,0)`，对返回值做 `& 0xffdf` 后与 `0x1002` 比较。
  `ReadPciConfigWord` 把第一个参数解成 bus/device/function，再调用内嵌 libpci，域固定为 0；
  这里读取的是 `0000:00:00.0` 的 offset0 word。不能以函数名推断检测的是处理器厂商。
- `isit_adlv` 位于 `0x378b30`，同样读取 PCI word。先要求 offset0 为 `0x8086`，随后检查 offset2：
  `(id & 0xffdf) == 0x4648`、`id == 0x4660` 或 16 位运算 `(id + 0x5900) <= 0x80`。
  这里只记录旧程序的数值条件；没有把这些 ID 推断成用户某款 CPU/主板，也没有用于新 GUI。
- MainWindow 的 `0x8e4267` 调第一项，非匹配路径继续第二项；再调用 `is_it_asus`。
  未通过时在 `0x8e42bd` 引用 `Not supported!`，`0x8e42f7` 显示 QMessageBox，关闭后 `exit(0)`。
  因而“进程最后退出码 0”也不代表进入了主窗口。
- `is_it_asus` 使用 DMI 主板字符串及部分 VRM 检查；`getmobo` 下游 `test_dmi_get_mbi`
  读取 `/sys/firmware/dmi/tables/{smbios_entry_point,DMI}`，尝试 EFI systab 和 `/dev/mem`。
  这条路径未采用 `/sys/devices/virtual/dmi/id/board_name` 这样的普通身份文本。
- 云端日志中 PCI config 打开失败、`/dev/mem` 不可读，与上述启动路径相符。
  继续采集原生 CPUID 与只读 sysfs PCI/DMI 身份，区分云端硬件不匹配和访问权限问题。
  仅改变 QEMU 的 CPUID 型号不会改变 libpci 读取的宿主 PCI 配置，因此不把 CPU 仿真当作该检查的验证。

原版保留了这些识别/权限要求，不通过修改它、伪造 PCI/DMI 或强制返回成功把测试变绿。
作者四套目标平台必须实读 PCI/DMI/权限后才能判断该分支；Windows 截图不能代替旧 Linux 分支的验收。
下一步真机证据应包含只读 `lspci -Dnn -s 0000:00:00.0`、`board_vendor`/`board_name`、lockdown 模式，
不需要公开序列号/UUID。涉及访问方案的变化须继续保持旧 MMIO 线级协议。

### 原生环境进一步核对（36675803213）

[第三轮证据](validation/legacy-el-run-36675803213.json)：EL9 的实际 CPUID 是
`AuthenticAMD / EAX=00a10f11`，EL10 为 `GenuineIntel / EAX=000a06d1`，两者仍然 Not supported。
两台云端机器均没有 `/sys/bus/pci/devices/0000:00:00.0/{vendor,device}`，DMI 文本为
`Microsoft Corporation / Virtual Machine`。这进一步说明 AMD CPUID 本身不会通过旧程序的 PCI 判断。
没有据这些值推断用户四套真机的结果。

此轮 EL8 在构建显示镜像时失败，尚未执行 GUI：DNF 镜像的 AppStream 提供了 gcc-8.5.0-29，
但所选 BaseOS 元数据缺少匹配 libgcc/libgomp。官方 `dl.rockylinux.org` 上这三个确切 RPM 的 HEAD
均返回 200。诊断 Containerfile 将 BaseOS/AppStream 指向同一官方入口并刷新元数据；不使用
`--skip-broken` 绕过必需依赖，不把这次仓库失败归因于 OCTool 或计为窗口通过。

## ELF 可执行栈与 EL 策略验收

又一项实际发现：原 ELF 的 `PT_GNU_STACK.p_flags=7`（RWE），十个已归档重构版原生 GUI 均为
6（RW），见[逐文件哈希与标志](validation/gnu-stack-audit.json)。这属于 ELF 属性，不是源码重编推测。
EL 目标机是否因其 SELinux 域/策略拒绝原程序的 execstack，仍需真实 AVC 日志确认；容器窗口不验证
该策略。不能从缺少原汇编源码就假定 executable-stack 标志可以直接清掉。

生产打包的 [check_elf.py](../port/tools/check_elf.py)现解析程序头表，要求唯一 PT_GNU_STACK 且无 PF_X，
防止后续恢复 NASM/汇编功能时把旧属性重新带入新 GUI。新增真实 ELF 布局的 RWE/缺失标记负例；
不修改原文件，也不建议关闭目标机 SELinux。
属性含义参考 [GNU ld 的栈选项](https://sourceware.org/binutils/docs/ld/Options.html)，
策略拒绝示例见 [Red Hat execstack 诊断](https://access.redhat.com/solutions/43215)；示例是另一程序的
受限域，不作为 OCTool 在默认 EL 桌面必然被拒绝的证据。

## 本轮最终复测记录

- [原版诊断 36676593044](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36676593044)
  使用 446ff37：EL8 官方仓库修复后构建显示环境成功，三目标均执行到了原 Qt 对话框；加载器返回0，
  无外部 `.so`、无 Fontconfig 错误。三张截图与第二轮逐字节相同，均为 Not supported；三个 GUI 门禁
  仍失败。详见[完整机器可读结果](validation/legacy-el-run-36676593044.json)。
- 此轮 EL8 实际为 GenuineIntel，EL9/EL10 为 AuthenticAMD；三者均无 PCI00:00.0 节点且 DMI 为
  Microsoft Virtual Machine。实际 maps 的 `[stack]` 也是 `rwxp`，与旧 ELF 的 RWE 属性一致。
- [生产回归 36676590930](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36676590930)
  同为 446ff37，23 个 job 全部成功：[证据](validation/actions-run-36676590930.json)。
  新 GNU_STACK 门禁通过，18 项 Python 门禁和 ELF PLT/CET/GOT fixture 继续参与；
  既有 HAL/MMIO 协议、内核模块、重构 GUI 源码在本轮均未改变。

重跑原版诊断（拥有此仓库草稿附件访问权的账户）：

```sh
gh workflow run legacy-runtime.yml --ref main -f asset_id=600130240
gh run list --workflow legacy-runtime.yml --limit 5
# 用上一条输出的 run ID，下载仅含诊断证据的产物
gh run download RUN_ID --pattern 'legacy-*-evidence' --dir build/legacy-evidence
```

原 ELF 保存在未发布输入草稿；私有库只在临时 runner 工作区组装，不放入公开 artifact 或现有 GUI RPM/DEB。
当前仍需作者的真实目标平台完成[硬件验收](hardware-acceptance.md)中的 PCI/DMI、权限、MOK、
主窗口及 MMIO live 对拍；本轮没有拿到这些真机结果。

## 2026-10-08 离线逆向对兼容性的补充

原版ELF、重构基础版、原包装器实验分别记录，不能共用一个“GUI通过”的结论。
当前作者暂不能采集四机身份，已选择继续离线研究；以下结论不要求现在再次采集。

| 层次/问题 | 已查明或已验证 | 尚未完成 |
|---|---|---|
| 原ELF库依赖 | 私有loader/库闭包使EL8–10实际到达Qt提示框 | 原Work Tool主窗口及所有动态路径 |
| 重构基础GUI | EL8静态Qt基线；十目标编译/装包/窗口，EL10为Mutter/Xwayland | 原全部调参面板恢复、真机硬件读写 |
| 平台识别 | [原PCI/DMI分派550项](legacy-platform-dispatch.md)、[主板菜单592项](legacy-menu-routing.md) | 四机实际进入分支；营销型号不代替原谓词 |
| 原直接MSR | [两槽148项](legacy-msr-failures.md)证实写命令栈字节被失败读复用，保留busy位 | 其余407处静态形状的调用上下文及原GUI恢复策略 |
| 原MMIO错误 | [56项](legacy-mailbox-contract.md)确认完整done=1条件；[可选guard](legacy-mailbox-guard.md)有界退出 | 完整GUI的错误提示/继续运行、与capture组合及真机对拍 |
| AMD配置选择 | [385项](legacy-amd-initialization.md)确认返回域、不可达比较及部分表覆盖 | 真实CPUID/PCI状态、消息含义和单位 |
| AMD命令传输 | [448项](legacy-amd-transport.md)确认固定BDF、端口等待耗尽仍发送、libpci错误值透传 | 上层完整业务、真实PCI后端、权限和固件时序 |
| AMD MP1/反馈 | [563项](legacy-amd-mp1.md)确认身份辅助只读BDF0、21次轮询及失败后Applied | 真实Qt/完整构造/固件与其余调用者 |
| Intel NGU/MMIO地址 | [266项](legacy-intel-ngu.md)从两完整槽到96字节请求；读加基址，写直接传偏移，101次busy耗尽仍写 | 平台正确地址契约、其他调用者、硬件定义及恢复实现 |
| Qt业务恢复 | 39类451个元方法入口及NVL控件绑定已核对 | 451个函数体的业务含义与全部面板，不以入口数量算完成率 |

这批研究没有修改原GUI调用点或MMIO协议。直接MSR、I/O端口和内嵌libpci没有因安装新`/dev/mydev`
模块而被重定向；相应错误等待也不能靠换glibc/ICU/JPEG库消除。下一步实现须分别处理访问错误、
完成条件、上层反馈与平台范围，不能把错误返回伪装成成功应答。新GUI可以通过已有HAL恢复已核实的
只读/原始访问，未核实的传感器单位和固件命令仍保留为原始证据。

`fa4c541 / 37766384139`的十目标23/23结果包含2501项原指令实验及新GUI回归，
[详细证据](validation/legacy-initialization-ci-fa4c541.json)已保存；后续AMD传输448项单独回归。
完整工作量与真机缺口以[覆盖台账](reverse-engineering-status.md)为准，仍不能签“逆向完毕”。

## 相关原始资料

- [glibc：显式启动动态加载器](https://sourceware.org/glibc/manual/2.39/html_node/Dynamic-Linker-Invocation.html)。
- [glibc：显式加载器与 `/proc/self/exe` 的差异](https://sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker-Introspection.html)。
- [GitHub：草稿 release 仅有 push 权限的账户可列出](https://docs.github.com/en/rest/releases/releases)。
- [原始多发行版分析](../analysis/docs/linux-porting.md)，保留历史实测边界。
