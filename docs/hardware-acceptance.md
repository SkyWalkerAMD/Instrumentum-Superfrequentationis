# 真机验收清单

适用目标：EL8/9/10（Rocky、Alma、RHEL 分别记录），Ubuntu20.04/22.04/24.04/26.04，
Debian11/12/13，x86_64。每个目标都必须单独留下结果，不能由一个容器推断其他目标已通过。
本清单尚未在本轮执行。运行前以 [verification-status.md](verification-status.md) 为准。

## 0. 记录环境与证据

- [ ] 源码归档 SHA-256、commit（如有）、版本、包 SHA-256、测试日期、测试人。
- [ ] `/etc/os-release`、`uname -r`、CPU 型号、主板/BIOS、显示会话 X11/Wayland。
- [ ] `mokutil --sb-state`、`/sys/kernel/security/lockdown`（若存在）、DKMS 版本。
- [ ] 旧 `.ko` 的实际文件名、`modinfo`、签名者、vermagic，确认它匹配当前内核。
- [ ] 保存原始构建日志、包安装日志、`dkms status`、`journalctl -k -b`。

不确定的面板值/单位/位域含义记录为“待作者确认”，不能由数值大小或相似工具猜测。
本对拍比较原始 MMIO 返回值，不引入温度、电压或频率换算。

## 1. 安装与 DKMS

从源码根目录构建测试工具：

```sh
make -C port/tests check hwio_smoke
python3 -m unittest discover -s port/tests -p 'test_*.py' -v
```

Debian/Ubuntu：

```sh
sudo apt-get update
sudo apt-get install "linux-headers-$(uname -r)" gcc make libelf-dev dkms mokutil openssl
sudo apt-get install ./dist/octool-hwio-dkms-2.0.1-1.amd64.deb ./dist/octool-2.0.1-1.amd64.deb
```

EL（先启用该系统对应的 EPEL 与 CRB/PowerTools；RHEL 使用已订阅的 BaseOS/AppStream/CodeReady）：

```sh
sudo dnf install "kernel-devel-$(uname -r)" gcc make elfutils-libelf-devel dkms mokutil openssl
# 将包路径换成该 EL 目标产物的确切文件名。
sudo dnf install ./dist/octool-hwio-dkms-2.0.1-1.el9.noarch.rpm ./dist/octool-2.0.1-1.el9.x86_64.rpm
```

不要给 EL8 装 EL9 包；源包可共用，发行包按目标生成。
如果头文件要求的 gcc-N 没安装，先安装对应编译器再重新配置包。

```sh
dkms status -m octool-hwio -v 2.0.1 -k "$(uname -r)"
modinfo -k "$(uname -r)" octool_hwio
```

- [ ] 当前内核条目为 `installed`，vermagic 第一项匹配当前 `uname -r`。
- [ ] `/usr/src/octool-hwio-2.0.1/{kmod,abi}` 内容齐全。
- [ ] 包装头引用 canonical ABI，设备名仍是 mydev，没有覆盖系统 libc。
- [ ] 包安装失败没有被 `|| true` 隐藏。RPM 的 scriptlet warning 也按失败排查。

## 2. Secure Boot：签名、登记和加载分开验收

参考依据：[DKMS 当前手册](https://github.com/dkms-project/dkms/blob/main/dkms.8.in)、
[Debian11 DKMS 手册的 sign_tool](https://manpages.debian.org/bullseye/dkms/dkms.8.en.html)、
[RHEL9 模块签名文档](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/9/html/managing_monitoring_and_updating_the_kernel/signing-a-kernel-and-modules-for-secure-boot_assembly_managing-kernel-command-line-parameters-with-uki)。

先查看本机 `man dkms`、`/etc/dkms/framework.conf` 和构建输出。
根据实际支持的配置选路径，不单靠发行版或 DKMS 版本字符串判断。

### 2.1 使用已有 DKMS 签名机制（优先）

现代 DKMS 支持 `mok_signing_key` / `mok_certificate`。
Ubuntu 的发行版 DKMS/shim 集成可能使用 `/var/lib/shim-signed/mok/MOK.priv` 与 `MOK.der`；
其他发行版常见 `/var/lib/dkms/mok.key` 与 `mok.pub`。必须以本机日志和配置的路径为准。
不新建第二套密钥覆盖已登记的密钥，也不把 CI 的临时测试证书拿来登记。

```sh
# 使用构建日志显示的 DER 证书实际路径替换这一值。
CERT=/实际路径/MOK.der
sudo mokutil --import "$CERT"
# 手工重启，在固件的 MokManager 中确认登记；此交互不能由普通容器替代。
sudo mokutil --test-key "$CERT"
modinfo -F signer octool_hwio
modinfo -F sig_hashalgo octool_hwio
```

如果 DKMS 尚无密钥而本机手册支持 `generate_mok`，可先执行 `sudo dkms generate_mok`，
然后核对输出路径。若手册没有该能力，不执行不存在的子命令，使用下一节。

### 2.2 旧 DKMS 的 sign_tool

仅在本机 DKMS 支持 `sign_tool`、且没有已配置的签名工具时使用下面的示例。
已有其他模块使用签名配置时，需要管理员合并配置；包本身不会改全局 framework.conf。

先确认下面两个文件**不存在**，再生成本机专用密钥：

```sh
sudo sh -eu <<'SH'
test ! -e /var/lib/dkms/octool-mok.key
test ! -e /var/lib/dkms/octool-mok.der
umask 077
openssl req -new -x509 -newkey rsa:3072 -nodes -days 3650 \
  -subj '/CN=OCTool local module signing/' \
  -keyout /var/lib/dkms/octool-mok.key -outform DER -out /var/lib/dkms/octool-mok.der
chmod 600 /var/lib/dkms/octool-mok.key
chmod 644 /var/lib/dkms/octool-mok.der
SH
```

在 `/etc/dkms/framework.conf` 中配置：

```sh
sign_tool="/usr/libexec/octool/dkms-sign.sh"
```

辅助脚本接受 DKMS 传入的 `<kernel-release> <module.ko>`，使用该内核的 `scripts/sign-file`
进行 SHA-256 签名，并验证 signer 字段非空。脚本默认使用上面的 key/DER 路径。
如要给新 DKMS 使用同一密钥，则配置 `mok_signing_key` 和 `mok_certificate`，
使用 DKMS 自带签名流程，不同时配置两种互相覆盖的机制。

配置完后为当前内核重建：

```sh
sudo dkms build -m octool-hwio -v 2.0.1 -k "$(uname -r)" --force
sudo dkms install -m octool-hwio -v 2.0.1 -k "$(uname -r)" --force
sudo mokutil --import /var/lib/dkms/octool-mok.der
# 重启并在 MokManager 确认后：
sudo mokutil --test-key /var/lib/dkms/octool-mok.der
modinfo -F signer octool_hwio
```

签名后不要再 strip 或修改 `.ko`。给手工 `insmod` 的原始文件签名时，可直接运行：

```sh
sudo sh port/packaging/dkms-sign.sh "$(uname -r)" port/kmod/octool_hwio.ko
```

- [ ] Secure Boot 为 enabled，DER 证书已登记，模块 signer 正确。
- [ ] `sudo modprobe octool_hwio` 成功，内核日志没有拒签/不信任错误。
- [ ] 再安装一个内核及对应头文件，DKMS 自动重建并签名；重启到该内核后再次加载成功。
- [ ] key 只保留在本机受控路径；发行包和源包没有私钥。

“模块已签名/能加载”和“旧 GUI 所有硬件功能在 lockdown 下可用”是不同验收项。
旧 GUI 调用点保持不变，不能据此承诺其 iopl、MSR 写或 /dev/mem 直接路径可用。
新基础版 GUI 通过 HAL 访问；必须在已签名模块真实加载后单独验证各族后端及错误处理。

## 3. 模块与 HAL 真机闭环

确认旧模块未占用 `/dev/mydev` 且没有程序正在使用它。先用测试机停掉 GUI，再按旧模块
实际名称卸载；不要猜测模块名或对繁忙设备强制卸载。

```sh
sudo modprobe octool_hwio
ls -l /dev/mydev
sudo ./port/tests/hwio_smoke
```

- [ ] /dev/mydev 为 root:root 0600。
- [ ] `hwio_smoke` 各族后端都为 module；fallback 不能算模块通过。
- [ ] CPUID leaf0 与本地指令一致，在线逻辑 CPU 数与 sysconf 一致。
- [ ] MSR 读的返回码单独记录；某 CPU 不实现指定寄存器时，不从失败推断电压/温度含义。
- [ ] 选择两个确实在线的逻辑 CPU，核对请求 CPU 不被 per-open token 覆盖；对不存在的 CPU
  发起只读 CPU 命令，必须 EINVAL，不能返回其他 CPU 的值。不要为测试强制下线生产 CPU。
- [ ] `journalctl -k -b` 无 oops、锁错误等异常。
- [ ] 没有因为设备权限问题而放宽 udev 到 0666 或移除 capability 检查。

手动构建场景可用 `sudo insmod port/kmod/octool_hwio.ko`；开启 Secure Boot 时必须先签名并登记证书。
不要让旧模块和新模块同时争用同一设备名。

## 4. 新旧 MMIO 对拍（必要验收）

旧 `.ko` 必须适合当前内核；旧模块若不能编/不能加载，不允许强制忽略 vermagic 来“做对拍”。
应换到它支持的参考测试内核，记录该限制。Secure Boot 开启时两份模块都需被信任。

先停掉上一步 GUI/测试进程，卸载新模块；装旧模块，使旧模块独占 `/dev/mydev`。
新模块在对拍时通过 `devname=mydev_v2` 使用第二个节点。

```sh
make -C port/tests octool_capture.so octool_parity
make -C port/kmod KDIR="/lib/modules/$(uname -r)/build"
# 有 Secure Boot 时，先按第 2 节签名这里的新 .ko。

sudo env DISPLAY="$DISPLAY" XAUTHORITY="${XAUTHORITY:-}" \
  OCTOOL=/实际路径/原Linux发行包/octool \
  NEW_KO="$PWD/port/kmod/octool_hwio.ko" \
  CORPUS="$PWD/octool-corpus.bin" \
  sh port/tests/parity-run.sh
```

这里 OCTOOL 必须指向保留的旧 Linux GUI，用它验证原有调用序列；不能误填新基础版的安装路径。
旧 GUI 的 Ubuntu22.04 运行依赖和旧 .ko 的 vermagic 必须同时满足，可用参考测试系统采集。
只有新基础版采集的手工地址不等于旧面板覆盖；可作附加诊断，但需分别标明来源。
采集需要能在授权桌面会话运行 GUI；如果显示认证失败，按该桌面的认证方式排查，
不要使用 `xhost +` 放开所有客户端。
采集阶段正常查看所需面板然后退出；观察库不另发写请求，但 GUI 自身的操作仍可能写硬件。
重放阶段仅重放采集到的 MMIO 读。未知寄存器不自造地址，不盲目扩大扫描范围。

复用已有 trace：

```sh
sudo CORPUS="$PWD/octool-corpus.bin" \
  NEW_KO="$PWD/port/kmod/octool_hwio.ko" sh port/tests/parity-run.sh
```

逐项核对：

- [ ] 旧设备 `/dev/mydev`、新设备 `/dev/mydev_v2` 分属预期模块，trace 有实际 MMIO 读。
- [ ] live 模式、stable match > 0、MISMATCH(stable)=0、err-parity=0、退出码 0。
- [ ] 空 trace、截断 trace、全不可读或没有稳定可读地址退出码 2，不能列为通过。
- [ ] volatile 和 volatile-oob 数量/地址保存。三读只是降低时间漂移干扰的启发式方法，
  不是证明易失寄存器实现等价；必须根据作者已确认的寄存器语义进一步分析。
- [ ] 保存 trace SHA-256、完整结果、旧新模块 SHA-256 和内核日志。
- [ ] offline 模式仅供参考，不作为替代 live 的签字结论。
- [ ] 采集库会增加等待/日志开销，不能声称“时序完全不受影响”。

写操作效果不做双执行对拍。其请求编码由 loopback 覆盖；若验收某个写入功能，
先由作者确认寄存器、单位、范围和恢复方法，另列测试用例。

## 5. GUI 与安装生命周期

- [ ] `ldd /opt/octool/bin/octool-real` 无 not found/版本错误；没有动态 ICU 或 libjpeg 依赖。
- [ ] 普通桌面启动主窗口；在 X11 与 Wayland/Xwayland 分别记录实际测试的会话。
- [ ] EL10 在真实 Wayland 会话和 `headless-smoke.sh xwayland` 均通过；不以 Xvfb 结果代替。
- [ ] 日志可写、字体/图片正常，AT-SPI 可从实际使用的自动化客户端访问。
- [ ] 硬件读取所需的权限在当前会话明确授予；不能把窗口出现当作传感器或超频成功。
- [ ] 基础版 Information 页的 CPU 型号/在线 CPU 数/OS/内核与系统工具一致；无模块普通用户
  也可进入窗口，读取失败清空旧结果并显示错误码，不能显示伪造零值。
- [ ] MSR 的 CPU 为十进制，寄存器/值为十六进制；MMIO/PCI 宽度与输入范围明确，完整 64 位
  输入不被截断。PCI 仅 domain 0000、首 256 字节，不把扩展 offset 截断。
- [ ] 只用作者确认可读且非 read-to-clear 的寄存器比较原始结果；更换目标/宽度后旧结果清空。
  具体地址、BDF、MSR 编号和语义填入测试记录，本清单不提供猜测地址。
- [ ] 写入先由作者确认目标、值、范围和恢复方法；检查取消不发请求、确认后只写一次，
  UI 不自动回读。没有批准的硬件写用例时，该项标未验收，不用 CI 内存 transport 代替。
- [ ] 面板名称、单位和关键数值由作者确认，未确认项留空，不创造容差或换算关系。
- [ ] 卸载 GUI 和模块包，再安装，DKMS 状态与设备权限正确。
- [ ] 升级内核后有新版本的已签名模块；旧内核仍可启动和加载。

GUI 如需 root，使用该机器的合法本地显示认证，在已授权的会话中保留所需 DISPLAY/XAUTHORITY；
这一步须在目标桌面验收。当前包没有加入提权代理，设备 capability 检查和 root:root 0600 保留。

## 6. 单目标验收记录模板

| 项目 | 结果/证据路径 |
|---|---|
| OS 品牌/版本/架构 | |
| kernel release / gcc / header 包 | |
| GUI 源码/包/ELF SHA-256 | |
| DKMS build/install 与签名 | |
| Secure Boot enabled / MOK 已登记 / modprobe 成功 | |
| HAL module backend 与 smoke | |
| live 对拍：stable/volatile/oob/mismatch/error 数量、退出码 | |
| X11 / Wayland / headless GUI | |
| 内核升级、卸载/重装 | |
| 作者尚需解释的硬件字段/单位 | |
| 测试人、日期、阻塞项 | |

表中有必需项空缺就标“未完成”，不签“全部通过”。
