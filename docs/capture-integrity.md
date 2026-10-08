# 对拍采集完整性与映射生命周期

日期：2026-10-08。接续全面复查 R9 的 capture 边界。本轮只修改采集/对拍工具及门禁；
旧 GUI、96 字节设备协议、HAL、模块、寄存器定义均不改动。

## 发现与处理

原采集器只保存每个 fd 的最后一个 mmap 地址，没有处理 munmap、mremap、保护权限变化或
MAP_FIXED 覆盖。解除映射后再写设备，采集器可能读到失效地址；fd 复用和请求重叠也可能混淆归属。
原日志头从开始就是有效 v1，记录输出忽略短写/失败；因此一份被截断到完整记录边界的半份日志仍
可能被当作有效输入。请求在实际 write 返回后才复制，还可能取到已经被后续代码修改的字节。

当前实现采用保守拒绝，不试图替旧 GUI 修复线程和驱动行为：

- 请求在实际 write 前保存，设备 write 仍只执行一次，失败/短写不补发，原返回值和 errno 保留。
  请求来自普通用户内存，用 self `process_vm_readv` 有界复制，坏指针不被采集器变成 SIGSEGV。
  若 seccomp 等限制拒绝该复制，整份采集无效，不伪造成功。
- 邮箱仍直接读取已映射页，不能把普通内存复制方案套到驱动 PFNMAP 页上。同步元数据和读取
  快照所用的锁也保护被拦截的 munmap/mprotect/mremap/MAP_FIXED 操作。锁不跨越设备 write，
  不通过串行化硬件请求来掩盖原程序并发。
- 只接受 offset 0、至少 40 字节、可读共享映射。已确认旧初始化的两次同页映射可以继续采集。
  映射失效后不猜新地址，也不回退到更早的别名；下一次请求若没有新有效映射就拒绝。
  失败的 VMA 修改也保守忘掉可能受影响的映射，代价是需要重新采集。
- open/open64/openat/openat64 按设备身份识别路径别名，close 清理 fd，重新打开分配新的代际编号。
  dup/dup2/dup3 复制目标设备属于不支持的路径，会使整份采集无效；dup2 同 fd 不算复制。
  不实现 fcntl 的全部变参命令：若经 fcntl 创建的未跟踪别名发起 mmap/write，会按身份识别并拒绝。
  替换为普通文件的旧 fd 不再被当成设备；fd 超过表范围也不静默漏记。
- 重叠设备请求、请求期间的映射/fd 变化、不完整应答、错误完成字或约 1 秒无完成，均使日志无效。
  超时只在设备 write 返回后计时，不能中断内核阻塞。错误不会把 done 改成 1，也不退出原 GUI。
  要阻止旧 GUI 随后的永久等待，仍须单独验证[错误保护层](legacy-mailbox-guard.md)。

## v2 文件提交与向后读取

设备 ABI 不变。仅对拍文件新增 magic `0x4f43545250520002`，头仍 32 字节，记录仍 152 字节。
开始时 magic=0；所有记录完整写入，正常退出且没有不确定观察后，才写确切 nrec、同步已有数据，
最后写有效 magic。短日志写可以重试剩余文件字节；设备请求绝不重试。

输出在截断前取得独占非阻塞 flock，防止继承 LD_PRELOAD 的 exec 子进程截断父进程日志。
输出非普通文件、锁冲突、ENOSPC/EIO、同步或提交失败，均不产生本次有效提交。信号、`_exit`、正在进行的请求
或 fork 也不提交；fork 子进程关闭自己的日志 fd，不能替父进程提交共享文件。最终标记未承诺断电
持久化；丢失标记时拒绝文件。输出文件应由操作者独占，不支持其他进程修改或替换其内容。

共享库析构顺序不能等同于“进程已经结束”。采集析构函数发布标记后仍保留日志 fd/锁，直到内核
结束进程；后续 DSO 析构器若再发设备请求，会在转发前撤销标记。成功和失败的晚到请求都拒绝，
不让析构后的错误留下一份看似完整的 trace。若外部环境使已发布文件连撤销写/截断都不能完成，
仍必须按 stderr 的 INCOMPLETE 拒绝该文件；工具不是抗任意存储故障的事务数据库。

新对拍工具核对完整记录和精确 nrec，**在地址去重前**拒绝不完整读取，防止成功记录掩盖同地址的
后续失败。旧 v1 文件仍可读取并明确警告没有完整收尾保证；有非零 nrec 时也校验计数。
不能把旧流式文件升级为“已完整采集”的证据。

```sh
make -C port/tests check
port/tests/octool_parity --check-trace --trace /实际路径/corpus.bin
```

`--check-trace` 不打开设备，仅检查文件与已采集读记录；退出 0 不代表硬件对拍成功。
`parity-run.sh` 每次新采集使用 mktemp 独立文件，通过预检后才替换正式 corpus；退出时清理临时文件。
这避免输出初始化失败或 preload 未加载时误用以前的有效文件。手工启动也应给每次运行新的输出路径，
并保存 stderr 与 GUI 退出状态。脚本在 insmod 前预检，未完成采集不会进入模块加载/重放阶段。

## 无硬件回归

`capture-harness` 在普通临时文件上建立共享邮箱，`capture_backend.so` 作为下游 write 替身。
执行真实生产采集库和对拍读取器，不启动原 GUI、不访问 `/dev/mydev`、不装模块。
每例先成功记录一次，然后制造第二次访问的条件，以检验“已有好记录也不能掩盖后续失败”。

当前 46 例：15 例正常提交、31 例明确拒绝。覆盖双重映射、三种 open 别名、只读保护、fd 复用、
dup2 替换/无操作/失败、dup 三入口、fcntl 别名、解除/移动/覆盖映射、并发解除映射和关闭 fd、
重叠请求、异步完成、请求缓冲返回后被改写、无效指针/长度、失败/短设备写、驱动错误/非法完成/超时，
以及日志短写/EINTR/部分写后失败、提交/同步失败、信号/_exit/fork/exec 子进程、只解除非邮箱尾页。
另两例先实读已提交 magic，确认析构顺序，再在后续 DSO 析构器制造成功/失败的晚到设备请求，
要求撤销提交，同时仍只调用原 write 一次。
核对所有请求字段、邮箱五字、
序号、原 errno 与下游调用次数；非法场景既不能信号崩溃，也不能被文件检查器接受。

已接入 `make -C port/tests check`，十目标 kernel job 导出 `capture-results.json`。
另外 Python 对拍输入回归覆盖 v2 计数、零 magic、尾部记录缺失和失败重复地址；真实 shell 脚本回归
以 /bin/true 制造未采集场景，必须拒绝、保留旧 corpus 且不触及替身模块命令。
本节描述代码与门禁，实际云端结果见下文；不沿用上一轮全绿。

## 仍然不支持或不能证明的范围

这不是对任意进程的系统调用审计或内存安全边界。直接 syscall/隐藏 libc 调用绕过拦截、close_range、
pkey_mprotect、writev/io_uring、exec、异步信号处理器内调用、线程取消、其他进程或线程直接改写
请求/邮箱，均未建立完整跟踪保证。测试中的直接 mmap 只为创建独立合成回应页，不冒充该路径受到保护。
工作负载需遵守已核实原 GUI 的 open/mmap/write 调用方式，并在退出前停止工作线程；超出范围重新设计。

旧模块先发布 done 后更新结果的竞态仍存在；快照的 acquire 读取不能弥补生产者顺序错误。
与其他 LD_PRELOAD 库混用时调用地址、顺序和退出路径会变化；本轮没有把 capture 与 guard 的组合
作为已支持配置。硬件对拍、旧完整 GUI 启动、EL vendor 运行内核和 Secure Boot/MOK 仍待真机验收。

接口语义依据：[mmap/munmap](https://man7.org/linux/man-pages/man2/mmap.2.html)、
[mremap](https://man7.org/linux/man-pages/man2/mremap.2.html)、
[dup](https://man7.org/linux/man-pages/man2/dup.2.html)、
[close](https://man7.org/linux/man-pages/man2/close.2.html)、
[正常退出的清理范围](https://man7.org/linux/man-pages/man3/exit.3.html)。
请求复制使用的 [process_vm_readv](https://man7.org/linux/man-pages/man2/process_vm_readv.2.html)
会经 [pin_user_pages_remote](https://raw.githubusercontent.com/torvalds/linux/v6.12/mm/process_vm_access.c)；
[GUP](https://raw.githubusercontent.com/torvalds/linux/v6.12/mm/gup.c) 拒绝 VM_IO/VM_PFNMAP，故未用于邮箱。
具体项目行为以源码与下述实际测试为准。

## 本轮云端证据

`3920018c7e045786684cf28b332bde3956e2376f` 的
[portability37749651590](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37749651590)
23/23 job已全部通过。十个 kernel job各46项采集、21项Python（零跳过）、307项原指令guard观测、
loopback/transport/PCI/EC/线程权限/parity selftest及DKMS安装生命周期。合计460项采集结果，
下载后十份 `capture-results.json` 的SHA逐字节一致；Ubuntu22.04的GA/HWE使内核实编共11套。

| 目标 | 实际 glibc | 本地编译器主版本 | 采集提交/拒绝 |
|---|---|---|---|
| Rocky EL8 | 2.28 | GCC8.5 | 15/31，符合预期 |
| Rocky EL9 | 2.34 | GCC11.5 | 15/31，符合预期 |
| Rocky EL10 | 2.39 | GCC14.3 | 15/31，符合预期 |
| Ubuntu20.04 | 2.31 | GCC9.4 | 15/31，符合预期 |
| Ubuntu22.04 | 2.35 | GCC11.4 | 15/31，符合预期 |
| Ubuntu24.04 | 2.39 | GCC13.3 | 15/31，符合预期 |
| Ubuntu26.04 | 2.43 | GCC15.2 | 15/31，符合预期 |
| Debian11 | 2.31 | GCC10.2 | 15/31，符合预期 |
| Debian12 | 2.36 | GCC12.2 | 15/31，符合预期 |
| Debian13 | 2.41 | GCC14.2 | 15/31，符合预期 |

这是每个目标原生编译后的执行，不是同一capture.so跨全部glibc运行的验证；容器共享runner内核。
本轮十目标GUI构建、装包、DKMS生命周期、发行/native窗口门禁均成功；各QtTest12项、零失败/跳过。
EL10由Mutter/Xwayland承载，窗口PID分别19385/19452，发行AMD PStates页截图已人工检查：未读值为空，
没有伪造传感器数据。原GUI和真实MMIO不在此次执行范围。
同提交的[元数据与重复加载探针37749651635](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37749651635)
成功，模块SHA与此前f2142e4相同，仍无硬件IO/固件MOK验证。

完整job/step/artifact、460项采集的共同原始观测与逐目标哈希、Python及探针结果归档于
[回归证据](validation/capture-integrity-3920018.json)；实际20个安装包、11套模块、100张截图和源码包
哈希见[交付记录](validation/deliverables-3920018.json)。十份采集报告的共同SHA为
`7b5dc95730f1fa2cba7ac149d814475c69edc1379b63e883564e699372f6bdd2`。
安装包保存于`dist/packages-3920018/`并附SHA256SUMS，cloud源包在`dist/cloud-3920018/`，
源码SHA为`1e149d3d85602456a96c2c32aa93be7a55c66166d1a33c6634aaf4938dd184ea`。
收尾归档仅增改文档和证据；本地重新生成的源码包含最新归档，不冒充cloud字节相同。

首个42例提交及44例追加提交的matrix已实际成功，随后被后继提交取消完整流水线；不计成23项全绿。
`47a843d / 37749289855`在严格编译发现未检查ftruncate回退结果，46例未执行；修正后才形成上述结果。
Windows静态ABI/文档链接/可重复源包检查通过；Python3.14解包的沙箱路径权限限制未绕过，
Linux上述21项实际执行弥补测试覆盖，不将本地跳过或失败记作通过。
