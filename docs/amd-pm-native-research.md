# REA：PM 传输、逐核/CCD 校准与 VID 换算分歧

实际运行 [REA 任务 39568542099](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39568542099)，
研究提交 `4347fd2885b457f8e9c386e213e4763e869ae760`。REA 6.7.0 / Ghidra 12.1.4 成功查询十个函数，
导出证据并关闭会话。原 ELF SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
逐个核对 ELF 符号、完整原始字节和 Capstone 反汇编；Ghidra 地址比原文件偏移多 `100000h`。
[机器记录](validation/amd-pm-native-rea.json)保留任务、函数、固定来源及证据哈希。

## PM 表传输

分析覆盖 `AMD_PM_LOG` 的 32/64 位地址读取、`start_monitor`、`get_layout`，以及父类地址/启动函数。
原版混合 ABI 的 PCI 参数在伪代码中不可靠，因此寄存器与访问次序同时按原指令核对。

新增回放使用显式合成的 PM 对象状态，执行三个完整原函数：36 个函数/标志/返回地址组合，
另逐个注入默认路径的 21 个 PCI 访问故障。它不重放构造选择，不把合成对象当作真机初始化结果。

- `start_monitor` 主动把 response 写为 1，不读取完成状态，最后返回 0。
- 地址函数同样没有读取 response；不能把拼出的地址或函数返回视作固件接受命令。
- 32 位函数在最后一次读取 data port 前已释放锁；64 位函数在持锁期间读取高低两字后拼接。
- 本实验的 PCI 写入失败返回 0，读取失败返回合成 `FFFFFFFFh`；原函数仍完成后续访问。
  这仅说明它们如何处理替身边界，不代表真实驱动、固件的错误合同。

[新原生采集实现](amd-pm-native.md)使用固定公开配置独立建立 `05h/04h/03h` 事务，
要求真实返回长度与精确版本表一致，检查完成状态和整个地址范围，并在读取后复查元数据。
没有复制原版的主动完成标志、固定延时后假定成功或隐含初始化。
这提供了受约束的原生样本采集途径，**没有消除逐核/CCD 偏移、长度和字段别名冲突**。

## Rails 与通用 VID 帮助函数

另外分析 `cal_vid_from_raw`、`cal_raw_from_vid`、`TEST_SMC_MSG_ForceCpuVid` 和 `set_core01_rails`。
两项通用换算函数在 16 个合成平台标志组合、11 个输入值下执行双向换算，
再完整覆盖两条关键分支各 512 个原始输入，共 1376 条回放。

原指令选择 SVI3 算术的条件是：

```
(is_am5_es() || GLOBAL_IS_GRANITE) && !GLOBAL_IS_SHIMADA && !GLOBAL_IS_GPT
```

因此，`GLOBAL_IS_SHIMADA` 为真时，这两个函数使用 `1550 - 6.25 × VID` 的 SVI2 算术，
反向换算同样使用 6.25 步长。它们不会按名称自动选到此前 rails 函数的 `245 + 5 × VID` 公式。
原 rails 指令夹具独立复核了 700/1250 mV 对应 `91 | (201 << 17)` 的双字段编码。
这证实原程序内部存在不同编码路径；没有证实它们的调用者、物理轨或固件命令可以互换。

公开 `SetOverclockCpuVid` 的单字接口、通用帮助函数、P-state VID 描述和原双字段 rails
仍不能拼成已验证的电压设置合同。新版本保持 rails 离线预览，不开放猜测的写入协议。

## 逐核/CCD：补跑原始校准路径

此前逐核/CCD 回放预置了“校准完成”。本次继续使用
[前次 REA 的 `get_pm_patch` 证据](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39425573426)，
在原构造与公共监控初始化后显式重置校准标志，重新执行完整原校准指令。
针对 `730204h / 2 CCD`、`731004h / 16 CCD` 两个合成组合，各运行六种 MMIO 输入，共 12 次实验。
这些组合用于检查冲突，不代表经固件确认的版本/拓扑对应关系。

- 有效输入两次读取后完成；第一对无效、第二对有效时，校准偏移前移 4 字节、修正量为 8。
- 第一值始终小于下限时，会绕过 30 次计数检查；在第 129 次读取前人为停止，校准仍未完成。
  因此该计数不是全局扫描上限；没有把观察上限当作原函数自行超时。
- 第二值为零或过高时，122 次读取后结束；第一值为 quiet NaN 时，两次读取后也设置完成标志。
- 所有完成路径只改动对象的 `CD8h/CDCh/CE0h`，继续运行原 mode 4 得到的逐核/CCD 读取组完全不变。
  `730204h / 2 CCD` 仍有 22 字超出公开长度，`731004h / 16 CCD` 仍有 176 字重叠。

这排除了上述已完成校准路径能直接修正布局的假设；没有证明其它对象状态或真实固件也相同。
原生采集不会复制这段可能越过扫描计数检查的物理内存搜索。

## 2026-10-26：内存读取封装复核

沿用[已归档的 REA 证据](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39435187923)，
进一步对照 `RW_MMIO::Rd_MMIO`、通用读取、内核读取及相关构造/查询函数的完整原始指令。
[逐指令记录](validation/amd-pm-address-review.json)固定原 ELF、函数字节哈希和 REA 证据 ID。

REA 的混合 ABI 伪代码没有显式显示对象基址。实际指令从对象 `+18h` 取 64 位基址，
加上 RSI 中的字节偏移，再通过 RCX 传给 `Read_MMIO`；没有乘以 4、数组步长修正或范围检查。
返回前将 EAX 零扩展，只保留读取结果的低 32 位。内核路径将同一个绝对地址放入 96 字节的
`0Ch` 读取请求，忽略 write 返回值并无限等待完整完成字等于 1。

这排除了“封装器隐含换算步长”这一解释；仍不能确认 PM 对象的基址、实际固件布局和物理映射。
本次复用已有 REA 输出，未宣称启动新的 REA 任务；既有 NGU 回放已覆盖相同通用读取封装，
没有将此次静态复核另算为新增云端测试。

## 重放与范围

```sh
python analysis/tools/legacy-amd-pm-native.py --output build/pm-native-original-replay.json
python -m unittest discover -s analysis/tests -p test_amd_pm_native.py -v
python analysis/tools/legacy-amd-pm-calibration.py --output build/pm-native-calibration.json
python -m unittest discover -s analysis/tests -p test_amd_pm_calibration.py -v
```

需要已固定的 Python 分析依赖。共 1433 个传输/VID 实验、12 个校准实验、三项本机分析回归通过。
原始字节夹具及 SHA256 随源码提供，原程序不会在宿主执行；PCI、固件、锁和平台检测均为替身。
这三项新分析回归同时纳入最终生产提交的云端分析测试。
仍需真实固件版本、返回长度和独立物理标签样本，才能完成逐核/CCD 遥测与 rails 验收。
