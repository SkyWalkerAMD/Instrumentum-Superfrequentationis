# CHANGELOG — octool 硬件访问层重构

## Unreleased — 2026-09-30（无对 octool 界面层的改动；新增可移植访问层）

### 新增
- `abi/octool_hwio_abi.h`：模块与用户态共用的线级契约。请求 96 字节、`cmd@0/user_id@8/data0@16/data1@24`、邮箱 `slot[0]=done / slot[1]=result`——与现有 octool 二进制反汇编逐字节一致（`_Static_assert` 钉死）。
- `kmod/octool_hwio.c` + `Kbuild`：可移植内核模块。
  - MMIO 八个操作码（0x0a–0x11）线级兼容现有 octool，可直接顶替 `peter_kernel*.ko`；
  - 增加 MSR(0x20/0x21)、TSC(0x22)、CPUID(0x23)、端口(0x30–0x35)、PCI(0x40/0x41)、EC(0x50/0x51)、核数(0x60)，供 lockdown 下走模块；
  - **去掉每请求的 `kthread_create_on_cpu`**（5.17 前不导出），CPU 相关改用 `rdmsr_safe_on_cpu`/`smp_call_function_single`——消除除 `class_create` 外的版本敏感点；
  - `class_create` 参数个数由 Kbuild 探测头文件（RHEL 9 在 9.2/9.4 之间改签名，版本号都是 5.14）；
  - `open()` 要 `CAP_SYS_RAWIO`（默认还要 `CAP_SYS_ADMIN`）。
- `hal/octool_hwio.{c,h}`：用户态访问层。一套 API（MSR/MMIO/IO/PCI/EC/CPUID/TSC/cores），运行时按 模块 / 直接路径 选后端，读 `/sys/kernel/security/lockdown` 判定；lockdown 且无模块时相关族返回 -EPERM，CPUID/TSC/cores 仍本地可答。
- `tests/hwio_loopback_test.c`：离线协议自测（参考传输镜像模块语义），断言每个操作的操作码/字段与 octool 反汇编一致且值往返正确。
- `tests/hwio_smoke.c`：实机冒烟（模块 CPUID 与本地 cpuid 对拍、核数、MSR 读、各族后端）。
- `packaging/`：DKMS conf、rpm spec、debian 骨架、udev 规则、modules-load(msr)、`dkms-install.sh`。
- `docs/refactor-guide.md`：落地指南（如何把 HAL 接进 octool、验证矩阵、真机待确认项）。

### 验证
- 模块在 11 个内核实编+链接通过：Ubuntu 5.4.0-216 / 5.15.0-139 / 5.15.0-194 / 6.8.0-138 / 6.8.0-142 / 6.17.0-42 / 7.0.0-34（×2 发行版），Rocky 8.10(4.18) / 9.8(5.14) / 10.2(6.12)。`class_create` 探测：≤EL8/老 Ubuntu 用双参数 `__class_create`，6.4+/EL9.4+ 用单参数。导入符号在三棵 EL 树全部导出（0 缺失）。唯一真实告警：EL8/GCC9 的 `-Wmissing-attributes`，无害。
- HAL 在 glibc 2.31/2.35/2.39/2.43（gcc 9/11/13/15）`-Wall -Wextra` 零告警编译并通过 loopback。
- 结构 ABI 与 octool 观测布局一致（size 96、cmd@0、user_id@8、data0@16、data1@24）。

### 真机待确认
- insmod + 读写硬件；HAL↔真实模块端到端（`hwio_smoke` CPUID 对拍）；EL 用 RHEL 自带 GCC 编模块；EL 用户态编 HAL；Secure Boot 下 MOK 签名与 lockdown 回退。

## Unreleased — 2026-09-30（新增 MMIO 对拍验证工具；仍无对 octool 界面层改动）

### 新增
- `tests/octool_parity_trace.h`：采集端与比对端共用的 trace on-disk 格式（magic
  `0x4f43545250520001` + 定长记录：请求 96B + 邮箱 slot[0..4] + seq + wrote + completed）。
- `tests/octool_capture.c`：`LD_PRELOAD` 观测库。挂 `open/openat(+64)/mmap(+64)/write/close`，
  认出目标设备 fd、记下 mmap 邮箱页，对 96B 写**像 octool 一样自旋等 done** 后把请求+邮箱
  落一条记录。纯观测：不改 octool 行为、不自访硬件、只读邮箱。env：`OCTOOL_CAP_DEV`/
  `OCTOOL_CAP_OUT`。带 fd→邮箱表（`MAXFD` 上限）、`__thread` 重入保护、输出互斥锁。
- `tests/octool_parity.c`：对拍比对器。读 trace → 取 MMIO 读、按 (phys,width) 去重成地址集合
  → 比对。
  - **live 模式**（`--old`/`--new`）：背靠背三读 `old,new,old`，用 `old1==old2` 判稳定，
    稳定地址 `new!=old` 即 `MISMATCH`（硬失败）；`old1!=old2` 判 volatile，`new` 落在
    区间内为 `volatile`，否则 `volatile-oob`；返回码不一致 `ERR-PARITY`；都失败 `unreadable`。
    有任何稳定分歧退出码 1。
  - **offline 模式**（仅 `--new`）：新模块实时读 vs trace 里旧值，仅供参考。
  - **`--selftest`**：进程内参考传输验证分类引擎 + trace 格式往返，不碰硬件。
  - 写操作默认跳过（不双执行；写的线级一致性由 loopback 覆盖）。
- `tests/parity-run.sh`：一键实机流程——确认旧模块在位 → `LD_PRELOAD` 采集一次真实会话 →
  新模块 `insmod ... devname=mydev_v2` 挂成 `/dev/mydev_v2` → live 三读比对 → 结论。
- `tests/Makefile`：新增 `octool_parity`、`octool_capture.so` 目标；`check` 现在还跑
  `octool_parity --selftest` 并编译采集库。
- `docs/parity-verification.md`：对拍设计与实机步骤（两段式、三读法、三种模式、只读原则、
  结果解读、离线已验证/真机待验证、局限）。

### 验证（离线，`make -C tests check`）
- `loopback` 通过：HAL 编解码与 octool ABI 每操作逐字节一致。
- `octool_parity --selftest` 通过：
  - `selftest-trace`：trace 写入→读出往返正确（去重=1、跳过写=1、跳过非 MMIO=1、唯一地址=2）；
  - 引擎分类：故意让 new 在一个稳定地址异或 0xff → 正确判 `MISMATCH`；一个自增地址 → 正确判
    `volatile`；一个相同地址 → `match`。计数 match=1/mismatch=1/volatile=1 符合预期。
- 采集库端到端（scratchpad 假设备）：拦截 `open/mmap/write/close`、done 自旋、逐记录落盘
  正确；四条记录（RD_MEM32×2、RD_MEM8、WR_MEM32）操作码/地址/结果全部吻合。
  - 过程中发现并定位一处**测试脚手架**假象：假设备用同一普通文件既做 mmap 邮箱又做 write 目标，
    offset 0 的写会覆盖邮箱页 → 首条记录被请求字节覆盖成 0。真字符设备不会（write 由模块解释、
    不落到 mmap 页）。修正假设备令写落到邮箱页之后，四条记录即全部正确。**非采集库缺陷。**
- 全部 `-Wall -Wextra` 零告警；采集库以 `-fPIC -shared` 编成 `.so`。

### 真机待确认
- `write()→模块→邮箱` 在真实字符设备上的闭环；旧 `.ko` 与新模块（`devname=mydev_v2`）同时在位；
  对真实 octool 会话的 live 三读比对结论。
