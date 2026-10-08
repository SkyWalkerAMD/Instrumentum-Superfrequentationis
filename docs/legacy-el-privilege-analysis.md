# 原版 EL8–EL10：启动权限、模块接入与失败路径

日期：2026-10-08。接续 [运行库追踪](legacy-el-compatibility.md)。
本轮继续分析同一原 ELF，不改 GUI、原调用点、96 字节请求、mmap 邮箱或模块协议。
截至本页初次提交，下面是静态证据；新一轮带系统调用观察的云端结果在执行后追加。
既有十目标重构版通过记录，不代表原版所有页面通过。

## 可复现的证据范围

```sh
python3 -m pip install --target build/reference-tools -r analysis/tools/requirements-reference.txt
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/audit-legacy-privileges.py \
  build/input-audit/octool --output docs/validation/legacy-privilege-analysis.json
```

工具先校验原 ELF SHA-256 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`，
只读解码，绝不运行输入。报告包括 20 个具备 ELF 长度声明的函数、15 段零长度汇编标签和
`MY_KMOD_LOADED` 的 13 处函数内 RIP 相对基址引用。

上一轮 42,115 个函数的扫描没有覆盖 NASM 的 `STT_NOTYPE / st_size=0` 标签。本轮显式选取
这些标签，并以同一可执行节内的下一符号或节末为边界；这只是有明确上限的字节区间，
不把它称为完整函数边界。报告单列汇编区间、哈希、解码长度及指令，不混入函数数量。
直接引用不等于动态执行；对象别名、间接调用、回调也不据此宣称已全部追踪。

新增 ELF fixture 会实际汇编/链接含 syscall、IN/OUT 的样本，但不运行它；验证同址别名、
局部边界、节末边界、截断指令、不允许把数据节当代码，以及 BSS 对象引用。

## 1. `iopl` 有两条路径，替换一个 libc 符号覆盖不了

| 原指令位置 | 已确认行为 | 对移植的影响 |
|---|---|---|
| `0x36fd93 → my_iopl` | NASM 在 `0xa7d6e5` 设置 EAX=172，在 `0xa7d6ed` 执行 syscall | 直接内核调用，不经过 libc `iopl` 或动态符号拦截 |
| `0x8e41d7` | MainWindow 另调 `iopl@plt(3)`，随后覆盖返回值 | 该调用点没有检查失败后停止启动 |
| `0xa7d536 / 0xa7d556` | 字节/字端口读取直接执行 IN | 安装私有 glibc 或新 `/dev/mydev` 不会自动重定向它们 |
| `0xa7d546 / 0xa7d566` | 字节/字端口写入直接执行 OUT | 不能把 `iopl` 强制返回成功当成适配方案 |
| `0xa7d76d / 0xa7d7ed` | 直接 syscall 105/106，MainWindow 传入 UID/GID=0 | 普通用户不会因此自动获得权限，原调用点没有核验结果 |

原 `enable_all_iopl` 保存当前 affinity mask 后只取置位数量，循环使用 CPU 编号 0..N-1，
最后调用 `set_all_affinity`，并未恢复所保存的掩码；设置 affinity 的返回值也没有检查。
这对 taskset、非连续 CPU 集和容器 cpuset 是独立问题，不能当作发行版版本号分支。

[Linux x86 iopl 实现](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/ioport.c)
明确将权限归属到线程，并检查 CAP_SYS_RAWIO / LOCKDOWN_IOPORT。
[x86-64 syscall 表](https://github.com/torvalds/linux/blob/v6.12/arch/x86/entry/syscalls/syscall_64.tbl)
用于核对这里的系统调用号。发行版 backport、实际配置和拒绝原因仍以目标内核为准。

## 2. 启动本身包含硬件写入，不能等同于只读监控

MainWindow 在 `isit_adl` 分支成立时于 `0x8e4ed0` 调 `en_ec_decoding`，发生在最终主窗口显示前。
该函数找到指定 PCI ID 后，在 `0x4b9e8d` 调 PCI byte 写接口；随后还构造 SIO 并调用写接口。
SIO 构造在 `0x4b36a0` 调 `enter_ef`，后者在 `0x4b3423` 等位置调用端口写接口；
`WriteIoPortByteEx` 又在 `0x3abf9e` 直接调用上表中的 OUT 包装。

这里只记录原程序的操作和可达分支，不解释未确认的寄存器含义，也不宣称这些写入适用于作者四台机器。
云端诊断继续不传宿主硬件设备、不授予 capabilities，不通过伪造 PCI/DMI 放开这些分支。
真机“仅启动 GUI”也应按可能有写入的步骤验收，不能据窗口操作少就称为只读测试。

## 3. `/dev/mydev` 存在与原 GUI 选择它，是两个条件

`MY_KMOD_LOADED` 位于 `0x1c7c5a0` 的 BSS，文件初值为 0。
在已观察到的初始化路径中：

1. `initilize_kernel_driver` 在 `0x36df54` 检查该标志，0 时先调用 `load_kmod`。
2. 它按程序目录查找 `peter_kernel.ko`、`peter_kernel_old.ko`、`peter_kernel_new.ko`。
3. `load_kmod` 在 `0x36d8f7` 调 `syscall(175, ...)` 即 init_module；成功或 errno=17
   （EEXIST）分别在 `0x36db75` / `0x36ddcd` 把标志置 1。
4. 标志成立才到 `0x36df75` 打开 `/dev/mydev`，再映射邮箱和读取 token。
5. `Read_MMIO` 的 `0x36efcd` 按该标志选择内核分支；否则进入 `/dev/mem` helper。
   helper 的一些失败路径会重试模块初始化，不能一概称为只走 `/dev/mem`。

所以旧报告“提前 modprobe 新模块即可直接接入原 GUI”的启动层面承诺过强。
**MMIO 线级兼容结论保持不变；旧 GUI 如何进入那条线级路径需要另验。**
只预载 DKMS 模块、但程序目录没有它要读取的模块文件，不能据设备节点存在就判定原 GUI 接入成功。

还发现 `load_kmod` 的 open 失败分支打印错误后回到 fstat/read/init_module 流程；
fstat/read 返回值在这一路没有检查。不能用一个空文件或旧内核 `.ko` 满足文件名来试运行。

保持原文件不变的候选方案是：为原 GUI 提供当前内核、正确签名、与已加载模块完全一致的
未压缩 `.ko`，在私有程序目录使用其旧查找文件名。文件名改变不改变模块内部名称。
这只是从旧加载路径得出的待验方案，当前没有加入包的自动安装或自动加载动作。
[Linux 模块加载实现](https://github.com/torvalds/linux/blob/v6.12/kernel/module/main.c)
会先检查模块加载权限、签名和模块信息，再有可能返回 EEXIST；不能假定“已加载”能免除这些检查。
EL8/9/10 各自的签名、重复加载返回码和实际 mailbox 连接仍需真机证据。

## 4. MSR 失败可能变成数值，而不是明确错误

`Rdmsr` 在 `0x36f81e / 0x36f82b / 0x36f83c` 调 open/lseek/read，之后直接从
`[rsp+8]` 取 8 字节并写到调用者输出。读缓冲区没有预先初始化，三项返回值均未检查。
`RdmsrTx` 相同；`Wrmsr/WrmsrTx` 也不核验 seek/write 是否完成。
因此在设备不存在、权限拒绝或短读时，不能把原页面显示的数值当作有效传感器数据；
这里没有推断任何温度、电压、电流或频率单位，也没有拿猜测值补齐。

需要纠正历史地基文档的一处概括：不能把“Secure Boot 开启”直接等同于“所有 MSR 读都被 lockdown 拒绝”。
[Linux MSR 驱动](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/msr.c)
在 open 检查 CAP_SYS_RAWIO/CPU 条件，LOCKDOWN_MSR 检查位于写入路径；读取仍可能因其他条件失败。
新 HAL 的保守后端策略是项目策略，不能当成所有内核的精确行为描述。

## 5. 私有运行库与现有对拍采集不能直接拼接

当前 `port/legacy/run.sh` 明确是普通用户桌面诊断入口：拒绝 UID0 和继承的 LD_PRELOAD。
`port/tests/parity-run.sh` 则要求 root，并通过 LD_PRELOAD 启动采集。
把 OCTOOL 指向该诊断入口会失败；直接指向 ELF 又绕过私有加载器。
这项约束来自代码检查，本轮没有运行硬件对拍。

此外，EL10 本机编译的采集 `.so` 可能依赖高于私有 glibc2.35 的符号，必须实际检查，
不能因为它“只是观察库”就混入进程。后续完整采集入口需要配套 ABI 合格的采集库、
仅目标进程的 preload、模块握手以及桌面授权。本轮保留现有 Ubuntu22.04 参考机采集流程，
复用 corpus 的 live replay 不依赖原 GUI 的私有运行库，但仍需两份模块与真实读地址。

## 云端观察方法与待填结果

独立诊断增加可选 `trace_syscalls` 输入，使用 strace `-D -f -i`，只观察 syscall 参数/返回值。
`-D` 保留原进程作为 smoke 的直接子进程，以便继续验证窗口 PID；依据
[strace 上游手册](https://github.com/strace/strace/blob/v5.18/doc/strace.1.in)。
不做返回值注入，不记录 read/write 缓冲区或环境变量，不增加 capabilities、网络或设备。
若容器不允许跟踪或没有采到 iopl，明确报错，不悄悄改成无跟踪后称为观察成功。

```sh
gh workflow run legacy-runtime.yml --ref main -f asset_id=600130240 -f trace_syscalls=true
```

新增日志保留 strace 版本、调用指令地址、CapEff/CapBnd、NoNewPrivs、Seccomp、TracerPid 和 CPU 掩码。
主窗口门禁继续要求 `Work Tool`，Not supported 对话框仍失败。原 ELF 仍在未发布草稿，
不进入 Git 或公开 artifact。本轮运行 ID、实际错误与结论将在下载证据后追加。
