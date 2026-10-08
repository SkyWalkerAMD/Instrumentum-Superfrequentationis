# 原版 MMIO 错误处理：动态链接保护层实验

日期：2026-10-08。接续[邮箱契约逆向](legacy-mailbox-contract.md)。
本实验不改变原 ELF、八个调用函数、96 字节请求、模块或 HAL。
**此处的原生测试执行原包装函数的机器码，不运行完整旧 GUI，不访问硬件。**
云端实测结果将在执行后补入；编写了代码并不表示 EL8–EL10 验证成功。

## 为什么需要在 write 返回之前处理错误

八个旧包装函数不检查 write 返回值，只接受完整 64 位 done=1，没有错误出口。
驱动返回负 errno、高位编码或始终不完成，都不能让旧函数正常报错。
仅把错误完成字强改成 1 会把失败伪装成正常读取；这不是修复。

已核实八个调用都经动态链接的 write@GLIBC_2.2.5。由此新增可选实验
[mailbox-guard.c](../port/legacy/mailbox-guard.c)：在这个现有链接边界处理完成条件。
成功时仍只发原来的一次 write，原请求及 mailbox 一个字节也不写；失败时打印结构化原因，
直接以 74 结束进程，防止返回原函数后永久忙等。结束进程会丢失尚未保存的 GUI 状态，
它不是 Qt 错误弹窗、继续工作的错误恢复，也不撤销可能已经发生的硬件写入。

## 限定匹配与行为

- dl_iterate_phdr 查找一个同时含八个原始函数字节和可写全局对象区的 ELF 映像；
  不用 /proc/self/exe 推断程序身份，因为显式启动私有 ld.so 时该路径指向加载器。
  原函数字节、虚拟偏移、write 返回地址来自固定 SHA 的公开 664 字节样本。
- 只处理这八个 write 的准确返回地址；其他调用透传。PIE 加载基址由程序头提供，
  不写死运行时地址。完整 ELF 的 SHA 校验仍应由实际启动流程执行；局部字节匹配不是安全身份认证。
- 核查 fd、全局请求指针、96 字节长度、该函数的 opcode，以及已清零的邮箱首字。
  不重试短写/失败写，不补发请求，不清错误，不改结果槽。
- write 成功返回后，done=1 立即返回原调用者；合法负 errno 编码、异常完成字均退出74。
  done=0 最多等约1秒，使用 CLOCK_MONOTONIC 和短暂睡眠；调度可能使实际退出稍晚。
  errno 在正常返回时恢复为下游 write 返回后的值。
- 高位错误只用于解释当前模块的已知编码；没有发明新 opcode、协商字段或硬件寄存器定义。
  重构 HAL 仍保留原来的错误读取能力，ABI/HAL/kmod 均无改动。

## 可复现实验

[工具](../analysis/tools/test-legacy-guard.py)仅依赖 Python 标准库和 GCC/binutils，
先核验公开样本完整 SHA、每函数 SHA，再生成头文件和独立的微型测试 ELF。
链接器把八个函数放回原虚拟偏移，函数体逐字节核对；原来的 write 目的地放一个测试跳板，
转入真正的动态 write@GLIBC_2.2.5。调用者函数体不打补丁。

测试后端是新编译的 userspace `.so`，fd600 从未打开，mailbox 为匿名内存，
请求中的物理地址只是合成数字。后端记录实际收到的96字节、初始 done 和调用次数。
延迟完成由真正 pthread 发布；其他错误由后端注入。没有加载/模拟内核驱动、没有特权指令。

```sh
python3 analysis/tools/test-legacy-guard.py --build-dir build/native-guard \
  --target local-linux --output build/native-guard/results.json
gh workflow run legacy-guard.yml --ref main
```

每次执行核查307项：72项未保护的PIE基线（8函数×9条件）、16项未保护非PIE成功/异步基线，216项保护层测试
（PIE、非PIE、显式ld.so三种入口），3项无关调用者透传。
九个条件包括正常、真实异步完成、无完成、ENOMEM、EINVAL、write失败、短写、
write失败但done=1、非法完成字2。未保护分支的外部超时是已知缺陷的观察，不计为兼容成功。
成功请求的保留字段、令牌和写宽度截断都核验；失败分支要求74和对应诊断，不能仅凭“不挂”通过。

[独立 workflow](../.github/workflows/legacy-guard.yml)在 EL8 编译一次，并把相同文件送到
EL8、EL9、EL10、Ubuntu22.04（私有运行库的 glibc 来源）原生执行，核对产物哈希和 GLIBC<=2.28。
执行容器 UID10001、无网络、无capabilities、只读根文件系统，只有证据输出目录可写。
权限 contents:read，无草稿下载或 token 传入容器。工作流在 main 推送、PR 和手动触发时运行；
它把同一份 EL8 构建产物放到四种原生 glibc 上测试。portability 的十目标 kernel 阶段也以
nobody 编译/执行该门禁，继续保留 loopback、transport、parity selftest、模块/DKMS和GUI门禁。
新保护层不自动装入现有RPM/DEB或旧GUI启动器。

## 尚未解决的边界

1. 计时在真实 write 返回之后开始；无法中断阻塞在内核内的 write。
2. 旧模块先done后result的竞态没有消除，原GUI共享全局请求也没有变成线程安全；
   仅能发现部分重叠的write，不能保证捕获所有并发覆盖。
3. 原初始化、两次mmap、token短读、模块文件名/EEXIST、权限和硬件识别尚未经过这项测试。
   无效/已解除映射的邮箱指针仍可能使原函数或保护层发生信号故障。
4. LD_PRELOAD 与进程启动方式有关，setuid/secure-execution 等情形不能假定会加载。
   本轮不新增root启动入口、不提升权限、不关闭SELinux/Secure Boot。
5. 原ELF完整GUI仍停在云端Not supported对话框；保护层通过不等于主窗口通过，
   更不等于四套真机功能或签名驱动加载通过。

原理依据：[RTLD_NEXT/dlsym](https://man7.org/linux/man-pages/man3/dlsym.3.html)、
[glibc 显式加载器启动](https://sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker-Invocation.html)。
具体原版适用性来自函数指令和实际测试，不由通用文档推断。

## 首轮失败定位

独立运行37730137601的EL8编译/GLIBC上限核查完成，随后runner对已转交UID10001的
目录写镜像JSON失败，未上传构建产物。修正为先写镜像记录、再chown，不增加容器权限。

正式运行37730136773中，EL8的未保护PIE及保护PIE已完成，但非PIE首例在后端调用前SIGSEGV。
下载实际产物后发现EL8 binutils2.30把自定义低地址段与默认0x400000文本混排，生成
PT_LOAD.p_vaddr=0、PT_PHDR.p_vaddr=0x40。失败对象是新造的测试ELF，不能归因原GUI或保护层。
链接现在显式选非PIE text-segment=0x10000、max-page-size=0x1000，保持八个原函数偏移及字节不变；
新增程序头检查，拒绝非PIE低地址LOAD、W+X段和可执行栈，并补上非PIE未保护基线。
实际修复是否生效以后续重跑为准，不改宿主mmap_min_addr。
布局选项见 [GNU ld](https://sourceware.org/binutils/docs/ld/Options.html)，
低地址映射限制见 [Linux mmap_min_addr](https://www.kernel.org/doc/html/latest/admin-guide/sysctl/vm.html)。

## 云端实测 37731171839：EL8 构建产物跨四种 glibc 运行通过

[Actions run 37731171839](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37731171839)
基于提交 `4f7c249578f95f7fd9b6b9e7146c68a0775b741f`，五个 job 全部成功：EL8 构建 job，
以及 Rocky EL8、EL9、EL10、Ubuntu 22.04 原生执行 job。EL8 上的构建脚本核验保护库需要的
最高 GLIBC 符号版本不高于 2.28；四个系统运行的是同一组 EL8 构建字节，guard SHA-256
均为 `57ca919e302573ca954a64bb69936c64845bd4fc00f426f2455766117d201d2c`。

| 容器目标 | 实际 glibc 包 | 本机 307 项 | 证据 artifact digest |
|---|---|---:|---|
| Rocky EL8 | `2.28-251.el8_10.43` | 通过 | `7cc0c58c5a2a7cd0969b8dc0181476bc040b0d310081be2ce58514a30535656d` |
| Rocky EL9 | `2.34-275.el9_8` | 通过 | `025f6b3185d0c2e8987b3c9ff08b9a71a19fc75d00387cfd7f23e50c15b74161` |
| Rocky EL10 | `2.39-128.el10_2` | 通过 | `a56837ad20581bcf3dfff6603f967c07b8bc73d43d2ecf770467f13e4e6213f` |
| Ubuntu 22.04 | `2.35-0ubuntu3.15` | 通过 | `7a0e570b7dfc443eb51ef779b09c32cbd9aa40d548bfbad660db9dce0df681c7` |

每个目标的 307 项由未保护 PIE 72 项、未保护非 PIE 成功/异步 16 项、保护后的 PIE/非 PIE/
显式加载器路径 216 项、无关 write 调用透传 3 项组成。结果中所有失败注入均以指定诊断和退出码 74
结束；成功与延迟成功仍由原机器码正常返回，96 字节请求和结果字未被保护层改写。EL8 编译产物
在四个目标上的 SHA 完全相同。逐运行摘要、版本、哈希、artifact ID/digest 见
[机器可读记录](validation/legacy-mailbox-guard-run-37731171839.json)。

这是对原版八个 MMIO 包装函数错误/无应答路径的跨 glibc 保护层突破：无需改原 ELF 指令、96 字节 ABI
或邮箱结果，也无需将失败伪装成成功。它只在 write 已经返回后监视完成字，不能中断内核内阻塞的
write；错误会结束整个进程而不是恢复 GUI；并发请求、模块握手、硬件身份、Secure Boot/MOK、实机
MMIO 对拍及旧 GUI 主窗口仍未由该模拟覆盖。CI 也没有运行完整旧 GUI 或物理硬件。
