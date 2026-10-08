# PCI/EC 系统协调与 HAL 线程权限

更新：2026-10-08。接续 [全面复查 R7/R9](review-2026-10-08.md)，基线 `bf43013`。
本轮修改生产模块/HAL 和离线测试，不修改旧 GUI ELF、96 字节请求、MMIO 八个操作码或邮箱偏移。
下列源码修正仍须对应提交的完整 Actions 回归，实际结果在文末单独记录。

## PCI：使用内核公开接口

原模块自行执行 CF8/CFC，以自有 raw spinlock 串行化。这只能保护本模块，无法保护系统 PCI
驱动正在使用的配置地址寄存器。根据 [Linux4.18 PCI 核心](https://raw.githubusercontent.com/torvalds/linux/v4.18/drivers/pci/access.c)，
配置访问应通过 PCI 核心及平台 ops，不能新建一个同名锁解决。

`octool_bus_access.h` 现在执行：

1. 在 u64 原始字段收窄之前验证 bus≤255、device≤31、function≤7、offset≤255、宽度1/2/4
   及自然对齐。依然仅支持 domain0000 的首256字节；未增加 ECAM 或扩展配置区协议。
2. `pci_get_domain_bus_and_slot(0, bus, PCI_DEVFN(dev,fn))` 获取已枚举设备及其引用；找不到返回
   `-ENODEV`。过去读不存在 BDF 可能得到全1，现不再把这种不存在设备的访问报成功。
3. `pci_cfg_access_trylock()` 遇到已阻塞的配置访问返回 `-EBUSY`，不越过阻塞或无限等待。
   获锁后调用 `pci_read/write_config_byte/word/dword()`，由内核选择实际配置机制和同步方式。
4. 用 `pcibios_err_to_errno()` 将 PCIBIOS 状态转为负 errno；成功读才更新输出。
   获锁后所有路径 unlock，取得引用后所有路径 `pci_dev_put()`。

接口和引用约定见 [4.18 公共 pci.h](https://raw.githubusercontent.com/torvalds/linux/v4.18/include/linux/pci.h)、
[设备查找实现](https://raw.githubusercontent.com/torvalds/linux/v4.18/drivers/pci/search.c)及
[6.12 配置锁实现](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/pci/access.c)。
`pci_user_*` 虽在核心导出，但4.18/6.12公共 `include/linux/pci.h` 没有其声明，因此本项目不复制
私有原型，也不依赖未随发行版 headers 安装的 `drivers/pci/pci.h`。

引用保护对象寿命，不保证热拔插过程中的设备一直可用；内核访问错误如实返回。trylock 协调
核心配置阻塞，单次 accessor 协调底层访问，不意味着任意寄存器写入与绑定驱动的业务逻辑相容。
这里不自动 enable/reset/resume PCI 设备，不证明休眠/热拔插/固件争用已通过真机验证。

## EC：交给 ACPI 驱动发现和串行化

删除自有 EC raw spinlock、固定0x62/0x66和关中断轮询，扩展EC操作使用公共 `ec_read/ec_write`。
公开声明参见 [4.19 acpi.h](https://raw.githubusercontent.com/torvalds/linux/v4.19/include/linux/acpi.h)及
[6.12 acpi.h](https://raw.githubusercontent.com/torvalds/linux/v6.12/include/linux/acpi.h)，
实际发现/锁/错误语义参见 [6.12 EC 实现](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/acpi/ec.c)。
4.18原始ACPI文件本轮网络获取失败，EL8是否具备接口须以目标headers编译及modpost结果为准，
没有把4.19源码冒称为4.18实测。

这个接口明确指向 Linux ACPI 注册的第一个 EC：不代表任意主板的指定 EC，更不推断寄存器含义。
系统ACPI驱动负责事务mutex以及固件要求时的global lock；无EC时保留 `-ENODEV`，事务超时等
错误原样返回。`CONFIG_ACPI` 未启用时返回 `-EOPNOTSUPP`，绝不回退猜测端口。
请求的index和写值均验证≤255后转换；失败读不使用残留值。

能力选择使用目标Kconfig宏，不判版本号。PCI公开接口作为构建必需项，ACPI接口在启用时必须
编译和modpost成功，缺失会让CI失败；没有静默禁用错误构建，也没有新加版本号分支。
原 `class_create` Kbuild编译探测保留。当前GUI没有EC页面或启动时EC扫描，四个平台的EC定义
仍未知；本轮不向任何真实EC发请求。

## HAL：端口权限属于当前线程

原 `h->io_ready` 在一次 `iopl(3)` 成功后永久跳过权限建立，共享句柄即使串行转交线程也可能
执行未经授权的IN/OUT。依据 [Linux x86 ioport.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/x86/kernel/ioport.c)，
IOPL权限属于线程。现在每次直接端口操作先对当前线程调用 `iopl(3)`，失败在执行指令前返回负errno；
不缓存句柄/TID，也不依赖可能被撤销的线程局部“已授权”标记。

这不是自动降低权限的沙箱；成功的iopl仍依Linux规则保留在调用线程中。句柄仍需单线程使用或
外部串行化；同一mailbox的并发、异步signal handler撤销权限与指令间的竞态不在本轮保证范围。
原GUI自己的直接iopl路径没有因此改变。

## 离线回归、打包和复现

执行 `make -C port/tests check`。现有loopback、真实HAL传输替身、parity selftest继续运行，新增：

- `bus-acpi` / `bus-noacpi`：直接编译生产 `octool_bus_access.h`，用API替身验证高64位非法字段、
  三种宽度与末端对齐、设备缺失、配置锁忙、读写失败、引用/解锁顺序、EC错误和无ACPI分支。
  替身还会在失败时写入污染值，要求适配层丢弃。测试不证明内核锁实现或硬件正确。
- `port-thread`：真实HAL仅以测试include路径替换 `sys/io.h`，所有IN/OUT变为权限断言。
  同一handle从已成功线程转交无权限线程、返回原线程撤销权限、再次授权及非法宽度均覆盖；
  失败不得调用端口替身或覆盖输出。测试不执行真实iopl或任何特权指令。
- DKMS stage内容检查新增生产适配头；rpm/deb及独立 `dkms-install.sh` 均复制该文件。
  新增测试可执行文件被Git和源包排除，仍产出点号命名 `octool-2.0.1-src.tar.gz`。

所有目标kernel/desktop job已有 `make check` 门禁；11套内核树的实编/modpost、DKMS安装重装移除、
十目标GUI构建与窗口门禁用于验证此变更。Windows本地没有Linux C工具链，不能用静态检查冒充实编。

## 未完成及实测记录

仍未解决模块能力握手、采集器完整fd/mmap生命周期、旧GUI错误等待/完整主窗口与四平台真实硬件。
尤其不能把能read/mmap的旧模块当作支持MSR/PCI/EC扩展命令的证明。后续单独实现能力识别，
保持旧MMIO协议。真机按 [验收清单](hardware-acceptance.md)记录；不提供猜测寄存器地址。

本轮修正的云端结果尚待执行，不能沿用 `a9ffbcd` 或 `bf43013` 的结果签本轮代码。

本地静态检查通过：canonical ABI哈希不变、HAL修正哈希固定、文档链接和源包重复生成一致。
19项Python测试中16通过、2项需要Linux工具而跳过、1项延续Windows Python3.14 tarfile
`realpath(..., ALLOW_MISSING)`沙箱WinError5。未改安全filter规避该错误，Linux结果待云端确认。
