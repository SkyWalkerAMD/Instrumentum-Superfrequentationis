# kmod-probe — 在目标内核上验证 peter_kernel 的 API 集能否编译

这不是 peter_kernel.c。它只调用 peter_kernel.ko 实际导入的那一组内核接口（从三个 .ko 的
`__versions` 和 DWARF 里读出来的）：chrdev + class + device 节点、由 `.fault` 提供的 mmap
（`get_zeroed_page` 页 + `get_page(virt_to_page())`）、`copy_{from,to}_user`、
`ioremap` + `readl/writel/writeq`、绑到指定 CPU 的 kthread、`kmalloc/kfree`、`cpu_data()`。
它回答的是「这组接口在内核 X 上能不能编译、链接」，不加载、不碰硬件。

## 三个变体

| PK_VARIANT | class_create | 绑核 kthread | 对应 |
|---|---|---|---|
| `asis`   | `class_create(name)` 单参数 | `kthread_create_on_cpu()` | 6.8 / 6.17 两个 .ko 的导入 |
| `old`    | `class_create(THIS_MODULE, name)` 双参数 | `kthread_create_on_cpu()` | 6.2 那个 .ko 的导入 |
| `compat` | 由 Kbuild 探测头文件决定参数个数 | `kthread_create_on_node()` + `kthread_bind()` | 建议 peter_kernel.c 采用的写法 |

`compat` 靠 Kbuild 里 grep 头文件判断 `class_create` 的参数个数，不看 `LINUX_VERSION_CODE`：
RHEL 9 在 9.2（5.14.0-284，双参数）和 9.4（5.14.0-427，单参数）之间换了签名，版本号却都是 5.14。

## 用法

```sh
# EL8/9/10
dnf install -y kernel-devel-$(uname -r) gcc make elfutils-libelf-devel
# Ubuntu（22.04 + HWE 6.8 内核还必须装 gcc-12；headers 包没有依赖它）
apt install -y linux-headers-$(uname -r) gcc make

cd tools/kmod-probe
for v in asis old compat; do
  make clean >/dev/null 2>&1
  if make PK_VARIANT=$v >/tmp/pk-$v.log 2>&1; then echo "$v OK"
  else echo "$v FAIL"; grep -E 'error:|ERROR:' /tmp/pk-$v.log | head -3; fi
done
```

指定别的内核：`make KDIR=/usr/src/kernels/<ver> PK_VARIANT=compat`。

## 已有结果（2026-09-29）

| 内核 | asis | old | compat |
|---|---|---|---|
| Ubuntu 5.4.0-216（20.04 GA） | ✗ class_create 参数个数 | ✗ `kthread_create_on_cpu` undefined | ✓ |
| Ubuntu 5.15.0-139（20.04 HWE） | ✗ 参数个数 | ✓ | ✓ |
| Ubuntu 5.15.0-194（22.04 GA） | ✗ 参数个数 | ✓ | ✓ |
| Ubuntu 6.8.0-138（22.04 HWE，需 gcc-12） | ✓ | ✗ 参数个数 | ✓ |
| Ubuntu 6.8.0-142（24.04 GA） | ✓ | ✗ | ✓ |
| Ubuntu 6.17.0-42（24.04 HWE） | ✓ | ✗ | ✓ |
| Ubuntu 7.0.0-34（24.04 HWE） | ✓ | ✗ | ✓ |
| Ubuntu 7.0.0-34（26.04 GA，gcc-15） | ✓ | ✗ | ✓ |
| Rocky 8.10（4.18.0-553.168.1） | ✗ 参数个数 | ✓ | ✓ |
| Rocky 9.8（5.14.0-687.52.1） | ✓ | ✗ 参数个数 | ✓ |
| Rocky 10.2（6.12.0-211.60.1） | ✓ | ✗ 参数个数 | ✓ |

Rocky 的三个是在 `ctrliq/kernel-src-tree` 的源码树上 `modules_prepare` 后编的，用的是 Ubuntu 的 GCC（9 / 11 / 14），
不是 RHEL 自带的 GCC；没有 `Module.symvers`，「是否导出」用 `../expcheck.sh` 按源码 grep 判断（三个树都 0 缺失）。
真机上用 `kernel-devel` 再跑一次即可替代。

## EL 上的 kABI：只在一个小版本内有效

RHEL 9 起，kABI 稳定列表只在一个小版本内有效（Red Hat 的 RHEL 9 kABI 政策）。用 `../kabicrc.sh` 对 Rocky 树内
`redhat/kabi/` 的核对结果：compat 探针的 33 个导入**全在**稳定列表里，但 CRC 跨小版本在变——
EL9 9.4→9.8 有 14 个变过，EL10 10.0→10.2 有 13 个变过。所以 EL9/EL10 上的 kmod 要按小版本重编，统一用 DKMS 最省事
（模板见 `../dkms/`）。

EL8 停在 8.10，按 RHEL 8 的政策稳定列表在大版本内有效，但 8.10 树里没带列表文件。要核对就在 EL8 真机上执行：

```sh
dnf install -y kernel-abi-stablelists
f=/lib/modules/kabi-current/kabi_stablelist_x86_64
modprobe --dump-modversions pk_probe.ko | awk '{print $2}' | sort -u |
while read s; do grep -qw "$s" "$f" || echo "不在 kABI: $s"; done
```

真正要看的是改好之后的 peter_kernel.ko，对它跑同一段命令。
