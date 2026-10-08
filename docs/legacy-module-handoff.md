# 原版 GUI 的模块接入：重复加载握手

更新：2026-10-08。接续[原版 EL 权限分析](legacy-el-privilege-analysis.md)。
这是原版 GUI 的接入路径研究，不改变 GUI、96 字节请求、mmap 邮箱、MMIO 操作或模块 ABI。

## 结论

旧 GUI 的 `load_kmod` 并非只检查 `/dev/mydev`。它调用 `init_module`，成功和 `EEXIST`
两条分支都会设置进程内 `MY_KMOD_LOADED` 标志。Linux 上游 4.18、5.14 和 6.12 的源码都表明：
重复模块名检查发生在模块签名检查和足以解析模块身份的格式/元数据检查之后。因此可行的候选
接入方式是让旧加载器读到**当前内核对应、已签名且未压缩的 `octool_hwio.ko` 原始字节**，并以
旧 GUI 实际打开的候选文件名放在它的程序目录。复查已确认，当初始化进入加载分支且标志未置位时，
按 `peter_kernel.ko` → `peter_kernel_old.ko` → `peter_kernel_new.ko` 固定顺序尝试，
每次按加载标志决定是否继续；这一分支不按内核版本选择文件。目标硬件仍需跟踪实际是否到达此分支，
以及第一个候选是否加载/重复加载成功。模块的文件名不改变其 ELF 内部名；两者内部名都是
`octool_hwio` 时，内核可返回真实 `EEXIST`，旧 GUI 再按原逻辑置位。不需要 syscall 返回值注入。

这比“只预载模块”多了必需的用户态条件：旧加载器必须能打开、完整读取一个真实模块镜像。
也比拦截 `syscall()` 少一层伪造状态的风险。实际 Rocky/RHEL 补丁树、Secure Boot 强制签名、
旧 GUI 真硬件路径和桌面权限仍须分别实测；上游标签源码不是发行版补丁源码，也不是实机结果。

## 两端证据

原 ELF SHA-256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
固定调用点在 `load_kmod` 的 ELF 虚拟地址 `0x36d8f7`，调用 glibc `syscall(175, image, size, args)`；
返回地址为 `0x36d8fc`。调用后先判定返回值为 0；否则读取 errno，并只在 errno=17 时进入
`0x36ddb0`。该分支在 `0x36ddcd` 写入 `MY_KMOD_LOADED=1`。证据来自固定哈希 ELF 的指令，
没有运行或修改完整原版 GUI。

内核源码的重复检查顺序：

- [上游 Linux v4.18 `kernel/module.c`](https://github.com/torvalds/linux/blob/v4.18/kernel/module.c#L3436-L3454)：
  `module_sig_check`、ELF header 检查、解析/分配后，`add_unformed_module` 对已存在的 live 模块返回 `-EEXIST`。
- [上游 Linux v5.14 `kernel/module.c`](https://github.com/torvalds/linux/blob/v5.14/kernel/module.c#L3676-L3738)：
  签名、ELF、section/modinfo 等检查和分配后，在 `add_unformed_module` 检查重复名并返回 `-EEXIST`。
- [上游 Linux v6.12 `kernel/module/main.c`](https://github.com/torvalds/linux/blob/v6.12/kernel/module/main.c#L2692-L2732)：
  签名检查先于 ELF/模块元数据检查；`early_mod_check` 在 `layout_and_allocate` 前检测已加载模块。
  [重复处理函数](https://github.com/torvalds/linux/blob/v6.12/kernel/module/main.c#L2513-L2548)
  对 `MODULE_STATE_LIVE` 返回 `-EEXIST`。

三代实现的阶段位置不同，但都不允许把空文件、截断文件、错误架构模块或未被强制签名策略接受的
字节当成“已加载”证明。Linux `init_module(2)` 文档也列出 `EEXIST` 为模块同名已加载，且说明调用
需要特权：[man-pages](https://man7.org/linux/man-pages/man2/init_module.2.html)。
真实调用前仍有 `CAP_SYS_MODULE` 检查；预载模块不会绕过这个旧 GUI 系统调用的权限检查。

## 复查后接续研究：固定候选顺序与文件失败路径

2026-10-08 从完整固定 SHA ELF 重新解码并核对两个函数，与已归档指令逐项相同：
`load_kmod`（0x36d5e0，2233 字节）及 `initilize_kernel_driver`（0x36dea0，615 字节）。
精简证据见[loader 复查 JSON](validation/legacy-loader-review.json)，完整可重现入口仍为
`analysis/tools/audit-legacy-privileges.py`。没有执行完整 GUI、文件加载 syscall 或硬件访问。

| 初始化调用位置 | 传入文件名 | 后续判断 |
|---|---|---|
| 0x36dfc7 | peter_kernel.ko | 0x36dfcc 检查标志；非零直接进入设备初始化 |
| 0x36dfd9 | peter_kernel_old.ko | 0x36dfde 同样检查 |
| 0x36dfef | peter_kernel_new.ko | 0x36dff4 检查；仍为零则返回 |

`load_kmod` 读取 `/proc/self/exe`，以最后一个斜杠前的目录拼接斜杠和传入文件名。
显式私有 ld.so 运行时，此符号链接指向 loader；现有运行时把 loader 与 octool 放在同一目录，
可以满足这个目录假设。仅改变工作目录或把文件放在别处不一定有效。

另发现一个接入前必须规避的旧错误路径：`open`（0x36d8a3）失败后进入打印分支，
0x36da75 又跳回 0x36d8b2；随后仍用该失败 fd 调用 `fstat`。`fstat`（0x36d8b7）、
`malloc`（0x36d8c7）、`read`（0x36d8d7）的结果没有检查，最终将原先从 stat 栈槽取得的长度
送入 `init_module`。所以不能把“前两个文件不存在，让第三个自动兜底”当作健壮的启动方案；
也不能由到达 syscall 推断模块已完整读入。此结论是指令控制流分析，未在真机制造失败文件。

候选交接因此进一步收敛为：在受控参考目录优先提供第一个正确的 `peter_kernel.ko`，
保证它实际对应当前内核有效的 octool_hwio 镜像，并在调用前核实文件内容、长度、签名和权限。
这不修改原 GUI 调用点，也不增加自动复制/链接部署。仍须证明目标机进入该初始化、权限符合，
并解决已知邮箱错误等待；不能据固定候选顺序直接宣布旧 GUI 在 EL 上已通过。

## 权限和启动限制

新模块 `hwio_init()` 只注册字符设备区域、cdev、class 和 `/dev/mydev`，不做 MMIO/PCI/MSR/端口访问；
硬件访问发生在后续设备请求。模块 `open()` 默认要求 `CAP_SYS_RAWIO` 和 `CAP_SYS_ADMIN`；
打包 udev 规则又将设备节点限制为 root:root 0600。旧 GUI 还直接执行 `iopl`、IN/OUT、MSR 和
`/dev/mem` 路径，因此 EEXIST 接通只解决模块标志/设备选择这一个启动条件，不能单靠它让普通桌面
用户运行，也不能使 lockdown 下被拒绝的旧硬件路径自动可用。见[权限分析](legacy-el-privilege-analysis.md)。

创建 DKMS 包或桌面启动器时，不应仅凭这一研究结果自动给普通用户添加 capabilities、放开 udev
权限或用 root 启动 GUI。授权后的真机桌面策略需按目标设备/Wayland 会话单独验收。

## 真实内核探针

`analysis/tools/kmod-eexist-probe.c` 和 `test-kmod-eexist.sh` 用当前 runner 内核实测：先用 `insmod`
加载该仓库编出的真实 `octool_hwio.ko`，再从同一文件逐字节读出镜像并直接调用 `init_module`。
探针要求 errno 精确为 `EEXIST`，并记录运行内核、vermagic、模块 SHA-256、class 设备与 `/dev/mydev`。
全面复查后还将 class/devnode 存在和显式卸载成功设为硬门禁，已有设备节点时拒绝运行。
清理只卸载本次脚本先前成功加载的模块。该模块初始化仅注册设备；早期探针不打开 `/dev/mydev`。
接续能力修正新增只读open/ioctl/close查询，详见[能力识别](module-capabilities.md)；仍不发96字节
设备请求、不访问宿主物理硬件，也不执行原 GUI。当前 runner 不启用 Secure Boot 强制签名测试；
记录 signer 字段只为说明本次确切加载的是哪份镜像，不能代替 MOK 验收。

对应 Actions workflow 可手动触发，也只响应专用分支 `research/kmod-eexist-probe` 的 push；主分支
普通 push 和 PR 不会把项目模块装入 runner 内核。产物只保留结构化 JSON。此探针证明的是选定 GitHub runner 内核上的真实模块重复加载语义，不
证明 EL8/9/10 各自带补丁的运行内核或 Secure Boot MOK 路径。正式运行结果按下节追加。

## 复现及后续边界

本轮边界修正提交 `a9ffbcd` 的[探针 37737505020](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37737505020)
已通过，包含新增卸载门禁；新模块 SHA、vermagic 和完整输出见
[复查回归记录](validation/review-fixes-a9ffbcd.json)。这是该提交源码的加载/重复加载/卸载结果，
与下方旧版本探针分开记录，验证范围仍不包含 EL vendor kernel、MOK 或硬件请求。

```sh
make -C port/kmod KVER="$(uname -r)" KDIR="/lib/modules/$(uname -r)/build"
sudo bash analysis/tools/test-kmod-eexist.sh \
  "$PWD/port/kmod/octool_hwio.ko" "$PWD/build/kmod-eexist/result.json"
```

### 云端实测 37732862984

[真实内核探针 run 37732862984](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37732862984)
基于 `05535742a705a40ad0f550e360452404bc5033c2`，`duplicate-load` 单 job 成功。Actions runner 为
Ubuntu 24.04.5 / `ubuntu-24.04` image `20261004.327.1`，运行内核 `6.17.0-1022-azure`。
目标模块以当前内核 headers 实编，Kbuild `class_create(name)` 能力探测通过；实际加载字节 SHA-256 为
`271908a2cdbc914fbf5b2d37a0b4800dca97ac548d2ba44eab408842aa7fca3d`，vermagic 匹配该内核。

脚本首次 `insmod` 成功（该步骤未 trace kmod 使用的底层 syscall），实际出现
`/sys/class/octool_hwio/mydev` 和字符设备 `/dev/mydev`；随后把同一个
未压缩 `.ko` 文件读入内存，直接执行 `init_module(image, length, "")`，观察到精确结果 `-1`、errno 17
（EEXIST），探针退出码 0。整个过程没有 open `/dev/mydev`、发送设备请求或触碰物理硬件。结构化结果和
Actions artifact 元数据保存在[本次验证 JSON](validation/legacy-kmod-eexist-run-37732862984.json)，
artifact ID 为 `11529992803`、digest `sha256:b0c69a81f46dcf63b239046ceaa232a231fb468ccf03fbafa929d80eec662354`。
原始脚本 JSON 把首次 `insmod` 标注成 `finit_module`，但没有跟踪该 syscall；归档记录已纠正为只确认
`insmod` 命令成功。第二次 `init_module` 是探针直接调用，返回码与 errno 有明确记录。

更正后脚本在提交 `a1b0e614501a7c539849f29f22ee9e4824552be5` 上再次运行：
[run 37733259162](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37733259162)
成功，仍为同一 `6.17.0-1022-azure` kernel 和模块 SHA，`initial_load_syscall_traced=false`，
第二次 `init_module` errno 17，设备 class/node 均存在。artifact digest 为
`sha256:27c66da4371c5500ac41063d6cd0d0377604357214579f5debb80524ea974e14`；其 run/job/artifact
元数据已并入上方验证 JSON。此复跑确认更正标签没有改变探针实测结果。

研究分支归档文档的提交 `0f01f2e4e113822458c0b37a34cdee2343b9b7af` 再次触发探针：
[run 37733967112](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37733967112)
成功。日志确认 Ubuntu 24.04.5 runner、内核仍为 `6.17.0-1022-azure`，项目模块 SHA/vermagic 未变，
首次 `insmod` 成功且明确标注底层 syscall 未跟踪；第二次直接 `init_module` 返回 `-1/errno 17`，
class 和 `/dev/mydev` 存在，没有硬件 IO。artifact ID `11531185654`、digest
`sha256:da879d5d88ca7abae12e75879851f86ae9b7d123cf688901d6e1803a93fa22fd`，完整逐字段记录见
[run 37733967112 JSON](validation/legacy-kmod-eexist-run-37733967112.json)。这是同一 Linux runner 环境的
独立复测，不扩大到 EL vendor kernel、MOK 或旧 GUI 运行结果。

这是真实内核、真实项目模块和真实第二次 `init_module` 调用，不是 syscall mock；它验证了旧 GUI 所需的
内核 `EEXIST` 语义与新模块内部名兼容。该构建未签名（signer 为空），runner 不执行强制签名/MOK 验收；
运行内核也不是 EL8/9/10。原 GUI 没有被执行，旧 loader 选择哪个候选 `.ko`、目标机调用权限、签名
策略、旧 mailbox/token 初始化和主板识别仍待真机步骤确认。不要把本结果表述为 EL 启动或 Secure Boot 已通过。

原版 GUI 的程序目录联动只有在真机做：取当前内核已签名、未压缩的 DKMS `.ko`，确认 `modinfo`
内部名是 `octool_hwio`，再以只读 symlink/copy 暴露为旧 loader 经跟踪确认的文件名；记录源/目标 SHA、
签名者、vermagic。不要在签名后 strip、压缩或改写模块。用原版 ELF 的系统调用跟踪核对其
`init_module(...)= -1 EEXIST`、随后打开 `/dev/mydev`、mmap 邮箱并读 token。若出现其他 errno，先记录
内核日志和完整调用参数，不要将其强行解释成重复模块成功。

即使这段成功，仍需在被原程序识别的硬件上继续核验原版主窗口、96 字节请求、旧/新模块 MMIO live
对拍、错误应答、并发和 MOK。当前 GitHub runner 没有作者的四套主板/CPU；Not supported 对话框不
能作为真实硬件接入结果。
