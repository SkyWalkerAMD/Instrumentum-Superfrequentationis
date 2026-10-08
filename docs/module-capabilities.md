# 模块能力识别与旧模块对拍入口

更新：2026-10-08，接续 [R9复查](review-2026-10-08.md)。
此前HAL把open/mmap/read成功等同于支持所有扩展命令，可能让重构GUI将MSR/PCI/EC命令交给旧驱动。
本轮增加独立只读查询，原GUI无需调用；旧96字节请求、token和MMIO邮箱布局保留。

## 新查询的固定契约

定义在 `port/abi/octool_hwio_caps.h`，不是改写 canonical `octool_hwio_abi.h`。
`OCTOOL_HWIO_GET_CAPS_V1 = _IOR('O', 0x80, struct octool_hwio_caps)`，x86-64编码为`0x80204f80`。
遵循 [Linux ioctl接口指南](https://docs.kernel.org/driver-api/ioctl.html)：固定宽度成员、无指针/long、
完整初始化、成功返回0、未知命令返回ENOTTY；不兼容扩展须另分配命令号，不能重解释V1。

| 偏移 | 类型/大小 | 含义 |
|---|---|---|
| 0 | u32 | magic=`0x4f435432` |
| 4 | u16 | 固定格式标记1，不用于切换同一命令的布局 |
| 6 | u16 | 固定总长32 |
| 8 | u64 | bit0 MSR、1 MMIO、2 port IO、3 PCI、4 EC、5 CPU |
| 16 | 两个u64 | V1保留，必须全0 |

这里的bit表示模块实现命令族，**不表示实际寄存器可读写、设备存在、平台已识别或写入安全**。
例如EC bit只在CONFIG_ACPI开启时存在，没有EC仍可能返回ENODEV。没有新增硬件探测或寄存器读写。
当前发行目标仅x86-64用户态，未声称已支持32位compat ioctl。

## HAL 与对拍各自的行为

`hwio_open()`先open，然后查询并校验magic/格式/大小/保留位/至少一个已知feature，成功后才mmap/read token。
查询失败或内容非法会关闭fd，不mmap、不发96字节命令；按原HAL策略改用允许的直接后端或NONE。
未知feature位可忽略，不能单靠未知位构成有效应答。完整有效元数据仍不是设备身份的密码学认证。

模块只报告部分能力时，未报告的特权命令族为NONE，调用返回EPERM，不对该族另发扩展命令或暗中直访。
CPU信息仍可走本地无特权指令，调用方负责目标CPU affinity。没有模块时EC也正确报告NONE，原来标为
direct但实际总返回EPERM的诊断不一致已修正。

旧模块没有此ioctl，因此新增显式 `hwio_open_legacy_mmio()`，**仅用于已确认旧节点的MMIO对拍**。
它不查询ioctl，以原mmap+8字节token初始化，仅开放MMIO模块后端；MSR/IO/PCI/EC均不发送到该节点。
`octool_parity --old`选择此入口，`--new`仍须通过新能力查询。重构GUI不调用legacy入口，不自动降级
为旧驱动协议。对拍依然拒绝相同设备号/别名，节点归属和运行期间稳定性由操作者确认。

`hwio_open_transport()`属于显式注入接口，继续信任调用者提供的测试/自定义transport；它不是设备自动识别。
旧完整ELF的调用点没有变化，它可以继续向新模块使用已有read/write/mmap；已知错误done字导致旧GUI
等待不退出的问题并未因此解决。见 [邮箱契约](legacy-mailbox-contract.md)。

## 回归与升级检查

- `make -C port/tests check`中的真实HAL系统调用替身覆盖ENOTTY/EACCES、正数返回、magic/格式/大小
  错误、两个保留字、空/全未知能力；要求拒绝发生在mmap之前且fd关闭。
- 部分能力/未知附加位、legacy opt-in分别验证未授权扩展不产生IO；MMIO仍发送原opcode/token/96字节。
  原loopback、parity selftest、56项原机器码模拟和每目标307项guard测试继续门禁。
- DKMS/rpm/deb及独立安装均携带新公共头；HAL Makefile加入依赖。
- 受限runner探针新增 `kmod-caps-probe.c`：真实open/ioctl/close项目模块，核对结构、未知命令ENOTTY、
  NULL输出指针EFAULT。它不read/write/mmap、不访问硬件；随后显式卸载模块必须成功。
  这是之前“探针不打开设备”的有意范围扩展，只打开查询元数据，旧运行记录保持其原始范围。

升级时须让GUI与新模块配套：同为未发布2.0.1的旧构建也没有查询，不能仅看版本字符串判断支持。
确认实际加载的是本次`.ko`（SHA/vermagic/signature），有旧句柄时先退出GUI；按正常DKMS升级与目标
签名流程加载新构建或重启。不要为让旧模块通过而在GUI开启legacy opt-in。
真机可编译只读探针：

```sh
cc -D_GNU_SOURCE -std=c11 -O2 -Wall -Wextra -Werror \
  analysis/tools/kmod-caps-probe.c -o build/kmod-caps-probe
sudo build/kmod-caps-probe
```

应看到caps_valid、unknown_command_enotty、bad_pointer_efault、close_succeeded全部true。
该查询成功仍不能签署硬件、MOK或旧GUI主窗口验收。

本轮含能力查询的完整云端回归与真实探针尚待执行；实际提交SHA、run与结果完成后另记。
