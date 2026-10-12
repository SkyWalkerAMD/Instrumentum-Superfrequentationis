# Intel TVB 温度与降倍率分档查询

GUI `Intel Controls → TVB thresholds` 和 CLI `intel-tvb-table` 共用无 Qt 核心。
当前限定 Intel Family 6 / Model B7、支持 MSR 的裸机；查询八个活动 P 核数量候选项，
也可以只查询其中一个。候选编号不是 Linux CPU 号，也不是逐物理核心编号。

```sh
sudo octool-cli intel-tvb-table --cpu 0
sudo octool-cli intel-tvb-table --cpu 0 --active-cores 4
```

`--cpu` 是执行查询的逻辑 CPU；`--active-cores` 只能为 1..8，省略时扫描八项。
不提供该表的 `--apply` 或设置命令。GUI 不会在打开页面时访问硬件，允许复制完整结果。

每项显示温度 A、降倍率 A、温度 B、降倍率 B、原始 32 位值和固件状态。
例如合成值 `0x02644601` 解码为 A=70°C / 1 bin、B=100°C / 2 bins。
零、255 和其它原始编码按实保留，不推断自动值、推荐值、启用状态或无效项。
不从这些值推算实测频率、电压、具体 SKU 能力或已启用物理核心数。

## 依据和接口区别

原 ELF SHA256：`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
[来源记录](validation/intel-tvb-table-sources.json)、[完整原函数静态提取](validation/legacy-intel-tvb-table-static.json)、
[88 场景原指令隔离仿真](validation/legacy-intel-tvb-table-emulation.json)分别记录来源、指令和行为。

| 原入口 | 地址 | 已确认行为 |
|---|---|---|
| `Rd_octvb2(int)` | `0x3931e0` | GET `0x24`；活动核心数量放接口位 15:8，第二参数放 23:16；返回五整数行 |
| `Wr_octvb_vec2(vector,int)` | `0x38d1a0` | SET `0x25`；前八行各重复发送两次，忽略错误 |
| `Rd_octvb3(int)` | `0x3a0180` | GET `0x24`；仅使用接口位 23:16，属于另一目标寻址方式 |
| `Wr_octvb_vec3(vector,int)` | `0x3738e0` | 对应另一目标方式；发送设置后立即返回 |

原 `octvb3::refreshit` 在选择公共活动核心表时给 `Rd_octvb2` 传 0，
逐核查询另走 `Rd_octvb3`。原菜单根据 `isit_adl()` 的 Intel PCI 设备条件选择该页面；
其范围包含现有 B7 平台关联的 `A700`。本实现仍用明确的 B7 CPU profile，未扩展到其它型号。

返回向量为 `[候选编号, byte3, byte1, byte0, byte2]`。
原页面的 `Temp A / Bins A / Temp B / Bins B` 标签、同列输入框位置和刷新赋值
共同确定 byte1 / byte0 / byte2 / byte3 的语义，见来源记录中的 UI 指令摘录。

Intel 官方 [Raptor Lake-S FSP 头文件](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h#L1937)
独立说明两组摄氏温度阈值及倍率减量的含义。它证明字段概念与单位，**不是** mailbox
地址、位布局或运行时写入范围的证明；FSP 配置结构偏移也不是 MSR 地址。

## 新实现的失败处理

查询只发 MSR `0x150` 的 GET `0x24`，interface parameter 1 = 1..8、parameter 2 = 0、data = 0。
共享内部传输明确区分一般命令参数与电压域，未把活动核心数量强制转换为电压域枚举。

- 发送前和发送后各最多 100 次 busy 检查，服从共同事务取消和截止时间。
- 解释完整 8 位固件状态；非零不解码、不显示残留数据。
- 单项固件拒绝不会被当作表尾，继续查询后面的候选；传输失败、超时或取消立即停止。
- JSON 的 `scan_completed` 与逐项 `valid` 分开。被拒绝项的数据为 `null`，未尝试的后续项省略。
- GUI 切换 CPU 或候选时立即清空；异步结果与当前选择不符时丢弃，关闭窗口可取消任务。

读取使用顺序采样，不是原子硬件快照；本进程事务锁不能排除其它程序同时访问 mailbox。
查询会写入 GET 请求，但不会发送配置更改指令。OC lock 不必解除，固件仍可以拒绝查询。

## 仍未开放的部分

分档设置、逐物理核心表、其它 CPU 型号和一键 Boost 配置仍未开放。
原 setter 仅对有符号温度 A 做上限 115 的裁剪，其它字段截断到八位；
它还会无条件重发、忽略固件失败，不能直接作为可靠的新写入契约。
原指令测试确认这些行为，尚未确认完整的合法写入范围、条目可用性和变化后的硬件效果。

既有 [TVB 两项配置开关](intel-tvb.md)继续独立提供；本轮没有修改它们的设置语义。
真实 B7 硬件和其它目标机器的调节效果、稳定性及 Secure Boot 验收仍未完成。
