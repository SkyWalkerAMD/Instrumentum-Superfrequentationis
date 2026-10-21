# REA 后续：Fmax 与 core01 rails 设置入口

最新实现见 [Fmax、rails 与 PM 布局诊断](amd-pbo-fmax-layout.md)：GUI/CLI 已开放
Shimada Fmax 能力读取与 `70h` 命令提交，明确报告 `accepted` 与 `verified=false`。
未提供当前 Fmax 回读；rails 只开放编码预览。512 个熔丝输入、两个访问故障位置和
1025 个 rails 输入的原指令实验已完成。下文未开放 Fmax 的表述是历史记录。

2026-10-21 后续：新的 [11 函数 REA 分析与完整事务回放](amd-pm-percore-layout-research.md)
确认 Shimada Fmax 走 `70h`，并揭示名为 `MP1_C2PMSG` 的函数在该分支实际访问 RSMU。
本轮已独立完成 [PBO Scalar 读取与设置](amd-pbo-scalar.md)，以下 Fmax/rails 写入仍未开放。

补查原始调用指令后，`tr5_get_both` 在 `6fed3fh..6fed4fh` 将保存的 PM binary32 转为
binary64、乘以精确常量 1000、向零截断成整数并存于对象 `+9ch`。
该值在 `7021e1h..7021efh` 作为 ESI 参数交给 `set_fmax`；析构也在值非零时传回同一保存值。
这补上了 REA 伪代码丢失的参数，但仍只是原版换算和恢复路径，不是独立的实时 Fmax 查询合同。
两个 rails 按钮在调用点分别把解析出的整数放入 ESI/EDX；原版界面还带有提前启用 OC 的行为。
指令、函数 SHA256 和常量见[调用证据](validation/amd-pbo-caller-arguments.json)。

以下保留先前的 setter 静态记录。

2026-10-21：同一批 [REA 任务](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39435187923)
还返回 `AMD_PBO::set_fmax` 和 `set_core01_rails`。
伪代码丢失了部分混合调用约定参数，以下结论以
[完整函数的原指令记录](validation/amd-pbo-setters-static.json)为依据。
这是静态研究；没有新增这两个高层写入入口，没有执行硬件访问。

| 函数 | 原指令行为 | 尚不能直接用于新实现的部分 |
|---|---|---|
| `set_fmax`，`0x2397b0`，471 字节 | B8/BC、BDF 0；先向 `0x50200` 写 1；对象 `+11` 为零时选 `6D`，非零时选 `70`；输入原字写入 argument 两次；清 response 两次；提交后解锁，不读取完成状态 | 隐含写入的条件、平台初始化、参数范围与可靠回读 |
| `set_core01_rails`，`0x239990`，552 字节 | F8/FC、BDF 0；两输入均零时直接返回；只有一个为零时复制另一值；打包后连续写 `61`、`5D`；向 response 主动写 0 和 1；最后清 argument；没有等待、读回或互斥调用 | 双字段实际含义、物理单位、合法范围、连续命令与自动 OC 的关系 |

在此前固定的 Shimada 非 mobile/GPT、PCI 153a 构造分支，对象 `+11` 为 1；因此 Fmax 的该分支选择 `70`，
与固定 ZenStates Shimada 设置表的 boost-limit 命令号相符。
但它仍包含 `50200=1`，不能据此把完整原函数当作已验证的单次设置协议。

rails 每个字段的算术是：先按原 32 位运算减 `245`，有符号除以 `5`（向零截断），
再仅保留低 9 位；第二字段左移 17 位后合并。它没有范围校验，超范围输入会截断/回绕。
公式只能说明原编码过程，不能单凭函数名认定这是可用的逐核电压接口。
固定外部 Shimada 表中的 `61` 和 `5D` 分别命名为 OC VID 与 Enable OC；
原函数没有分别等待它们完成，也未保存供回滚使用的旧状态。

这两条路径与本轮[曲线设置](amd-curve-settings.md)分开：曲线只发送已核对的 `06`，
以 `A3` 比较旧值和回读，不包含 `50200=1`、rails 打包或隐含 OC enable。
后续需继续核对调用界面的输入语义、独立命令合同和返回途径，才能接入 GUI/CLI 的高层控件。
