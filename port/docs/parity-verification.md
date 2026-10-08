> 2026-09-30 更新：本文保留输入包的历史设计与验证记录。当前范围、修正、构建方式与实际状态见 [../../docs/README.md](../../docs/README.md)。历史 HAL 接入建议不代表本轮已改 GUI；本轮不承诺旧 GUI 全部直接硬件访问在 lockdown 下可用。对拍无稳定可读地址时返回 2，采集会影响时序，不能把易失项当成等价证明。

# MMIO 对拍验证（新模块 vs 旧 .ko）

> 2026-10-08 补充：本历史指南的“兼容”尚不覆盖错误应答。原版等待完整 done=1，当前模块的 errno
> 高位会导致它持续等待，采集器非零判定又可能记为 completed。旧模块之间也有先 done 后 result 的
> 顺序差异；详见 [原字节证据与 56 组模拟](../../docs/legacy-mailbox-contract.md)。

本文档说明 `tests/octool_capture.c` + `tests/octool_parity.c` + `tests/parity-run.sh`
这套“对拍”工具：**在不改动 octool 一行代码的前提下，用现有 octool 二进制作为负载，
验证新的 `octool_hwio` 模块在真实硬件上的 MMIO 读结果与原 `.ko` 逐一致。**

这是新模块能“顶替旧 .ko”这一说法的实机证据。离线部分（协议、引擎、格式）已全部自测
通过；唯一需要真机的是“write() → 模块 → 邮箱”这一步，本文给出完整步骤。

---

## 1. 要回答的问题

重构后的模块把 MMIO 八个操作码（0x0a–0x11）保持与旧 `.ko` 逐字节兼容，所以现有 octool
可以直接对着新模块跑。但“协议兼容”只保证请求/应答的**格式**一致，不保证新模块在同一
物理地址上**读回的值**和旧模块一致（ioremap 宽度、映射属性、字节序处理等都可能引入差异）。

对拍要回答的正是后者：

> 对 octool 实际会读的每一个 MMIO 地址，新模块返回的值是否和旧模块完全相同？

回答它不需要改 octool——地址全部来自对真实 octool 会话的观测，工具只是把这些地址重放到
两个模块上做比对。

---

## 2. 两段式设计

### 2.1 采集（capture，纯观测）

`octool_capture.c` 编成一个 `LD_PRELOAD` 动态库，挂在 `open/openat/mmap/write/close`
上。它**只观测、不改变** octool 的任何行为，也**不自己访问硬件**：

- `open`/`openat`：认出目标设备（默认 `/dev/mydev`，`OCTOOL_CAP_DEV` 可改）的 fd；
- `mmap`：记下该 fd 的邮箱页（offset 0）；
- `write`：对该 fd 的 96 字节写，先调真正的 `write()`，然后**像 octool 一样自旋等待
  done 标志**（原驱动可能用 kthread 异步填邮箱，write 返回时邮箱未必就绪），再把
  “请求 96 字节 + 邮箱 slot[0..4]”记为一条 trace 记录；
- `close`：忘掉该 fd。

因为记录发生在 octool 自己的 `write()` 调用内部、且只读邮箱不写，octool 的时序完全不受
影响。产物是一份 trace：octool 这次会话真正碰过的所有地址与当时旧模块的返回值。

on-disk 格式见 `tests/octool_parity_trace.h`（magic + 定长记录），采集端与比对端共用一份
定义。

### 2.2 重放比对（parity，只读）

`octool_parity.c` 读入 trace，取出其中的 MMIO **读**操作（写操作默认跳过，见 §4），
按地址去重成一个“地址集合”，然后对每个地址做比对。**默认只读，绝不自己写硬件。**

---

## 3. 易失寄存器：三读法

难点在于：很多 MMIO 地址本身就是会变的（温度、频率、各类计数器）。同一地址先后读两次
本来就可能不同，若直接 diff 会把大量正常的“硬件在变”误报成“两个模块不一致”。

对拍用**三读法**在 live 模式下区分这两种情况。对每个地址按顺序背靠背读三次：

```
old1 = 读旧模块
new  = 读新模块
old2 = 读旧模块
```

- `old1 == old2`（该地址此刻稳定）：则 `new == old1` 判 **match**，否则判 **MISMATCH**
  （真正的实现分歧——这是会导致 PARITY FAILED 的硬失败）；
- `old1 != old2`（该地址本就在变）：只要 `new` 落在 `[min(old1,old2), max(old1,old2)]`
  区间内就判 **volatile**（正常，不算失败），否则 **volatile-oob**（提示，需人看一眼）;
- 旧模块两次读的返回码不一致：判 volatile（拿不到干净的稳定读，不比对）;
- 新旧返回码一个成功一个失败：判 **ERR-PARITY**（硬失败）;
- 两个模块都读不了（都返回错误）：判 unreadable（不是分歧，跳过）。

背靠背三读把硬件漂移压到最小，因此“稳定地址上的不一致”几乎只可能来自实现差异——正是
我们要抓的东西。

---

## 4. 只读原则与写操作

对拍**默认不重放写操作**。原因：
- 写会改变硬件状态，对同一地址“分别写到两个模块”并非幂等，可能不安全；
- 写路径的**线级**一致性已由离线的 `hwio_loopback_test` 证明（每个写操作码/字段与 octool
  反汇编逐字节一致）。

因此 trace 里的写会被计入 `writes skipped` 但不执行。真正需要验证的、且只能在真机上验证的，
是新模块的**读回值**与旧模块一致——这正是对拍聚焦的范围。

---

## 5. 三种运行模式

| 模式 | 命令 | 用途 |
|---|---|---|
| **live** | `octool_parity --old /dev/mydev --new /dev/mydev_v2 --trace corpus.bin` | 两个模块同时加载，背靠背三读比对。**给结论**（PARITY OK / FAILED）。 |
| **offline** | `octool_parity --new /dev/mydev --trace corpus.bin` | 只有新模块在位，把新模块的实时读和 trace 里旧模块的历史值比。时间跨度大、易失集更大、octool 后来自己写过的地址会误报，故结果**仅供参考**。 |
| **selftest** | `octool_parity --selftest` | 不碰硬件。用进程内参考传输验证比对引擎本身的 match/mismatch/volatile 分类，以及 trace 格式的读写往返。 |

live 是拿结论的模式。它要求两个模块能同时在位——靠模块的 `devname` 参数把新模块挂成第二个
节点（旧模块占 `/dev/mydev`，新模块 `insmod octool_hwio.ko devname=mydev_v2` 挂成
`/dev/mydev_v2`），互不冲突。

---

## 6. 实机步骤

### 一键脚本

```sh
sudo OCTOOL=/path/to/octool sh tests/parity-run.sh
```

脚本流程：确认旧模块已加载并占着 `/dev/mydev` → 在 `LD_PRELOAD` 观测下让你正常操作 octool
一遍（把你关心的面板都点一遍，退出 octool）→ 把新模块挂成 `/dev/mydev_v2` → live 三读比对
→ 打印结论。相关环境变量：`OCTOOL_ARGS`、`CORPUS`（复用已有采集）、`NEW_KO`、`NEW_DEV`、
`OLD_DEV`。

### 手工步骤（等价）

```sh
# 0. 编译
make -C tests octool_capture.so octool_parity
make -C kmod                       # 得到新模块 kmod/octool_hwio.ko

# 1. 旧模块在位（占 /dev/mydev）——按你现有方式加载原 .ko

# 2. 采集一次真实会话（只读观测）
OCTOOL_CAP_DEV=/dev/mydev OCTOOL_CAP_OUT=corpus.bin \
  LD_PRELOAD=$PWD/tests/octool_capture.so /path/to/octool
#   正常操作 octool，退出

# 3. 新模块并排加载成第二个节点
sudo insmod kmod/octool_hwio.ko devname=mydev_v2

# 4. live 三读比对
sudo tests/octool_parity --old /dev/mydev --new /dev/mydev_v2 --trace corpus.bin
```

### 读结果

结尾会打印各类计数，并给出：

- `PARITY OK: new module matches old on every stable MMIO read` —— 稳定地址零分歧，
  退出码 0；
- `PARITY FAILED: N stable divergence(s)` —— 有 N 处稳定地址不一致或返回码不一致，
  退出码 1，前若干条会逐行列出 `phys/width/old/new`，据此定位。

`volatile` 计数高是正常的（传感器/计数器多）。真正要盯的是 `MISMATCH(stable)` 和
`err-parity` 必须为 0。

---

## 7. 离线已验证 / 真机待验证

**离线已通过（`make -C tests check`，无需内核或硬件）**
- `loopback`：HAL 编解码与 octool ABI 每个操作逐字节一致；
- `octool_parity --selftest`：
  - `selftest-trace`：采集/比对共用的 trace 格式写入→读出往返正确（去重、跳过写、跳过
    非 MMIO 计数均正确）；
  - 引擎分类：对 match / MISMATCH / volatile 三种情况判定正确；
- 采集库端到端（scratchpad 内用假设备驱动）：`open/mmap/write/close` 拦截、done 自旋等待、
  逐记录落盘均正确（四条记录操作码/地址/结果全部吻合）；
- 采集库以 `-Wall -Wextra` 零告警编成 `.so`。

**只能在真机验证（本质需要内核模块 + 硬件）**
- `write() → 模块 → 邮箱` 在真实字符设备上的闭环；
- 旧 `.ko` 与新模块（`devname=mydev_v2`）同时在位；
- 对真实 octool 会话的 live 三读比对结论。

---

## 8. 局限

- **只覆盖 octool 实际读到的地址**。没被这次会话触碰的寄存器不在集合里——多点几个面板能
  扩大覆盖；也可多次采集后合并 corpus。
- **写效果不做双执行验证**（见 §4），写的线级一致性由 loopback 覆盖。
- **offline 模式仅供参考**，拿结论请用 live。
- 三读法压小但不消除漂移窗口；极高频变化的寄存器可能落入 volatile 而非 match，这是保守
  的正确方向（不会把真差异漏成 match，只会把个别真相同的算成 volatile）。
