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

## 三份旧模块的静态核对

对作者原ZIP中的三份固定SHA `.ko` 继续核对：从各自DWARF读取 `file_operations` 成员偏移，
检查名为`fops`的实际对象字节及指向该槽的重定位。不能只看未链接的零值，因为已注册的read/write
回调在ET_REL中同样存零，由重定位填入真正地址。新审计器同时确认这些回调的R_X86_64_64重定位存在。

| 固定原样本 | fops字节数 | unlocked_ioctl偏移 | compat_ioctl偏移 |
|---|---:|---:|---:|
| peter_kernel.ko（6.8样本） | 264 | 72 | 80 |
| peter_kernel_new.ko（6.17样本） | 272 | 80 | 88 |
| peter_kernel_old.ko（6.2样本） | 272 | 80 | 88 |

三个对象的两个ioctl槽均为完整8字节零，且无覆盖该槽的重定位。不同样本布局并不相同，本轮由
其DWARF取偏移，不根据内核版本推断。证据见[旧fops字段记录](validation/legacy-fops-capabilities.json)。
这是“原对象没有ioctl回调”的静态证据，不是装载旧模块后实测ENOTTY，也不覆盖任意第三方驱动。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/audit-legacy-fops.py \
  ../octool-linux.zip --output build/legacy-fops.json
```

脚本核验输入SHA、ELF类型、DWARF重名布局一致性、对象边界和重定位；缺项或不同样本直接报错。
本地已对三份原输入执行成功，未装载旧模块或发布其完整字节。

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

## 本轮实际结果

提交`f2142e4c88fa196ee5b5bb9eb630d8fe4342b176`的
[portability37741335600](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37741335600)
23/23全绿：十目标均编译模块与GUI、产出rpm/deb并安装、通过DKMS生命周期和窗口门禁。
下载后的每目标offline.log均包含能力拒绝、部分能力、legacy隔离、PCI/EC两分支与线程权限通过；
十份QtTest均12通过0失败0跳过，十份guard报告各307项观测符合预期。kernel阶段19项Python无跳过；
matrix阶段的2项Linux工具跳过由后续kernel执行补齐。EL10发行/native窗口PID分别19386/19453，
实际由Mutter/Xwayland承载，并已人工查看发行版AMD PStates页截图：表格未填入未经读取的值。

同提交[probe37741335727](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37741335727)
在`6.17.0-1022-azure`实载新模块，SHA为`c798b85824914e9c67ae75f006ced4e460139224bf6b2e49972ccf45c9dc011f`。
真实ioctl返回magic正确、size32、features0x3f；未知命令ENOTTY、坏指针EFAULT、close和显式卸载均成功，
重复init_module仍精确errno17。无硬件请求，模块未签名，本记录不证明目标EL内核/MOK/原GUI主窗口。

Windows再次从完整固定SHA旧ELF运行56项有界模拟，八个函数哈希和全部观测与上轮完全相同；
新增能力查询没有改变原机器码或已知失败分支。旧fops静态脚本另在三份原输入实跑、重复结果字节一致。

完整job/artifact、关键日志与探针JSON在[回归记录](validation/module-capabilities-f2142e4.json)。
十目标20个安装包已下载到`dist/packages-f2142e4/<目标>/`，附`SHA256SUMS`；11套模块、镜像digest、
包与截图哈希见[交付记录](validation/deliverables-f2142e4.json)。cloud源码SHA为
`8395496120c37862893a01b848deef6297470e103b3056de778fb487e2205d1f`，保存在
`dist/cloud-f2142e4/octool-2.0.1-src.tar.gz`。后续归档只增加文档/只读逆向脚本，没有改生产模块、HAL或GUI。
