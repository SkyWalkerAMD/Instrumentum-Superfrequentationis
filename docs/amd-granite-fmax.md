# Granite Ridge Fmax 读取、设置与回读

GUI 和 CLI 共用的 Fmax 服务新增 Granite Ridge 支持。读取的是固件保存的
共享频率上限，单位 MHz；写入后比较这个保存值，不是测量实际核心频率。
本功能不推断 PM 槽位、物理 CCD、OS 核心或电压 rails 的对应关系。

## 适用范围

- 显式选择 `granite-ridge`，CPUID 为 AMD family `1Ah` / model `44h`，
  PCI `0000:00:00.0` 为 `1022:14d8`，且未报告虚拟机环境。
- 精确 SMU 版本 `98.84.0` / `0x00625400`；不同版本拒绝继续查询或写入。
- 证据来自 ASUS ProArt X870E-CREATOR WIFI BIOS 2503 的 SMU，适配目标为
  用户的 9950X3D。实际 BIOS 未知，目标机器不可用，尚未真机验收。
- 原有 Shimada `115.46.0` 路径保留。Granite 的 Fmax 支持不会放开其他
  Shimada 曲线、Scalar、手动 OC、PPT/TDC/EDC 或任意 SMU 命令。

## 使用

GUI 在 AMD 调参页的 Platform 中选择 `Granite Ridge · Fmax · SMU 98.84.0`，
先点 Read Fmax，再填写所需上限并点 Apply Fmax。切换平台、CPU 或 PCI
目标会使旧快照失效。界面同时显示请求值、固件确认、回读值和是否匹配。

CLI 读取示例（CPU 0 仅为示例，须选择当前进程允许的逻辑 CPU）：

```sh
octool-cli amd-pbo-fmax-read --cpu 0 --platform granite-ridge --json
```

只有读取返回旧值 5500 MHz、完整能力字 `0x00000004` 时，下面的比较值才适用：

```sh
octool-cli amd-pbo-fmax-set --cpu 0 --platform granite-ridge \
  --mhz 5000 --expect-mhz 5500 --expect-caps 0x00000004 --apply --json
```

数值是调用示例，不是超频建议。程序接受正整数 `1..1048575` MHz，属于沿用
的应用编码范围。固件具有自己的限幅和权限条件。能力位要求 PBO 可用且
手动 OC 未启用；完整能力字和旧 MHz 均必须仍与快照一致。

固件版本在采集前后、写入前及写后采集时核对；身份在关键操作之间复核。
已有值相同则跳过设置。只发出一次设置；拒绝、超时、取消、回读错误或
回读值不同都会保留未验证状态，不自动重试、切换模式或回滚。

JSON 的 `verified` 表示保存的上限与请求相同。即使固件响应成功，也可能
限幅；此时显示实际 `current_fmax_mhz`，操作返回失败。
`operating_frequency_mhz` 保持 `null`，`hardware_effect_measured` 保持 `false`。

## 固件与 REA 证据

沿用 REA 对原版 `set_fmax` (`0x2397b0`) 及调用者的静态分析，复核原版
功能边界。Granite 固件使用 Ghidra 的字节校验解码和合成 RAM 回放。
REA 不执行目标硬件，也不把原版平台协议直接套用于 Granite。

| 项目 | Granite 98.84.0 证据 |
|---|---|
| RSMU 传输 | 消息 `0x3b10524`，响应 `0x3b10570`，六参数从 `0x3b10a40` 起；沿用已核对的原生 PM 传输坐标 |
| `70h` 设置 | 表 `0x6fa4 + 8*0x70` 指向 `0x29e54`；输入 MHz 转 binary32 后乘 0.001 |
| 默认限幅 | 8 位 fuse `0x2c33` × 0.025 得到基准，上下界为基准 +0.2 / −1；浮点运算单位为 GHz |
| 条件路径 | RAM `0x11208` 的系数、模式字节和权限结果决定限幅与仅允许下调的分支；输入零返回 FD |
| 保存与传递 | 选中的 float 写到 `0xaee4`；MP5 `90h` 的六字记录在偏移 +8 携带该值；真实跨处理器传输未执行 |
| `6Eh` 读取 | `0x2a7c8` 读取同一 float，乘 1000 后 ROUND.S 输出整数 MHz；为存储回读 |
| `6Fh` 能力 | `0x1f36c` 将 fuse `0x2cda` 放入 bit 2，`0xae68` 的手动 OC 状态放入 bit 16；5D/5E 的置位/清位指令提供来源证据 |

[已有频率策略数据路径](amd-granite-performance-policy.md)给出共享值到 MP5 的
传播证据；[固定版本的 RyzenSMUDebug 文档](https://github.com/Death4two/RyzenSMUDebug-LINUX/blob/a78069caaf73f6245c97d6c8bad5a1236b54f300/README.md)
独立说明 Granite 的 6E/70 以 MHz 读取和设置。

`analysis/tools/firmware-granite-fmax.py` 校验 SMU SHA256、命令表和所执行
指令的原始字节。2496 组策略用例含 2179 次接受、317 次拒绝，1669 次限幅
或零哨兵处理，以及 179 次条件降频接受；另有 4204 组能力位组合。
测试输入包括 fuse、权限返回值和 RAM 策略系数，均为显式合成输入。
所有返回分支得到覆盖，不代表已模拟外设、固件异常状态或所有浮点特例。

SMU SHA256：`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`。
GUI、CLI、核心模拟后端及云端测试结果统一记录在[当前验证报告](amd-granite-validation.md)。

仍未完成的部分包括核心 0/4/12、CCD 0/1/2/6/8 的物理单位，PM 到物理
CCD/OS 核心的绑定，以及第二 rails 的独立写入和设备侧回读。
