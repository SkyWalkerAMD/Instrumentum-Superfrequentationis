# Granite Ridge 全核 Curve Optimizer 协议

参考 SMU/MP5 `98.84.0` 中，RSMU `07h` 将低 16 位解释为有符号
CO 计数，通过 MP5 `0Fh` 更新每个选中固件目标的八个局部核心状态。
这些计数不是伏特或毫伏。Granite 的受限路径范围是 **−50..+30**，
不能照搬 Shimada 接口的正向 +50 边界。

输入策略、传输选择与 MP5 存储已由原字节回放；全核主机回读、物理核心
绑定及实际硬件效果未验证。因此产品只能报告命令被接受，不能报告所有
物理核心的曲线设置已经一致或生效。

## 固定证据

参考 ASUS ProArt X870E-CREATOR WIFI BIOS 2503，目标 9950X3D 真机
不可用，已安装 BIOS 未知。

- SMU SHA256：`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`。
- MP5 SHA256：`fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556`。
- REA/Ghidra 导出：`build/granite-study-42432173976/{smu,mp5}/smu-study.json`。
- 证明脚本：`analysis/tools/firmware-granite-curve-all.py`。
- 输出：`build/granite23-curve-all.json`。

| 证据 ID | 位置 | 结论 |
|---|---|---|
| GRC-01 | SMU `0x6fa4 + 8*07h` | handler `0x2bcdc`，flags 6 |
| GRC-02 | SMU `0x2bcdc` | 邮箱参数 low16 经 `L16SI` 解释为有符号值；条件权限失败响应 FF；成功调用 `0x1d760(0Fh, payload)` |
| GRC-03 | SMU `0x2bd0d..0x2bd20` | 受限路径先对 +30 取较小值、再对 −50 取较大值；写回 low16，参数原 high16 保留 |
| GRC-04 | SMU `0x1d760` | 两次 LOOP 分别提交及等待两个目标，选择掩码为 halfword `0x6816`；不使用邻近 `0x6814` |
| GRC-05 | MP5 `0x13298 + 8*0Fh` | handler `0x2f02c`，flags 6 |
| GRC-06 | MP5 `0x2f02c` | signed low16，条件 upper cap +30；LOOP 写八个 `0xee3c + 0x520*i + 0x1fc` word，响应 1 |

固定 [Zen5Settings](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings.cs)
继承的 [Zen4Settings](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen4Settings.cs)
也将 `07h` 定义为全核 margin 命令。实际参数和 Granite clamp 以本页
固件指令为准，未继承其他平台的范围或回退策略。

## 策略与接受语义

SMU byte `0x8197 != 0` 且 MMIO word `0x3210064 != 0x80000000`
时调用 `0x2c910(2)`。平台资格与 fuse `0x2cda` 不允许时，响应 FF，
没有调用跨处理器 helper；允许时限到 −50..+30。其他分支不检查该权限，
也不执行 SMU clamp。应用仍应要求 `6Fh` bit 2 可用、bit 16 手动 OC
关闭，并比较完整能力字、严格校验目标身份及精确固件版本。

应用只提交 margin 的 low16、high16 和其余五个参数为零；整数范围取
−50..+30。该范围的每个整数都经过 SMU → MP5 合成组合测试，不会触发
已知 clamp；它是编码范围，不是对稳定性或电气安全的承诺。

`0x1d760` 对 `0x6816` 选中的每个目标，先清响应 word，再向
`0xf8010600 + (target<<20) + 0x398` 写参数、向 `+0x300` 写 `0Fh`。
两个目标都提交完以后才开始等待。它等待响应变成非零，并没有验证
该非零值等于成功码 1。因此 RSMU 的响应 1 不能证明所有 MP5 目标成功。
若任一响应持续为零，内部等待没有本实验可见的截止条件。

MP5 handler 的八次存储是固件局部索引，不证明物理 CCD、OS CPU 或 PM
槽位对应关系。它会覆盖该目标的八个局部 CO 状态，而不会由应用推断
启用核心数量。目标掩码为空时，SMU helper 也能正常返回；不能仅凭接受
响应报告受影响核心数量。

产品应保持 `verified=false`、当前全核计数未知、受影响核心数量未知、
物理映射未验证。超时或取消时设置可能已提交，不自动重试、回滚、改模式
或改 VID。选核查询协议也不能自动充当全核一致回读。

## 合成检查

| 检查 | 覆盖 |
|---|---:|
| SMU/MP5 两条原 SEXT 指令；所有 low16 × 三组 high16 | 393216 |
| SMU 策略；含 128 拒绝、72 clamp | 384 |
| 双目标路由；两个相邻 mask 独立变化 | 1152 |
| 非成功但非零完成码仍使 helper 返回 | 648 |
| 持续零响应触发回放预算上限；目标均已提交 | 2 |
| MP5 核心存储；2304 个 word 及两侧哨兵 | 288 |
| 应用 −50..+30 每个整数的 SMU/MP5 组合 | 1296 |

REA 伪代码在 `0x1d760` 与 `0x2f02c` 丢失 LOOP，只显示单次操作；
以上数量来自原始反汇编和实际有界循环回放。SEXT 按编码立即数指定的
符号位扩展，15 表示对低 16 位符号扩展；未依据无符号十六进制显示猜测
负数。mailbox、fuse、MMIO 完成响应和锁均为明确的合成边界。

两条 SEXT 使用实际 body 字节 `0x2bd17: 80 44 23` 与
`0x2f055: 80 aa 23`，没有替换成自造指令。额外按固定
[QEMU ISA module](https://github.com/qemu/qemu/blob/d73149283205645625bfa9fe029cfb68e4c4a9ae/target/xtensa/core-de233_fpu/xtensa-modules.c.inc)
独立拆出 opcode、源/目标寄存器与 4 位 `t=8`。`tp7` 解码为 `t+7=15`，
[translate_sext](https://github.com/qemu/qemu/blob/d73149283205645625bfa9fe029cfb68e4c4a9ae/target/xtensa/translate.c)
再按 `immediate+1=16` 位作有符号提取。实际 sign-bit immediate 范围为
7..22，提取的位数才是 8..23。这里的 393216 用例覆盖两个真实指令的
数据输入，固定 immediate 15；没有声称执行了所有指令立即数。
结果的独立 oracle 是 little-endian `int16` 解包。

```sh
python3 analysis/tools/firmware-granite-curve-all.py \
  --smu build/granite-study-input/smu-62-54-body.bin \
  --smu-study build/granite-study-42432173976/smu/smu-study.json \
  --mp5 build/granite-study-input/mp5-62-54-body.bin \
  --mp5-study build/granite-study-42432173976/mp5/smu-study.json \
  --output build/granite23-curve-all.json
```
