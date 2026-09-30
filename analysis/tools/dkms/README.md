# DKMS 模板

`dkms.conf.example` 是给 peter_kernel 用的 DKMS 配置。它和实测用的 `pkprobe` 配置只差名字：

| 发行版 | dkms | 内核 | build + install |
|---|---|---|---|
| Ubuntu 20.04 | 2.8.1 | 5.4.0-216、5.15.0-139 | ✓ |
| Ubuntu 22.04 | 2.8.7 | 5.15.0-194、6.8.0-138 | ✓（6.8 HWE 需要 gcc-12） |
| Ubuntu 24.04 | 3.0.11 | 6.8.0-142、6.17.0-42、7.0.0-34 | ✓ |
| Ubuntu 26.04 | 3.2.2 | 7.0.0-34 | ✓ |

四个 dkms 版本的日志里都没有 deprecated / obsolete / ignored 之类的告警。

## 打包要点

- 源码与 `dkms.conf` 装到 `/usr/src/octool-kmod-<version>/`，postinst 执行 `dkms add` 和 `dkms autoinstall`，prerm 执行 `dkms remove --all`。
- 依赖：
  - Ubuntu：`dkms`、`linux-headers-$(uname -r)`、`gcc`、`make`；22.04 装了 HWE 6.8 内核时还要 `gcc-12`，headers 包不会拉它，缺了会报 `/bin/sh: 1: gcc-12: not found`。
  - EL：`dkms`（EPEL）、`kernel-devel-$(uname -r)`、`gcc`、`make`、`elfutils-libelf-devel`。
- Secure Boot：
  - Ubuntu 的 dkms 通过 `update-secureboot-policy`（shim-signed）生成 MOK 并签名；没有时提示「modules won't be signed」，照样装未签名模块。
  - EL 的 dkms 3.x 用 `/var/lib/dkms/mok.key`/`mok.pub`。
  - 两边都要用户 `mokutil --import` 公钥并重启确认一次。
- EL9/EL10 的 kABI 按小版本变化（见 `docs/linux-porting.md` §4.4），所以不要改成跨小版本的 kmod 包。

## 复现

```sh
mkdir -p /usr/src/pkprobe-1.0
cp tools/kmod-probe/pk_probe.c tools/kmod-probe/Kbuild /usr/src/pkprobe-1.0/
sed -e 's/octool-kmod/pkprobe/; s/@VERSION@/1.0/; s/peter_kernel/pk_probe/' \
    -e 's#/build modules"#/build PK_VARIANT=compat modules"#' \
    tools/dkms/dkms.conf.example > /usr/src/pkprobe-1.0/dkms.conf
dkms add -m pkprobe -v 1.0
dkms build -m pkprobe -v 1.0 -k "$(uname -r)" && dkms install -m pkprobe -v 1.0 -k "$(uname -r)"
dkms remove -m pkprobe -v 1.0 --all    # 验证完删掉
```
