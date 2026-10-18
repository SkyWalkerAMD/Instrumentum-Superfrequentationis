# HWP 包级请求设置

2026-10-18：GUI / CLI 共用核心增加五项 `IA32_HWP_REQUEST_PKG` 设置，
将此前只读的包级最小性能、最大性能、目标性能、能效偏好和活动窗口接入编辑。
这是对已恢复 HWP 模块的文档化扩展；没有声称原 ELF 已实现这五项完整事务。

## 接口与作用范围

GUI 在 `Intel Controls → Power / performance` 中读取后选择 `HWP package ...`。
确认框明确说明：设置作用于所选逻辑 CPU 所在物理封装的共享请求，可能影响使用包级控制的其它逻辑 CPU。
CLI 使用已有 `intel-set --cpu N --field FIELD --value VALUE --apply`，新增字段如下：

| FIELD | 单位与约束 |
|---|---|
| `hwp-package-min` | 性能索引；在所选 CPU 的最低/最高能力范围内 |
| `hwp-package-max` | 性能索引；在所选 CPU 的最低/最高能力范围内 |
| `hwp-package-desired` | 性能索引；0 表示自主选择，其余值须在包级 min/max 范围内 |
| `hwp-package-epp` | 0–255 的整数 |
| `hwp-package-window-us` | 0–1270000000 微秒的整数；0 由硬件选择，其余向下编码；包级 desired 必须为 0 |

例如先执行 `octool-cli intel-read --cpu 0` 查询实际可用字段；命令中的 CPU 必须来自本机允许的 CPU 集合。
CLI 报告包含 `shared_package_hint`、`selection_flags_changed=false` 和 `capability_reference_cpu`。
性能索引不是 MHz，包级请求不是当前实际频率；能力范围参考所选 CPU，并未逐核认证整个封装的能力。
选择了本地控制的 CPU 不一定使用包级值；有效请求还受 flexible-valid 位、极性和 PECI 覆盖影响。

## 可复核依据

[Intel SDM Vol. 3B, 253669-085US](https://cdrdv2-public.intel.com/835755/253669-sdm-vol-3b.pdf)
§16.4.3、§16.4.4.1–16.4.4.4 定义能力位、包级作用范围、五个字段的读写属性、活动窗口编码和 PECI 优先级。
本项目固定副本 SHA256 为
`b56b034c34a1efdb672491a9f5a8eb575b5e0a96f0380fcef354f0682798a353`，
物理 PDF 第 10–15 页。图 16-7 对应地址 `772h`；FSP 结构偏移不参与本实现。
[此前固定来源记录](validation/intel-hwp-requests-sources.json)保留文件身份和核对范围。

## 事务和失败处理

- 要求 Intel、CPUID HWP 与 package-request 能力、HWP 已启用；EPP/window 另按能力门控。
- 包级源值、能力、所选 CPU 的本地请求、PECI 和极性必须可读。对应 PECI override 生效时禁止 min/max/EPP 编辑。
- 写前重新读取能力和完整上下文；完整旧包级寄存器比较后，只改变所选字段。保留其它位和所有选择标志。
- min/max 必须有序，非零 desired 必须落在其间；相同编码直接验证为未写入。
- 最多一次 `772h` 设置写入。之后独立读取全部 64 位并复核 CPU 身份、能力、本地请求、PECI 和极性。
- 失败、并发改值或取消不报告成功，不自动重试或回滚。GUI 清除旧快照，CLI 不输出有效的回读解码值。

不写 `770h`、`773h`、`774h`、`775h`、`776h` 或 `777h`，不自动启用 HWP、切换包级控制或清除历史。
事务不锁住操作系统及其他硬件管理程序；快照和回读不是整个系统的原子策略或性能效果证明。

## 验证范围

合成核心测试覆盖五字段、四种本地选择/极性组合、无变化、边界、八种 PECI 覆盖组合、
写前/写后上下文变化、每个必要 MSR/CPUID 的读取失败、丢弃写入、写失败、取消及最终 RMW 竞争。
GUI 测试覆盖共享范围提示、取消、切换 CPU、五项设置、无变化、失败清空和截图；CLI 测试检查 JSON 的
原始回读必须来自 `772h`，并验证参数错误在打开设备之前拒绝。
这些验证不涉及本机物理寄存器写入；真机效果、稳定性、其它剩余功能和 Secure Boot 验收仍待完成。
