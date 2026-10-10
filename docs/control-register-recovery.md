# Intel 控制读回、HWP 活动窗口与 CLI 寄存器访问

本轮共用核心新增 HWP 活动窗口，GUI 与 CLI 同时提供读取和设置。
现有 8 项 RAPL、4 项 HWP 控制与新增窗口共 13 项设置，均检查完整配置寄存器读回。
CLI 另补齐 GUI 已有的原始 MSR、MMIO 和 PCI 读写入口。
这些功能不代表 AMD PState 写入、曲线写入、Intel VF/逐核/服务器控制或板级调压已经完成。

## HWP 活动窗口

依据 [Intel SDM Volume 3B](https://cdrdv2-public.intel.com/835755/253669-sdm-vol-3b.pdf)
§16.4.4、图 16-6，`IA32_HWP_REQUEST` 的 bits 41:32 是活动窗口。
按 7 位尾数和 3 位十进制指数解码为微秒。设置接受整数 0..1270000000；
0 由硬件选择，正数向下取可编码值，例如 15333 us 编码后为 15000 us。

仅在 CPUID.6:EAX[9] 宣告支持、HWP 已启用、未使用 package control 且 desired=0 时可设置。
程序保留其它字段，不自动修改 desired 或 package control。
GUI 的 Intel Controls 列表显示微秒；CLI 使用：

```sh
sudo octool-cli intel-set --cpu 0 --field hwp-window-us --value 15333 --apply
```

这是交给硬件的优化提示。寄存器读回确认保存的配置，不代表实际频率、功耗、
工作负载历史窗口或稳定性已经测量。

## RAPL/HWP 写后验证

设置前重新检查身份、能力、单位、锁和完整旧值。只修改选中字段。
写入后重新读取身份与配置，并比较完整 64 位寄存器及单位/能力上下文。
设置已经相同则验证新快照并跳过写入；写入或读回失败、其它位变化、能力变化均返回错误。
不会自动重试、回滚或覆盖后来发生的操作系统电源策略更新。

CLI 保留 `write_attempted`、`completed_writes`、`submitted`，增加 `unchanged`、
`msr`、`expected_raw`、`readback_raw` 和 `readback_value`。
`verified:true` 只表示本次完整读回一致。失败时 `readback_value:null`；
如获得有效原始读回，仍保留 `readback_raw` 供诊断。
GUI 成功时刷新快照并清空输入，失败时清除旧的可编辑快照。

## CLI 原始寄存器访问

与 GUI RegisterPanel 复用 `HardwareService`、HAL、驱动以及 `validateRequest`。
命令先完成全部参数校验，再打开设备。原始地址和值支持十进制或 `0x` 前缀的十六进制，
全程使用 64 位整数，不经过浮点数。JSON 原始值输出十六进制字符串。

```sh
sudo octool-cli register-read --space msr --cpu 0 --address 0x606
sudo octool-cli register-read --space pci --bus 0 --device 0 --function 0 --address 0 --width 4
# MMIO：register-read --space mmio --address ADDRESS --width 1|2|4|8
# 写入：register-write 加相同目标参数、--value VALUE 和 --apply
```

MSR 为 32 位索引、64 位访问，必须明确 CPU。
MMIO 必须明确物理地址与访问宽度；PCI 必须明确全部 BDF 和宽度，仅支持 domain 0 的前 256 字节。
拒绝未对齐、越界、溢出、宽度不支持、参数混用及没有 `--apply` 的写入。
原始写入只提交一次，不自动预读或读回，因为目标可能是命令、W1C 或只写寄存器。
它不提供高级控制中的型号、锁位或保留其它字段检查；`verified` 始终为 false。
已有进程锁不协调其它进程或内核驱动。

## 验证与研究边界

新增回归覆盖全部控制字段、单位取整、相同值跳过、过期快照、保留位变化、
逐次身份/寄存器失败注入、写后取消以及全部 1024 个活动窗口编码。
GUI 测试覆盖确认取消、成功刷新、失败清空、窗口自动模式和能力门控。
CLI 测试覆盖 64 位上下界、MSR/MMIO/PCI 各宽度、失败空值及单次提交。
云端执行状态以本轮验收记录为准；模拟设备测试不是目标主板实测。

本轮重新核对 [AMD PPR 57238 C1 revision 0.49](https://docs.amd.com/v/u/en-US/57238_C1_pub_049)。
下载页标题标注 Models 00h–0Fh，但包内 Volume 1 首页与页眉明确写 Model 02h C1。
第 240 页要求 VID 在同一处理器的各核一致，其它 PState 字段在一致性域的各核一致。
因此没有据此扩大型号白名单或开放单核 PState 写入。
来源的校验值见 [source manifest](validation/control-register-sources.json)。
