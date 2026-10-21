# AMD PBO Scalar 读取与设置

2026-10-21：GUI 的 AMD Tuning 页面与 CLI 共用 `gui/core/amd_pbo.*`，
支持 **Shimada、AMD family 1Ah/model 08h、PCI 1022:153a、domain/BDF 0** 的 PBO Scalar。
这是该目标的共享固件设置，不是逐核或逐 CCD Scalar；其它平台返回不支持。
[整批验证](amd-pbo-scalar-validation.md)单独记录编译、测试与安装包。

| 操作 | 合同 |
|---|---|
| 读取 | RSMU `6D`，argument 0；返回 argument 0 是 binary32 浮点倍率 |
| 设置 | RSMU `5B`；整数倍率 `1..10` 编码为 `倍率 × 100`，例如 5 倍是整数 500 |
| 旧值检查 | 调用者提供此前读取的原始 32 位字；重新查询后逐位比较，不符则返回冲突 |
| 成功判定 | 设置响应为 1 后再次查询；只有回读精确等于目标 binary32 才报告 verified |
| 无需更改 | 新旧倍率相同则返回 unchanged，不发送设置命令 |

读取保留原始字及有限的 `0..10` 倍率，包括小数和非负 subnormal。
不把 0 解释成“自动”或“关闭”，也不允许将 0 作为设置输入。
负数、负零、NaN、无穷和超范围值返回错误，同时保留已取得的原始字供诊断。

CLI 使用示例：

```sh
sudo octool-cli amd-pbo-scalar-read --cpu 0
# 仅当刚才的 raw 确实为 0x3f800000（1.0）时使用下面的 expected 值
sudo octool-cli amd-pbo-scalar-set --cpu 0 --scalar 5 --expect-raw 0x3f800000 --apply
```

语法、倍率范围、expected 编码和 `--apply` 均在打开后端前检查。
输出分别给出 compare/write/verify/complete 阶段、旧值、新值、设置参数、
write_attempted、accepted、verified 与 unchanged。`hardware_effect_measured` 始终为 false；
固件回读一致不代表性能、温度或稳定性已经验收。

GUI 先选择目标 CPU 并点击 **Read scalar**，再填写 `1..10` 整数、点击 **Apply scalar**。
确认对话框展示目标、旧值和新倍率。更改 CPU、原始命令或其它共用目标条件后，
之前的读取失效；确认期间改变目标也会中止。异步旧结果不会覆盖新目标。
提交后清除缓存，下一次设置需重新读取。已接受但回读失败会明确显示为未验证。

传输使用 B8/BC 间接访问，message/response/argument 地址为
`3b10924 / 3b10970 / 3b10a40`。Scalar 命令显式写入六个 argument，后五个清零。
每次事务使用现有服务互斥与期限；提交前重新验证 CPU、型号与 PCI 身份，
每次访问检查取消和期限，响应轮询有界，首个错误即停止。
设置不自动重试、回滚、启用 OC/PBO、切换 MP1，也不执行原版隐含的 `50200=1`。
这个锁只约束本服务的事务；其它程序或固件仍可能并发改变设置。

协议依据固定在 ZenStates-Core 提交 `869b7e82572337e5a673e92230edc92bddf0e44b`：

- [Shimada 命令与地址表](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)。
- [查询编码](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/GetPBOScalar.cs)、[设置编码](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetPBOScalar.cs)、[范围常量](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Common/Constants.cs)。
- [六参数邮箱传输](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/SMU.cs)。

新实现没有照搬来源中的跨邮箱 fallback 或其它平台的自动 enable。
REA 对原版 Fmax/rails/邮箱的分析用于区分冲突的命令含义和隐含操作；
Scalar 的读写编码来自上述固定公开实现，并非宣称 REA 从原版恢复了 Scalar 的完整合同。
原始文件校验、调用证据及限制见[研究记录](amd-pm-percore-layout-research.md)。

新增覆盖包括核心 10 个场景组、CLI 1 个场景组和 23 份进程级 JSON 报告、GUI 5 项回归。
它们测试原始浮点数据、六参数清零、逐访问故障、旧值冲突、无变化、响应拒绝、
取消/期限、身份变化、回读不符及异步界面状态；全部使用合成后端。
真实 Shimada 硬件、固件差异和实际调参效果仍需验收。
