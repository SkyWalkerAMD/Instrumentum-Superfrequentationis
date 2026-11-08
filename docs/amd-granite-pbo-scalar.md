# Granite Ridge PBO Scalar 的存储协议与限幅证据

在参考 SMU `98.84.0` 中，RSMU `5Bh` 接收百分之一倍编码的 Scalar，
`6Dh` 返回固件保存的 binary32 倍率。返回值是软件设置的回读，不是电压、
功耗或可靠性测量。本页的证明使用原始指令与合成 RAM，不代表目标机器
已经接受该设置。GUI、CLI 的接入和构建结果另见产品验证记录。

证据来自 ASUS ProArt X870E-CREATOR WIFI BIOS 2503，适配目标为 9950X3D。
实际机器不在用户手边，安装的 BIOS 未知。因此实现必须严格校验
family `1Ah` / model `44h`、PCI `1022:14d8`、非虚拟机及精确 SMU 版本。
不能把同名 Shimada 命令整体移用于 Granite。

## 固定来源和可复现证明

- SMU body SHA256：
  `5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`。
- REA/Ghidra 研究文件：`build/granite-study-42432173976/smu/smu-study.json`。
  每条执行的反汇编都与该 body 的原字节匹配；伪代码只用于定位。
- 独立证明：`analysis/tools/firmware-granite-pbo-scalar.py`。
- 本轮输出：`build/granite23-pbo-scalar.json`。mailbox、fuse 与 MMIO 读取
  是明确的合成输入，浮点倒数函数也是显式返回值边界。

```sh
python3 analysis/tools/firmware-granite-pbo-scalar.py \
  --smu build/granite-study-input/smu-62-54-body.bin \
  --study build/granite-study-42432173976/smu/smu-study.json \
  --output build/granite23-pbo-scalar.json
```

## 协议证据

| 证据 ID | 原始指令位置 | 已建立的事实 |
|---|---|---|
| GRS-01 | RSMU 表 `0x6fa4 + 8*command` | `5Bh → 0x26f78`，`6Dh → 0x26fe0`，`6Fh → 0x1f36c` |
| GRS-02 | `0x26f78` | 无符号参数转 binary32，乘 literal `0x11404 = 0x3c23d70a`，即 binary32 的 0.01 |
| GRS-03 | `0x26f78`、`0x2c910(2)` | 当 byte `0x8197 != 0` 且 MMIO word `0x3210064 != 0x80000000` 时，权限失败强制 1.0，权限成功取请求值与 float `0xa068` 的较小者；其他路径跳过这道限幅 |
| GRS-04 | `0x26de0..0x26e1e` | float `0xa068` 来自 4 位 fuse `0x2c11` 的数值乘 `10^e`，`e` 为 3 位 fuse `0x2c15`；这不是固定 10 倍上限 |
| GRS-05 | `0x26f78`、`0x1c928` | 选定值写入 float `0xa064`；乘状态表选中系数后传给共用消费者，更新状态 float、历史下界及倒数 |
| GRS-06 | `0x26fe0` | 读取 `0xa064` 原始 32 位编码；手动 OC byte `0xae68 != 0` 时输出零，保留存储本身；成功响应 1 |
| GRS-07 | `0x2c910(2)`、`0x1f36c` | 权限由平台状态及 fuse `0x2cda` 决定，和 `6Fh` 的 bit 2（mask `0x4`）对应；手动 OC 是独立 bit 16（mask `0x10000`） |

`5Bh` 所有已覆盖的接受路径都响应 1，强制 1.0 或被限幅也不改变该响应。
它本身没有手动 OC 拒绝分支。应用必须先检查完整能力字、PBO bit 2 和
手动 OC bit 16，并在写入前复核；不能依赖 getter 返回零就认为保存值为零。
写后重新取得能力和 `6Dh` 原始值，比较请求的 binary32 编码，才能报告
设置回读一致。限幅应显示实际回读并保持未验证，不能自动重试或改模式。

应用接受 `1..10` 整数倍率是保守的接口范围；固件初始化不能证明该范围
在目标机器上的电气安全。原始 getter 也不校验浮点特例；应用需拒绝
负数、负零、非有限值和超出支持范围的值。

## 合成覆盖及边界

| 检查 | 覆盖 |
|---|---:|
| 固件上限初始化：所有 4 位 mantissa × 3 位 exponent | 128 |
| 平台状态、权限 fuse 和手动 OC 的 getter / helper 对照 | 108 |
| getter：17 种原始编码 × 3 种 OC 状态 | 51 |
| setter：输入、限幅门、权限、上限、系数索引及旧状态组合 | 9072 |
| setter 接受但回读与请求不同 | 4500 |
| 权限路径强制为 1.0 | 3024 |
| 有权限但被 firmware cap 限幅 | 1692 |

输入包括 0、1、99、100、101、999、1000、1001、超过 binary32 整数精度
边界及 `0xffffffff`。getter 特例包含 NaN、无穷、次正规数及符号零，证明
其按原始编码返回，不能据此声称这些值可用于设置。setter 的实际应用
仍限制为 `1..10`。

`0x1c928` 的 float 乘法、保存与条件选择由原始指令回放；倒数函数
`0x3f870` 使用显式合成返回值，不模拟目标 FPU 近似误差或浮点异常。
外设事务、真实 fuse、跨处理器执行、硬件效果、第二 rails 协议与热稳定性
均未在该证明中验证。
