# Intel V/F 点查询恢复

GUI 的 Intel Controls → V/F points 和 CLI 的 `intel-vf-read` 同时接入共同核心
`readIntelVf`。仅接受 GenuineIntel family 6/model B7、具有 MSR 能力的 client 配置。
支持 core/domain 0 与 cache/domain 2，单点 1..15 或全部候选点读取。
输出点编号、配置倍率、配置电压偏移（mV）、完整 32 位原值、每点错误与固件状态。
这些值不代表实测频率/电压；逻辑 CPU 选择查询执行位置，不选择物理核心的专用 VF 曲线。

## 原版与来源证据

- 固定原 ELF SHA256 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
- [六函数静态指令](validation/legacy-intel-vf-static.json)：`check_vfpt_valid`、core/cache 写入、
  `percoreoverride_dis` 和两种枚举函数。后两者在本轮只作静态核对。
- [135 组原指令实验](validation/legacy-intel-vf-emulation.json)：四个完整函数字节，固定 fixture；
  执行 client 路径，NVL 标志为零，MSR 调用由合成边界替代。没有执行主程序、系统调用或真实硬件访问。
- 原 client 查询 MSR 0x150 的高 DWORD 为 `0x80000010 | (domain << 8) | (point << 16)`，
  低 DWORD 为 0。core/cache 枚举从 1 开始，增至 16 结束（最多 15 个候选）。
- [Intel Raptor Lake-S FSP 固定版本](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h)
  提供 15 项 core VF 数组并区分 all-core/per-core 配置。FSP 结构偏移不是寄存器地址，数组长度也不是每颗 CPU 的已支持点数。
- [coreboot 固定版本](https://github.com/coreboot/coreboot/blob/cdd0aaaa8e0c2e52a57b515e801b339549e53283/src/drivers/intel/oc_mailbox/oc_mailbox.c)
  交叉核对 MSR 邮箱的 busy、状态与 signed 11-bit offset（1/1024 V）布局。
  新查询复用已恢复的 offset 解码，不复制旧程序 core/cache 不一致的整数舍入。
  上述两份已下载来源的哈希保存在[既有来源清单](validation/intel-oc-sources.json)。

## 错误与事务语义

所有输入先校验；点 0 仅为内部“全部候选”选择，CLI 显式 `--point 0` 被拒绝。
固件请求仅使用 0x10，不发送设置 0x11 或 override 0x14/0x15。
型号/能力不匹配时不访问 MSR。点查询复用有界轮询、事务期限和取消检查，返回完成的同一个样本，
最终期限/取消检查通过后才显示数据。最多每次等待 100 次、相邻轮询间隔 1 ms。

每点保留原编号。固件拒绝只使该点无效，不把非零状态擅自解释成“正常结束”或填入零值；
继续查询其它候选点。MSR 访问失败、busy 超时或取消会终止扫描，保留此前成功点，失败点无数据。
`scanCompleted` 表示所有请求得到完成状态，整体 `error` 仍反映其中的固件错误。
单独的 `IntelVfSnapshot` 类型不能传入全域电压/倍率写入函数。
事务锁目前仍只保护同一进程；GUI/CLI 不能协调同时运行的其它调参程序。

## 尚未开放的写入

原 core VF setter **先读 0x14，再写 0x15 清除 data bit3**，然后访问选定点；
原 cache setter没有这一步。两者都丢弃 data[20:0]，忽略包装器失败和固件错误，忙等待无软件期限。
实验覆盖选择器截断、正负参数、错误返回和持续 busy，证实不能直接包装为可靠 setter。
新查询不会调用这些旧写入函数。VF offset 设置、逐核 override/目标选择及 SPR/GNR/NVL 配置仍待恢复。

## 验证范围

新增六组核心场景覆盖两个域/全部点/单点、型号/能力/范围、固件错误空洞、每个传输故障位置、busy、
末次读取取消/超时。CLI 同时验证 JSON、错误空值及参数拒绝；Qt 同时验证目标失效、错误空值和复制内容。
云端结果与安装产物在完成后记录到验证清单。软件实验不等同于真机调压或稳定性验收。
