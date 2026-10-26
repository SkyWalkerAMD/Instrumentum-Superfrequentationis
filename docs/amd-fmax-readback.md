# Shimada Fmax 固件设置回读

GUI 和 CLI 共用同一实现，新增 `6Eh` 读取及 `70h` 写后校验。
限定 AMD family 1Ah / model 08h、PCI 1022:153a、0000:00:00.0，
且 RSMU `02h` 必须报告 SMU **115.46.0 / 00732E00h**。
这是固件保存的共享频率上限，不是实测核心频率，也不是熔丝频率的替代显示。

```sh
octool-cli amd-pbo-fmax-read --cpu 0
# 用本机上一步的 ceiling_mhz 与 capabilities_raw 替换两个占位值。
octool-cli amd-pbo-fmax-set --cpu 0 --mhz 5000 --expect-mhz PREVIOUS_MHZ --expect-caps PREVIOUS_CAPS --apply
```

本次更新要求设置命令额外提供 `--expect-mhz`。旧脚本省略该参数会在打开硬件后端之前失败。
GUI 使用“Read Fmax → Apply Fmax”；单独读取 PBO capability 不会取得可编辑的 Fmax 快照。
每次编辑都比较固件版本、完整能力字和旧 MHz 值，要求 PBO 可用且手动 OC 关闭。
输入范围仍为 1–1048575 MHz，仅表示应用编码边界，不是推荐工作范围。

设置只发送一次，随后重新核验版本、能力及 `6Eh` 读回值。读回与请求一致时 `verified=true`；
若固件裁剪请求或其它写入者改变该字段，返回错误并保留实际 `after.ceiling_mhz`。
命令被接受不代表读回匹配。旧值已经匹配时不发送设置，`unchanged=true`。
没有重试、回滚、隐式 OC 模式切换。服务内事务不能排斥其它程序或固件并发访问。

## 固件证据

输入是 ASUS Pro WS TRX50-SAGE WIFI 1317 中的 SMU，SHA256：
`a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7`。
RSMU 表 `7C38h` 的 `70h → 30054h`、`6Eh → 30924h`：

| 路径 | 已核对的指令行为 |
|---|---|
| 写入 | 整数 MHz 转 binary32，乘 `0.001f`，经过策略分支及裁剪，`ssi` 保存到 `E170h + 8Ch = E1FCh` |
| 读取 | `lsi` 读取 `E040h + 1BCh = E1FCh`，乘 `1000.0f`，`ROUND.S` 转整数 MHz，返回 argument 0 |
| 后续 | 设置路径调用策略 helper，并以 `72h` 通知 MP5；这不等于测得时钟已经改变 |

[有限指令回放](../analysis/tools/firmware-fmax-replay.py)检查固定原始字节、表入口、两种地址别名，
使用合成 RAM、熔丝、许可状态和邮箱 helper。510 组设置中 420 组接受并逐次读取同一保存字段；
90 组拒绝且未改写该字段。覆盖 169 条处理函数指令中的 160 条，未覆盖地址逐项保留在报告。
浮点测试限定有限 binary32、nearest-even；没有模拟异常浮点、MP5 消费者或真实硬件。

## 分析器修正

Ghidra 12.1.4 在 `const.s` 等 Xtensa FP 指令处停止分析，原版浮点转整数语义还存在错误。
[隔离的扩展](../analysis/tools/ghidra/SmuFpPatch.py)锁定上游源码哈希，只修改云端工具副本，不修改固件。
编码和操作数依据固定的 [QEMU Xtensa 定义](https://github.com/qemu/qemu/blob/d73149283205645625bfa9fe029cfb68e4c4a9ae/target/xtensa/core-de233_fpu/xtensa-modules.c.inc)，
常量和整数舍入依据同提交的 [翻译实现](https://github.com/qemu/qemu/blob/d73149283205645625bfa9fe029cfb68e4c4a9ae/target/xtensa/translate.c)。
`const.s` 的四种值采用精确语义；近似倒数、细化步骤和转换运算保留显式 opaque 操作，不能用来声称数值仿真完成。
[独立合成程序检查](../analysis/tools/ghidra/SmuFpSelfTest.java)覆盖 11472 个编码，包含全部 FP 寄存器组合、保留常量及转换尺度。

修正后 SMU 的坏指令中断函数从 79 降为 7，MP5 从 71 降为 7。
函数发现数量有变化，不能把中断减少当作完整恢复率。
功耗调整消费路径已能看到有符号 counts 进入计算；单位仍未独立证实。
双 rails 第二字段的物理轨映射和安全写入条件仍未闭合，运行时继续不开放 `61h` 写入。
此项软件及静态验证不替代真机验收。
