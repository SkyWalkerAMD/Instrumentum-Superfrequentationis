# Intel TVB 分档单字段编辑

GUI `Intel Controls → TVB thresholds` 与 CLI 共用 `gui/core`，在已有
[温度 / 降倍率分档查询](intel-tvb-table.md)之上恢复单档位、单字段设置。
当前只允许 Intel Family 6 / Model B7、具备 MSR 的裸机，并要求固件 OC lock 未设置。
这不是逐物理核心 TVB 配置，也不从表项应答推断 SKU 能力或实测运行状态。

## 使用方式

GUI 选择一个 `Active P-cores` 数量，再点 `Prepare selected entry`，读取该项和 TVB 控制状态。
从四个字段中选一项，输入新值；`Apply field…` 展示 CPU、活动 P 核数量和完整档位的新旧值，
确认后重新核对、修改并回读。查询全部候选项不会启用编辑。

```sh
# 仅读取编辑前上下文，不发送设置指令
sudo octool-cli intel-tvb-table --cpu 0 --active-cores 4 --for-edit
# 示例参数，不是推荐配置；温度单位为摄氏度
sudo octool-cli intel-tvb-table-set --cpu 0 --active-cores 4 \
  --field temperature-a --value 80 --apply
```

`--active-cores` 必须显式指定 1..8，表示配置表中的活动 P 核数量，`--cpu` 是执行访问的 Linux 逻辑 CPU。
字段为 `temperature-a`、`down-bins-a`、`temperature-b`、`down-bins-b`。
两项温度允许整数 0..100°C，两项降倍率允许整数 0..10；负数、小数、溢出、缺少 `--apply`
及不适用参数在打开后端前拒绝。零按原始数值处理，不赋予“自动”或“关闭”语义。

四个旧值都必须处于本编辑 profile 范围内；超出范围仍可原样读取，但整个档位不开放编辑。
不裁剪旧值，不推断无效值或阈值之间的隐含排序规则。

## 协议与约束依据

原 `Wr_octvb_vec2` 的 SET `0x25`、八项候选编号和四字节编码已由
[原指令 88 场景](validation/legacy-intel-tvb-table-emulation.json)与
[静态提取](validation/legacy-intel-tvb-table-static.json)确认：
byte 1 / 0 / 2 / 3 分别为温度 A / 降倍率 A / 温度 B / 降倍率 B。
本实现参数 1 为选定数量、参数 2 为零，只提交该档位一次，保留其余三个字节。
原版无条件重复两次、忽略固件错误及有符号截断的行为没有沿用。

Intel 官方 [Raptor Lake-S FSP 编辑器配置](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Fsp.bsf#L3005)
提供上述温度和降倍率范围；[来源记录](validation/intel-tvb-table-sources.json)固定了提交和文件哈希。
这是本实现采取的保守输入约束，不是 Intel 对运行时 mailbox、每个 SKU 或全部固件的可写保证。
支持与否仍由实际固件应答决定，目标真机调节效果尚未验收。

## 修改与结果判断

准备阶段查询 TVB 控制字、FLEX_RATIO 与指定档位。提交前在同一进程事务内重新读取，
比较 CPU 签名、完整 FLEX_RATIO、完整 TVB 控制字和该档位的完整四字节。
旧值变化时返回 `EAGAIN`；固件锁定、型号改变、虚拟机、无 MSR 或固件拒绝都不会继续设置。
若新旧值相同，验证现值后报告 `unchanged`，不会发送 SET。

SET 后等待完成，检查全部 8 位固件状态，再读取该档位和控制上下文。
只有完整四字节匹配预期、上下文未变且没有取消或超时，才报告 `verified`。
每次 mailbox 请求前后都有 100 次轮询上限，并服从共同事务截止时间。
失败或状态不明时不自动重试，也不自动回滚。

JSON 区分 `write_attempted`、`verified`、`unchanged`、阶段、固件状态和前后快照；
成功提交但未验证时仍报告失败。GUI 会清空失效结果，并说明是否尝试过设置。
选择变更时不会套用旧的异步结果；已提交操作仍针对确认时的 CPU / 档位，需重新读取原目标。

进程内串行事务不能排除其它软件竞争，不构成系统级原子操作。
完整读回也不证明其它表项、实时频率或稳定性；本功能不会主动开关 TVB 倍频裁剪。
逐物理核心表、一键 Boost、其它型号、完整范围和目标真机 / Secure Boot 验收仍未完成。

## 验证状态

本轮核心、GUI、CLI 和跨发行版验证结果将在云端任务结束后补充。
