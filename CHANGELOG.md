# CHANGELOG

## 未发布 — 2026-10-08（原机器码原生执行与错误保护层）

- 继续研究原包装函数缺少错误出口的问题。新增可选 mailbox-guard.c，在已核实的动态 write 边界
  检查失败/短写/高位错误/非法完成值和完成超时，输出诊断并退出74；成功仅透传一次原请求，
  不改96字节、结果、完成字、GUI调用点、HAL或模块，不把失败转为伪造读数。
- 保护层用程序头加载基址、八个原函数字节及准确write返回地址限定匹配；无关调用者透传。
  记录其不解决内核write阻塞、旧模块结果发布竞态、全局请求并发、冷启动和硬件识别等边界。
- 新增原生测试工具与合成后端：将已授权公开的664字节原指令逐字节链接入微型测试ELF，
  对比未保护PIE和保护后的PIE/非PIE/显式ld.so；初版291项逐条核查请求及错误结果。
  后端只访问匿名内存和虚拟fd，不执行原完整GUI或任何硬件访问；代码完成不代表云端通过。
- 新增只读权限 legacy-guard.yml，EL8编译一次后用于EL8/9/10及Ubuntu22.04；每次main推送、PR
  和手动触发都会运行；portability十目标kernel阶段也以nobody运行相同测试。
  保护层暂为实验产物，不自动改变现有发行包/旧GUI启动器。复现步骤和边界详记
  docs/legacy-mailbox-guard.md；实际云端结果待运行后追加。
- 首次独立运行37730137601的EL8编译及GLIBC上限检查已完成，但runner在已chown给UID10001
  的输出目录写builder-image.json时被拒绝，任务失败且未发布构建产物。修正为先记录镜像、
  再转交输出目录所有权；不提升执行容器权限。首次失败保留，不计为跨发行版执行成功。
- 正式37730136773的EL8未保护PIE/保护PIE均完成，但非PIE首例SIGSEGV。实际下载ELF发现
  binutils2.30默认0x400000 text与原始低地址段混排，生成从0开始的LOAD/0x40 PHDR。
  修正测试链接布局为非PIE text-segment=0x10000、max-page-size=0x1000；不修改原机器码，
  不降低mmap_min_addr。新增拒绝低地址LOAD/W+X/可执行栈门禁，加入16项非PIE未保护基线，
  每目标现为307项；修复后云端结果见37731171839。
- [云端实测37731171839](docs/legacy-mailbox-guard.md)：EL8构建及EL8/9/10、Ubuntu22.04四个
  原生glibc执行job全绿，同一EL8构建guard在四者各通过307项，保护库符号上限<=GLIBC_2.28，
  SHA-256均为57ca919e302573ca954a64bb69936c64845bd4fc00f426f2455766117d201d2c。
  支持原错误/无应答路径在原函数忙等前诊断并fail-fast，成功请求仍原样返回。这是保护层模拟验证，
  不代表完整旧GUI、真实内核模块、硬件、并发、Secure Boot或真机主窗口通过；逐项记录在
  docs/validation/legacy-mailbox-guard-run-37731171839.json。

## 未发布 — 2026-10-08（MMIO 邮箱逆向续查）

- 新增只读 audit-legacy-mailbox.py：沿用现有 GUI 审计器，增加 ET_REL 按节重定位指令证据及 DWARF
  成员偏移，固定原 GUI 和三份旧 .ko 的 SHA。归档 13 个 GUI 函数、每模块 17 个函数；模块不装载、
  原字节不进入 Git/公开产物。新增真实编译器 fixture，验证不同函数节同偏移不会串重定位。
- 新增 Unicorn 2.1.4 有界模拟器，仅执行八个原 MMIO 包装函数的未改字节，write/邮箱均为合成。
  本地 Windows 已通过 56 个行为核查；精确核验 96 字节、保留字段、令牌、写值截断及实际返回分支。
  确认原版等待完整 64 位值 1，而非任意非零；不检查 write 返回，无超时。
- 发现当前新模块 rc 高位编码与旧 GUI 等待条件不兼容，合成 ENOMEM 完成字使全部八函数达到
  512 指令上限仍在轮询。HAL 的非零等待和采集器 completed 判定无法证明旧调用者兼容。
  这是尚未修复的缺口，不用清零 errno 隐藏失败；本轮不改生产 GUI、ABI/HAL、kmod 或打包行为。
- 三份旧模块 mmap 均经同一 file.private_data 指向同页，第二映射不是另一种 mailbox；DWARF 与
  指令字段偏移相符。原初始化三次 pagemap 计算的结果没有送入请求，实际 token 来自 read 前 8 字节。
  记录未核验 read 和失败 mmap 泄漏；fallback 虽不支持任意长度，但实际四处直接调用仅用 1/2/4/8，
  没有据此误认 SMBIOS/平台页面崩溃原因，也不推断面板单位。
- 核对旧模块 mem_read32：6.8/6.17 样本先 done 后 result，6.2 样本反向；记录潜在并发读取风险，
  未声称真机已复现。历史兼容指南、当前验证状态和真机清单同步收窄结论。
- 新增独立 legacy-mailbox.yml，复用已经获授权的草稿 ELF，在非 root、无网络/能力的容器模拟；
  仅发布 JSON，保持生产和原版 GUI 严格窗口门禁。云端模拟及本次回归尚待运行。
- 自动审批拒绝含约 1 MB 派生反汇编证据的公开提交/推送，认为既有草稿 ELF/日志授权不包含该公开范围；
  未执行 git add/commit/push。已询问作者是否公开，或仅保留本地。继续完成本地核查与可审阅源码包。
  56 组 Windows 模拟通过；3 项 Linux 编译器 fixture 明确跳过。文档链接、ABI 固定哈希与可重复源包门禁通过。
- 作者随后明确选择“①允许公开反汇编证据及模拟结果，继续云端测试”。据此恢复公开派生证据与研究工具，
  继续使用现有草稿 ELF 执行云端有界模拟和完整十目标回归；原始 ELF/.ko 公开范围没有扩大。
- 后续自动审批认可证据公开授权，但要求移除新研究工作流的 contents:write；已收紧为 contents:read，
  由实际云端执行核验草稿样本读取，不因新增研究任务扩大仓库写权限。
- `d0c6913` 已公开提交全部派生分析；`legacy-mailbox 37724202337` 的只读草稿下载实际 HTTP403，
  尚未执行模拟。根据这个结果采用权限更小的方案：公开八函数测试样本共 4,144 字节，仅含 664 字节
  已授权函数指令及必要地址；保持 contents:read，删除下载/令牌/asset_id，完整原 ELF/.ko 不公开。
- 增加完整 ELF 导出与固定 SHA 样本导入，两种模式的 56 个观测结果在本地完全相同；导出样本逐字节相同，
  保存完整 ELF 提取模式与样本模式的分别证据。云端验证样本哈希，原 ELF 整体哈希在本地导出时验证。
  同一 56 例接入每次 push/PR 的 matrix 门禁和 JSON artifact，不把复现已知失败分支称作兼容性修复。
- `d0c6913 / portability 37724109109` 的 23 项已全部成功，三个真实 ELF/ET_REL fixture 在 Linux 实际执行；
  十目标模块、GUI、打包、安装和窗口门禁继续通过。该轮尚不包含新加的公开样本 56 例自动门禁。
- `0e11ed3 / legacy-mailbox 37724841275` 使用只读公开样本方案成功，受限 Linux 容器实际跑完 56 例。
  同提交 portability37724823205 的 matrix 新门禁也已通过；分别下载两份真实 artifact，和 Windows
  完整 ELF/公开样本模式逐项比对，全部请求、返回值、停止地址和指令计数相同。每份 24 例返回、
  32 例达到指令上限，后者是已知调用者行为，不是原 GUI 错误兼容性修复。原始 JSON/镜像/哈希归档 docs。
- `0e11ed3 / portability 37724823205` 最终 23/23 成功，新模拟门禁与三个真实编译器 fixture 全过，
  kernel/desktop 精确覆盖十目标，23 份 artifact 的元数据与 digest 已归档。相比本轮起点 917b986，
  GUI、ABI、HAL、kmod、packaging、port/tests 未改；收尾仅补充实测证据与文档。

## 未发布 — 2026-10-08（EL8–EL10 原版启动/权限研究）

- 接续 9 月 30 日结果，校验原 ELF SHA 不变。扩充只读 ELF 审计：显式选择零长度 NASM
  STT_NOTYPE 标签，按同节下一符号/节末报告有界字节跨度，不把它们当作推导出的函数边界；
  增加命名对象 BSS 初值与函数内 RIP 相对基址引用。原有 42,115 函数统计口径不变。
- 新增可复现 audit-legacy-privileges.py 和 docs/validation/legacy-privilege-analysis.json，
  20 个函数、15 个汇编区间、MY_KMOD_LOADED 的 13 处引用。新增不执行的真实汇编 ELF fixture，
  覆盖同址别名、边界、节末、截断指令、数据节拒绝和对象初值/引用。
- 确认 my_iopl 使用内联 syscall 172，原端口访问是直接 IN/OUT，MainWindow 不检查 iopl 返回；
  setuid/setgid 也有直接 syscall 路径。仅替换 libc 符号不能迁移全部硬件访问。
- 确认原 MSR 包装不检查 open/lseek/read/write，读取失败会继续使用未初始化的栈缓冲区；
  不据失败数值恢复传感器单位。启动中的 en_ec_decoding/SIO 分支含实际写调用，不能称为只读冒烟。
- 确认原模块握手依赖 BSS 标志及旧 .ko 文件名；只有 init_module 成功/EEXIST 后才打开设备。
  修正历史文档把“MMIO 兼容”直接外推为“预载新模块后原 GUI 必然接入”的表述。
  当前内核签名模块按旧文件名提供只是待真机验证候选，未增加自动加载或修改生产包。
- 根据 Linux 上游源码区分 MSR 读/写 lockdown 路径与 HAL 项目策略；不是新增版本号条件。
  记录私有普通用户 launcher 与 root/LD_PRELOAD parity 捕获流程不能直接拼接，真机清单同步。
- 为独立 EL 诊断添加可选 strace 系统调用观察，保持 UID10001、零 capabilities、无设备/网络、
  原 ELF 不变和严格主窗口门禁；增加实际权限/CPU 掩码记录。初次提交时云端新结果尚待执行。
  本轮没有改 GUI、ABI/HAL、内核模块或其打包源码；详细结论见 docs/legacy-el-privilege-analysis.md。
- `268ac92 / legacy run 37716693694`：新增两个 ELF fixture 测试通过，三条 EL 诊断均成功跟踪原 ELF。
  每目标 4 次 NASM 直接 iopl + 1 次 libc iopl 都返回 EPERM，setuid/setgid 也为 EPERM；
  maps 重定位后直接 syscall 返回地址精确对应 0xa7d6ef。CapEff/CapBnd 为 0，NoNewPrivs=1，
  Seccomp=2；不能把受限容器的 EPERM 归因为目标机 lockdown。
- 三目标目标 PCI config/原始 SMBIOS 入口和 /dev/mem 均为 ENOENT，未在对话框前观察到模块加载或
  /dev/mydev 打开，所以模块握手结论仍单独标记为静态分析。加载器返回 0、无混入宿主 .so，
  三张 Not supported 截图与此前相同；严格 GUI 门禁继续失败，不把诊断观察成功冒充主窗口通过。
  全部原始跟踪和 maps/result 进入 docs/validation，原 ELF 和私有库仍不进入公开源码或产物。
- 同一 `268ac92 / portability run 37716682302` 共 23 项全部成功；十目标模块/DKMS、GUI 原生构建、
  rpm/deb 安装与真实窗口、既有离线门禁和新增 ELF fixture 均通过，job/artifact 证据已归档。
  按本轮起点 b43578d 比对 GUI、ABI、HAL、kmod、packaging 无差异；本地源码门禁确认哈希、
  docs 链接和源码归档可重复。收尾仅补充实测证据与文档，不改变研究工具或运行逻辑。

## 2.0.1 — 2026-09-30（未发布，十目标 CI 通过，待真机验收和后续面板恢复）

### 原版 ELF 的 EL8–EL10 兼容性继续分析

- 按作者要求回到原 Linux ELF。新增只读 elf-runtime-audit.py，按 GNU 版本表、PLT/GOT
  重定位与 Capstone 解码定位运行库依赖。42,115 个函数范围、1,195 个动态导入、0 个解码缺口；
  docs/validation/legacy-el-runtime.json 保存 111 个相关导入及 14 个启动/硬件函数证据。
- 把 hypot@GLIBC_2.35 追到静态 Qt 的 QLineF::length/unitVector；把 EL8 的
  GLIBCXX_3.4.26/28 追到 filesystem 和 Qt PMR，3.4.29 异常函数有 1,087 处静态引用。
  因此不采用替换 soname、改版本字符串或只补 hypot 的方案。
- 原版通过 /proc/self/exe 定位模块和 chdir；新增私有加载器与原 ELF 同目录的运行库实验，
  从 Ubuntu22.04 包取得配套加载器、glibc 与完整依赖闭包，不改原 ELF、不写宿主系统库，
  不将 LD_LIBRARY_PATH 传给子程序。记录依赖闭包、库哈希、包版本和运行中 maps。
- 新增独立 legacy EL runtime diagnostic 手动矩阵：Rocky8/9 Xvfb、Rocky10 Mutter/Xwayland，
  UID10001、无 capabilities/设备/网络；固定旧 ELF 哈希，草稿附件鉴权取样且不公开原文件。
  Work Tool 持续可见才通过，对话框不冒充成功。初始提交云端待跑，结果追加 docs 与本日志。
- 新增真实 ELF fixture 测试 PLT、CET PLT、-fno-plt GOT 及版本化数据导入；协议和生产 GUI
  均未改动，既有十目标门禁继续保留。详细路径和限制见 docs/legacy-el-compatibility.md。
- 原 ELF 的 GitHub 草稿附件上传被自动审批拦截，文件尚未上传；已询问作者该具体文件的
  外传授权。在答复前仅继续源码/文本证据和分析工具回归，不绕过拦截传送原文件。
- 作者随后明确允许该 ELF 上传并测试；草稿 asset 600130240 的 SHA 与输入一致，匿名读取
  release/asset 均 404。8854069 / run 36674306980 完成：EL8/9/10 私有加载器均成功，72 个
  配套库文件，maps 没有宿主 .so；三目标均出现 Error 对话框，主窗口门禁保持失败。
  新增截图采集以核实对话框内容；发现 ldd 报版本缺失也可能退出 0，补充错误文本记录。
  EL8/9 缺 Fontconfig 默认配置，补齐私有字体配置路径；没有修改旧 GUI/硬件调用或协议。
- 19d38f9 / run 36674725439：三目标字体错误消失，截图确认均为 Not supported!，不是加载器
  报错。进一步反汇编确认 check_if_amdv 实际取 PCI 00:00.0 offset0，非 CPUID；isit_adlv
  取同设备 vendor/device ID，之后才是主板/DMI 判断。不能凭函数名选择 CPU 仿真来验证此分支。
  新增 5 函数逐条指令证据及原生 CPUID、只读 PCI/DMI 身份采集，保留旧程序与失败门禁。
  docs/legacy-el-compatibility.md 记录完整数值条件、私有/原生库版本和三张真实对话框截图。
- 8854069 / portability run 36674187756 与 19d38f9 / run 36674723621 均完整 23 job 成功；
  重构基础版十目标继续全绿。原版的独立 legacy 诊断是失败结果，未混入或冒充生产 GUI 成功证据。
- e573b19 / legacy run 36675803213：EL9 实际为 AuthenticAMD、EL10 为 GenuineIntel，均没有
  00:00.0 sysfs 节点、DMI 为 Microsoft Virtual Machine，仍是 Not supported。EL8 此轮在 dnf
  阶段遇到跨仓库镜像元数据不一致（gcc29 缺 libgcc/libgomp29），没有运行 GUI；官方确切 RPM
  HEAD 均 200。诊断镜像改为同一官方 BaseOS/AppStream 入口并清理元数据，待复测。
- 原样本 PT_GNU_STACK=7(RWE)，十目标已归档重构 GUI 均为 6(RW)。生产 ELF 检查新增程序头
  解析、唯一栈声明及禁止 PF_X 门禁，避免后续汇编恢复引入 EL 策略兼容性问题；未修改原 ELF。
  Windows 离线测试 18 项中 17 通过、1 项 Linux-only 跳过，实际 EL8 GUI 新门禁通过。
  SELinux 是否拒绝旧程序需真机 AVC，已补充身份/权限/策略验收步骤，未据此宣称默认 EL 必然拒绝。
- 446ff37 最终代码复测：portability run 36676590930 的 23 个 job 全部成功，新增 GNU_STACK
  门禁实际用于 EL8 发布构建及打包，原有十目标内核/GUI/包/窗口和离线协议门禁保持通过。
  legacy run 36676593044 的 EL8 仓库问题修复；三个原版任务均成功装载配套库、字体正常、无
  宿主 .so 混入，但均因 Not supported 对话框未进入主窗口而失败，结果与正式重构版分别归档。
  三目标最终截图与第二轮字节相同。整个本轮 gui/、port/abi/、port/hal/、port/kmod/ 无内容改动。

### Windows 包取得与 GUI 第一阶段重构

- c48a38f / run 36670288030 首次完整全绿，2026-09-30 04:53:35 UTC 完成：23 个 job 成功，
  包含十目标 kernel、十目标 desktop、EL8 baseline、matrix 与 gate。十目标各 12 项 QtTest 结果全过，
  HAL loopback/真实 transport 包装/parity selftest 与 17 项 Python 测试继续为门禁。
  EL10 实测 Mutter49.4/Xwayland24.1.9，发行/native 两个窗口分别保持可见通过，错误 PID 不被接受。
  保存每目标镜像 digest、11 个 kernel release、GUI/模块包及日志哈希；真机 MOK/加载/对拍仍未运行。
  当前仅基础信息、原始 MSR/MMIO/PCI 与 AMD PStates 只读子页，不将 CI 全绿宣称原平台全部功能恢复。
- 作者明确选择先恢复 AMD PStates 的可核实只读频率与原始值，暂无电压/电流定义。
  新增 gui/pstates.*，显式 CPUID 厂商/Family1Ah/硬件 P-state 能力门禁；按 PstateMaxVal 限制读取，
  配置频率与实时频率分开标注，禁用/保留编码/错误均不显示伪造零值；完整保留 64 位原始 MSR。
  GUI CPUID 封装在直接后端临时绑定并恢复工作线程 CPU 亲和性，模块后端保持原 HAL/ABI。
  无启动扫描、无写入、无 VID/电流猜测。新增五项 Qt 回归，预期合计 12 项结果，待云端实编验证。
  docs/amd-pstates.md 记录 Linux 固定提交、AMD PPR 页码/哈希、旧 ELF 函数地址和算法差异。
- 作者确认四套 CPU：Z790/i9-14900KS、W790 ACE/w5-2565X（18 核，明确纠正先前笔误）、
  W890E-SAGE SE/Xeon 658X、TRX50 SAGE/Threadripper PRO 9995WX；BIOS 均未知。
  写入 docs/platform-recovery.md 及独立 JSON，不把型号确认等同于 CPUID/拓扑/寄存器实测。
- 18c18c5 / run 36666985989：十个 kernel job 和九个 desktop job 成功；新 Qt SDK 从源码重建成功，
  GUI 最高 GLIBC_2.28、GLIBCXX_3.4.15、CXXABI_1.3.9，7 项 Qt 测试结果全部通过。
  EL10 在 fresh runtime 安装阶段发现仓库不提供 xwininfo；总门禁按设计失败。
  用独立 Xlib window-probe 替代 xwininfo/xprop，仍要求 IsViewable、实际 GUI PID 与窗口标题匹配，
  并加入错误 PID 的负向检查；辅助程序仅 CI 编译，不进入产品包。继续使用 Mutter/Xwayland，无 Xvfb 回退。
- 作者新增四组参考图：Z790 7 张、W790 ACE 2 张、W890E-SAGE SE 2 张、TRX50 SAGE 6 张。
  已全部逐图查看，2,173,180 字节原样纳入 docs/references/platforms，清单与 SHA 写入 docs/validation。
  记录 Intel Controls 电压域差异、AMD Per CCX OC/PStates 标签；不把样本值用作写入默认值。
  新增 ELF UI 字面量分析器，静态交叉核对 12 函数/1064 引用，补全 Frequency in MHz 等截断标签。
  截图不含 CPU 商品型号/BIOS，已具体补问；原 PStates 的 VID=306mv 只记为旧显示证据，未推导算法。
- 作者进一步选择继续恢复平台面板；已请求 CPU/主板/BIOS/优先面板，尚未收到具体信息。
  静态整理两平台共有的 23 个代表面板类及元对象槽，写入 docs/platform-recovery.md 和 JSON 证据。
  不从类名推出 CPU 支持范围、寄存器地址或单位；基础版构建验收继续独立推进。
- c63582b / run 36666155701：十个 kernel job 全部通过；Ubuntu24 的 fortified pread
  实际命中 1 次，transport 修复获得验证。EL8 GUI/测试程序链接、7 项 Qt 测试结果和 ABI 下限门禁通过。
  Ubuntu22/24、Debian12/13 的 native GUI、安装包与两个真实窗口冒烟通过。
- 同轮真实 GUI 矩阵发现四类差异：EL8/10 release payload 没有 DWARF，RPM debugsource 空清单失败，
  仅 GUI runtime spec 关闭自动 debug_package；EL9 curl-minimal 与 curl 冲突，按 /usr/bin/curl
  能力安装；Ubuntu20/Debian11 缺 wl_proxy_marshal_flags，qmake 明确限定平台插件为 xcb/offscreen，
  Wayland 桌面通过 Xwayland；Ubuntu26 xwfb-run 缺 xauth，显式加入两类 Xwayland runtime 依赖。
  bootstrap 配方改变会使 SDK 缓存/旧 seed 失效，下一轮真实重建 Qt，不能冒用旧配方的验证。
- 可见窗口冒烟增加四页截图，便于检查真实渲染。新增可复用 PE/ELF Qt 元对象静态分析器与输入
  SHA/清单证据；ELF 720 个 Qt 字符串表、Tool.exe 358 个，两者共有 243 个类名。
  分析器不还原 C++ 源码、不推断寄存器语义，不执行输入二进制。
- run 36665814220 实际完成 EL8 GUI 与测试程序链接，SDK seed/cache 校验成功；GUI 回归退出 1。
  首轮日志在失败时未打印测试文件，现用 finally 保留失败细节；修正模拟确认只调用 dialog.done
  而不点击按钮的问题，并清理误导缩进告警。未把这次链接成功当作窗口/功能验收成功。
- 新 transport 测试在 Ubuntu22/24/26 的 PCI 短读断言失败，其他七目标通过；补拦截有界缓冲区
  的 __pread_chk 并输出实际命中计数，等待下一轮验证 distro fortify 路径。
  从已生成 Qt SDK .prl 核实 zstd 和各 xcb 链接接口，补 Debian native GUI 开发包，EL8 Qt 配方不变。
- 新 SDK 缓存允许用已成功 run 36661824443 的精确归档哈希引导；实际比对当前两份构建配方
  与 bfa992d 完全相同。配方变化/旧 artifact 到期走源码构建，哈希错误失败；始终重编 GUI。
- 用户新提供 E:/Download/Edge/ 两个 ZIP；Windows 包 79,363,256 字节、158 项，SHA-256
  02a50088a94000f651f8783b32bf1ff63a48fd4e6a3909983c7c1ed0a5a417a3；Linux 包与此前相同。
  Windows 无工程/C++/.ui/PDB，Tool.exe 的 debug 项只有类型 13；静态读取 Qt 元对象确认
  rw_msr/rw_memory/rw_pci 和相应菜单入口。没有执行任何附带程序、驱动或 BIOS。
- 用户选择首批“基础信息、MSR/MMIO/PCI 原始读写，随后恢复平台面板”。新增 gui/ Qt5 qmake
  工程，复用 HAL/ABI；严格整数/宽度/范围验证、后台串行访问、具体目标写入确认及错误展示。
  启动无寄存器访问，写后不擅自回读，输入变化清空读结果。PCI 限于原后端支持的 0000 域/256 字节。
- 接入发现 HAL 真实设备传输把 CPU user_id 覆盖为 per-open token，旧 loopback 未覆盖该层。
  保留 CPU 命令目标编号，MMIO 仍回填旧令牌；模块对无效 CPU 返回 EINVAL，不回退到另一个 CPU。
  修复设备/MSR/PCI 短传输错误误报成功。ABI/HAL header 未变，HAL C 新哈希写入门禁和 docs。
- 新增 syscall-wrap transport 离线回归，以及真实 Qt 控件/请求编码测试；后者在每个目标 GUI
  编译后运行，失败停止打包。正常窗口冒烟仍要求无假设备的真实可见主窗口。
- 为 EL8 SDK 增加按 Qt/依赖脚本哈希的 Actions 缓存与 SHA-256 恢复校验，GUI 仍每次实编。
  首批新 GUI 沿用已有 HAL 的 GPLv2，未改变旧二进制/第三方许可；详细范围见 docs/gui-phase1.md。
  此提交尚待首次真实 GUI/打包/窗口矩阵，不预先标全绿。

### 仓库命名与多轮真实 Linux CI 验证

- bfa992d / Qt SDK run 36661824443 成功：EL8 GCC8.5/glibc2.28 实际完成 Qt5.15.18 静态
  构建、安装、AT-SPI 私有宏和 libqxcb.a 检查、SDK 归档与上传。下载后 tar SHA-256 为
  3aaa15077aa3c15989c6a0f7d40215d8210a1c6aa765d9e07590c16ca3823b55，与云端相同。
  压缩包内检查确认 Wayland 静态插件存在；qmake ELF 的 GLIBC2.28/GLIBCXX3.4.15/CXXABI1.3.8
  通过现有门禁。只证明 SDK 工具，不外推为 GUI 已链接/运行或 TLS 功能通过。
  新增 docs/qt-sdk.md、完整配置摘要和机器可读 run 证据；SDK 保存在 dist/qt-sdk/。
- 同一 bfa992d 的 portability run 36661823555 十个 kernel job 再次全绿。
  新摘要保存 job ID/结论；逐路径 git diff 确认模块、ABI/HAL、自测和打包源码与 3f7f942 相同，
  本地 dist/packages/ 保留第六轮已验证的十目标包并生成 SHA256SUMS，不冒充最新 artifact hash。
  baseline 仍缺实际 GUI，desktop 未运行，总门禁保持失败；没有把 SDK 成功计入 GUI 验收。
- 再次核对用户给定 Windows ZIP 路径，包括沙箱外查询，仍无此文件。
  文档纠正“路径待提供”为“路径已提供但文件不可读”，避免继续索要已丢失的 Linux 源码。
  源码交接包按 octool-2.0.1-src.tar.gz 重新生成，包含实际成功/失败证据与真机清单；
  它仍不是通过完整 GUI 发布门禁的发行版。
- 审阅 Qt 上游 top-level configure 和 qt_configure.prf，确认 AT-SPI 宏位于版本化 private header，
  改为扫描安装后 QtGui 头目录核验该私有 feature。
  对 summary 位于 qtbase/ 的推断在 run 36661368766 被实际早期检查否定；不能根据 configure
  的 cd 推断顶层 qmake 的 OUT_PWD。现按实际文件选择 config.summary 或 qtbase/config.summary，
  打印所选路径，缺失在 make 前明确失败。该次 atspi 探测已实际为 yes。
- EL8 Qt SDK 首轮 run 36659551087 确实进入 C++ 编译，官方 Qt 源 SHA 校验通过。
  实时 configure 日志显示 accessibility=yes 但 AT-SPI bridge 缺依赖而关闭。
  核对 Qt5.15.18 src/gui/configure.json 的 atspi-2 探测条件，补 libatspi2.0-dev /
  at-spi2-core-devel，并显式要求该 feature，安装后检查生成头的 bridge 宏。
  首轮日志保留为诊断，修复后重跑；诊断 runner 使用 JOBS=4，常规构建默认仍为 2。
  同一配置还显示 OpenSSL=no；原 GUI 网络功能缺源码无法确认，未把 SDK 声称为完整 GUI 验收。
- 第六轮 3f7f942 / run 36660297759：十目标完整 kernel job 全部通过，包含初装、同版本重装、
  卸载再安装；所有目标最终版本/vermagic 查询通过。回归确认每次安装刷新 depmod 修复有效。
  新证据写入 docs/validation/actions-run-36660297759.json；dist/packages/ 更新为这轮包。
  README、构建指南与云端指南同步当前状态，保留早期失败记录，GUI 总门禁依然失败。
- 第五轮 818af54 / run 36659952626 的生命周期测试找出 EL 三项和 Ubuntu26.04 重装缺模块索引问题。
  rpm_safe_upgrade 锁已生效；DKMS force rebuild 删除最后一个模块时清掉 modules.dep，
  随后 install 又跳过 depmod。将索引建立收敛到包内 dkms-register：每次安装后实际 depmod，
  按模块名核对版本/vermagic 再报告成功；移除 CI 只初始化一次索引的补丁。
  新的真实事务回归保留，继续验证完整卸载和重装。
- 新增 docs/gui-recovery.md，详细记录原源码丢失后的恢复/重构授权、Windows ZIP 仍不可读、
  历史二进制分析与本轮证据的区别、GUI 功能/硬件单位的待确认边界及恢复后的 CI 接入顺序。
- 第四轮 a8a5f1e / run 36659550123：十个目标 kernel job 全绿，共 11 个 release。
  真实 HAL/C 自测、17 项 Python 测试、Kbuild/modpost、临时证书签名、rpm/deb 构建和 DKMS
  installed/vermagic/version 查询均通过。已下载全部 artifacts，包复制到 dist/packages/。
  各镜像 digest、kernel release、包和 .ko 哈希写入 docs/validation/actions-run-36659550123.json。
  总门禁继续因 GUI 未恢复失败，不能写“完整矩阵全绿”。
- 将同版本包重装、卸载、再安装加入独立 kernel job，实际验证此前未测的 RPM safe-upgrade
  与 Debian prerm/configure，不再因 GUI 缺失而跳过模块包生命周期检查。
- 第三轮 e530b51 / run 36659298597：Debian11 和 Ubuntu20.04 kernel job 通过；
  其他目标 class_create 在 C 编译阶段通过，但 modpost 重读 Kbuild 时没有 try-run，触发误判。
  探测限定到 Makefile.build 的实际 C 编译阶段；clean/modpost 不执行探测。
  EL9 5.14 vendor 内核实际单参数，维持能力检测，不引入版本号分支。
- 用户给出的 Windows 包仍为 F:\OpenAI\Codex\project\octool\OCTool0528.zip；
  再次 Get-Item/目录枚举确认当前仍不存在。原 GUI 接口恢复还不能开始。
  新增手动 Qt SDK diagnostic 工作流，允许在恢复 GUI 期间独立验证 EL8 静态 Qt；
  与正常 baseline 共用构建函数，SDK 成功不计为 GUI 或完整矩阵成功。
- 用户确认原 Linux GUI 源码已丢失，授权先检查 Windows 版本，必要时重构 GUI；
  Windows 包的实际路径仍待提供。ABI/MMIO 保持兼容，未知硬件字段/单位仍不得推断。
- 第二轮 f2c494b / run 36658900518：Debian11 官方快照安装和离线测试通过；
  诊断日志确认 class_create 探测失败的直接原因是 GNU Make 将 `\#include` 反斜杠传入 C。
  改用独立 class_create_probe.c，随两条 DKMS staging 路径携带；不再用 Make 字符串生成 C。
  前轮添加的 flags 完整性保留，但不把它误记为已证实的直接根因。
- 第二轮 EL8 实编/签名/RPM 构建完成且 DKMS installed，但按名称 modinfo 失败。
  核实 DKMS 3.4.3 在没有 modules.dep 时跳过 depmod；headers-only 容器先初始化目标索引。
  EPEL dracut 钩子也不适用于没有启动映像的容器，仅在 bootstrap 禁用 post_transaction，
  不改发行包/用户 DKMS 设置；真机清单继续要求实际内核升级、签名和加载。
- 按用户后续要求，将公开仓库改名为 Instrumentum-Superfrequentationis，展示标题为
  Instrumentum Superfrequentationis；已核实 GitHub 新地址、公开状态并同步 origin。
- 首次提交 22d0ea0 的 Actions run 36657953069 实际完成：Ubuntu20.04 的 5.4.0-216-generic
  模块实编、C 离线自测、17 项 Python 测试、临时证书签名、deb 构建和 DKMS 安装通过。
  十目标总门禁失败；baseline 因缺原 GUI 源码失败，desktop 未执行，不将其豁免。
- EL8/9/10 离线测试通过，kernel-devel 安装后没有 /lib/modules/<release>/build，导致目标枚举为空。
  在一次性容器 bootstrap 中从 /usr/src/kernels 的配置读取 release 并补链接；
  校验已有链接目的地，禁止以宿主 uname 或推测版本代替目标头文件。
- Debian12/13、Ubuntu22.04/24.04/26.04 的 class_create 两种签名探测都失败。
  对照上游 Makefile.lib，补齐 parse-time 探测缺失的 compiler_types.h 强制包含与 module flags；
  保留每个签名的编译器 stderr，两种均失败时打印原始错误，避免仅看到通用失败。
  仍是编译能力探测，没有新增内核版本判断；实际修复结果由下一轮云端验证决定。
- Debian11 尚未进入编译：live security 索引引用的包返回 404。核实 Debian bug #1147093、
  archive.debian.org 尚无该 security Release；官方 20260831T235959Z 快照的签名索引和
  两个原来 404 的实际包均返回 200。仅在 Debian11 测试容器固定 main/security 快照；
  仅该历史源使用 check-valid-until=no，继续要求 APT 签名，不用 trusted=yes。
- bootstrap 日志从安装开始写入 artifact，安装失败也有完整证据。
  首轮结果与后续修复依据见 docs/actions-debugging.md、docs/verification-status.md。

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

### 首次本地验证与当时限制（后续 Actions 结果见本版本首节）

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
