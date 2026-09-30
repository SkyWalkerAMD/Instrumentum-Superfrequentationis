> 2026-09-30 更新：本文保留输入包的历史设计与验证记录。当前范围、修正、构建方式与实际状态见 [../docs/README.md](../docs/README.md)。历史 HAL 接入建议不代表本轮已改 GUI；本轮不承诺旧 GUI 全部直接硬件访问在 lockdown 下可用。对拍无稳定可读地址时返回 2，采集会影响时序，不能把易失项当成等价证明。

# octool 硬件访问层（多发行版重构）

支持 el8–el10、Ubuntu 20.04/22.04/24.04/26.04 的可落地代码。接进 octool 源码树即可。

```
abi/octool_hwio_abi.h     线级契约：请求结构(96B)+邮箱+操作码，模块与用户态共用一份
kmod/octool_hwio.c        可移植内核模块（MMIO 线级兼容现有 octool，另加 MSR/端口/PCI/EC）
kmod/Kbuild               class_create 参数个数探测（非版本号判断）
hal/octool_hwio.{c,h}     用户态访问层：一套 API，运行时选后端(模块/直接) + lockdown 判定
tests/hwio_loopback_test.c  离线协议自测（无内核，全通过）
tests/hwio_smoke.c          实机冒烟（CPUID 经模块与本地对拍）
tests/expcheck.sh           核对模块导入符号在目标内核是否导出
tests/octool_capture.c      LD_PRELOAD 观测库：采集现有 octool 会话读到的 MMIO 地址（纯只读）
tests/octool_parity.c       对拍比对器：把采集到的读重放到 新/旧 两模块背靠背三读 diff
tests/parity-run.sh         一键实机对拍：采集→并排加载新模块→比对→结论
packaging/                  DKMS + rpm spec + debian 骨架 + udev/modules-load
docs/refactor-guide.md      落地指南：如何把 HAL 接进 octool、验证了什么、真机待确认项
docs/parity-verification.md 对拍验证：不改 octool，用现有二进制验证新模块 MMIO 与旧 .ko 一致
```

## 快速开始

```sh
make check                     # 离线自测：协议 loopback + 对拍引擎 selftest（无需内核/硬件）
make -C kmod                   # 编模块（需 kernel-devel/linux-headers）
sudo sh packaging/dkms-install.sh 2.0 && sudo modprobe octool_hwio
cc -O2 -o smoke hal/octool_hwio.c tests/hwio_smoke.c && sudo ./smoke   # 实机冒烟
sudo OCTOOL=/path/to/octool sh tests/parity-run.sh   # 实机对拍：新模块 MMIO 是否与旧 .ko 一致
```

## 一句话

现有 octool 只通过 `/dev/mydev` 模块做 MMIO，MSR/端口/物理内存走的用户态路径在 Secure Boot(lockdown) 下被拦。本层把**所有**硬件访问都能走一条已签名模块（非 lockdown 机器仍走直接路径），MMIO 操作码保持与现有二进制逐字节兼容，因此新模块可直接顶替现有 .ko。已在 11 个内核（Ubuntu 5.4→7.0、Rocky 8.10/9.8/10.2）实编、glibc 2.31→2.43 编 HAL、离线协议自测全通过。详见 `docs/refactor-guide.md`。
