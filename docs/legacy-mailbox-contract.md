# 原版 MMIO 邮箱：初始化、完成条件与失败分支

日期：2026-10-08。接续[启动权限分析](legacy-el-privilege-analysis.md)。本轮继续读取原字节，
没有改原 ELF、GUI 调用点、96 字节请求、HAL 或模块行为，也没有访问物理寄存器。
**发现一项现存兼容缺口：新模块的错误完成字不满足原版的等待条件。**
此前“MMIO 完全兼容”的表述必须收窄为已核对的命令、布局和成功应答路径；失败路径尚未兼容。

## 输入与可复现证据

- 原 GUI SHA-256：`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
- [静态证据](validation/legacy-mailbox-analysis.json)：13 个 GUI 函数、4 个命名对象，
  3 份旧模块各 17 个函数；每个函数有字节哈希、逐条指令，模块另有重定位和 DWARF 字段偏移。
- [Windows 原指令模拟](validation/legacy-mailbox-emulation-windows.json)：8 个包装函数 × 7 种合成应答，
  共 56 项。成功表示观察符合预期，包括预期不能退出的分支，**不是原版兼容性全通过**。

| 模块 | 原样本 vermagic 内核 | SHA-256 |
|---|---|---|
| peter_kernel.ko | 6.8.0-94-generic | `4538764d874c39641106f8e3b3d97f0e6a6b02f1d27ba538e79dea1472b4a026` |
| peter_kernel_new.ko | 6.17.0-14-generic | `405b5a59b1a80a21e246f9cae90c112a232574978097a0aeaaafb8f71581322f` |
| peter_kernel_old.ko | 6.2.0-20-generic | `955be8f274f831d77ae259cad45a19ad663f3c62de93c0b565c3d35cc2c8d975` |

模块从作者项目父目录的 `octool-linux.zip` 直接读取；原来的 E 盘路径在此次环境已不存在。
不运行旧安装脚本、不装载这些 `.ko`，也不将原 `.ko` 加入 Git 或公开 artifact。
ET_REL 指令地址是**节内偏移**，例如 `.text+0xb05`；重定位操作数未应用，不能当成已加载内核地址。
新增真实编译器 fixture 用两个不同节中同为 0 的函数偏移，核查重定位不会串到另一个函数。

```sh
python3 -m pip install --target build/reference-tools -r analysis/tools/requirements-emulation.txt
export PYTHONPATH="$PWD/build/reference-tools"
python3 analysis/tools/audit-legacy-mailbox.py build/input-audit/octool \
  --module-zip ../octool-linux.zip --output build/mailbox-static.json
python3 analysis/tools/emulate-legacy-mailbox.py build/input-audit/octool \
  --output build/mailbox-emulation.json
python3 -m unittest discover -s analysis/tests -v
```

模拟器使用 [Unicorn 官方接口](https://www.unicorn-engine.org/docs/tutorial.html)，固定版本 2.1.4。
每例最多执行 512 条指令；只放入被选函数的原字节，唯一外部调用 `write@plt` 按 GOT 重定位确认并模拟。
所有 fd、地址、令牌和应答都是合成值，不打开 `/dev/mydev`、不调用真实 write、不启动 GUI。
不模拟模块、页表、内核权限或并发调度。因此不能替代 EL 真机、模块加载和 live parity。

## 1. 完成条件是完整 64 位值等于 1

| 原函数 | 入口 | opcode | 等待中的比较指令 |
|---|---|---|---|
| Read_MMIO_kernel | 0x36e110 | 0x0c，32 位读 | 0x36e153 |
| Read_MMIO64_kernel | 0x36e160 | 0x0a，64 位读 | 0x36e1a3 |
| Read_MMIO8_kernel | 0x36e1b0 | 0x10，8 位读 | 0x36e1f3 |
| Read_MMIO16_kernel | 0x36e200 | 0x0e，16 位读 | 0x36e243 |
| Write_MMIO_kernel | 0x36e250 | 0x0d，32 位写 | 0x36e29b |
| Write_MMIO64_kernel | 0x36e2b0 | 0x0b，64 位写 | 0x36e2fb |
| Write_MMIO8_kernel | 0x36e310 | 0x11，8 位写 | 0x36e363 |
| Write_MMIO16_kernel | 0x36e370 | 0x0f，16 位写 | 0x36e3c3 |

八个函数都是 `mov rax,[mailbox]; cmp rax,1; jne 等待处`。
每次先把首字清零，再发一次 96 字节 write；不检查 write 返回值，没有超时、睡眠或错误出口。
读函数随后从偏移 8 取完整 qword；8/16/32 位截断在更外层发生。
请求使用全局 `user_request`：改 cmd、data0，写操作再改 data1，其余字不清零。
8/16 位写在包装函数内零扩展，32 位写包装仍储存完整的 data1，由驱动执行写宽度限制。

每个原函数均得到以下模拟结果，完整请求字节也逐例核验：

| 合成条件 | write 返回值 | 完成字 | 原函数行为 |
|---|---|---|---|
| 成功 | 96 | 1 | 返回；读值与合成应答相同 |
| 延迟完成 | 96 | 先 0，64 条后续指令后变 1 | 返回 |
| 永不完成 | 96 | 0 | 到 512 条上限仍在等待 |
| 模拟新模块 ENOMEM 应答 | 96 | 0xfffffff400000001 | 到上限仍在等待 |
| write 失败 | -1 | 0 | 到上限仍在等待 |
| 短写 | 12 | 0 | 到上限仍在等待 |
| write 失败但邮箱为 1 | -1 | 1 | 仍返回，证明没有使用 write 的错误结果 |

`port/kmod/octool_hwio.c` 当前将失败编码为 `((u64)(u32)rc << 32) | 1`。
`ioremap` 失败可以走到 MMIO 的 `-ENOMEM` 应答；这只是可达源码分支，本轮没有在真机制造映射失败。
原版要求完整值为 1，所以这种应答会卡住。这个问题与 glibc、EL 大版本或 Qt 平台插件无关。
现有 HAL 在“非零”时退出再解析高位 errno，其 loopback/transport 能通过，不能替代原调用者条件。

采集器 `octool_capture.c` 的 `completed = mailbox[0] != 0` 也比原版宽松。
遇到上述错误应答可能记录 completed=1，而原版仍在等；这是源码核查结果，未运行真实采集器复现。
对拍工具的 captured-completed 只表示采集器判断，不能单独证明原 GUI 已完成。
本轮保留生产行为，明确记录缺口；不要用清掉 errno、返回零值的办法把错误伪装成有效硬件读数。

后续兼容修复至少要同时满足：旧调用者完整 done=1；新 HAL 可区分错误；不更改旧 96 字节请求；
未改旧调用者收到错误时不能被宣称已正确处理。若增加可选的扩展错误通道，必须能力协商并验证旧模块降级，
不能未经验证搬动结果槽或把全体非零完成字视为成功。本轮没有实施新协议或确定它的发布方案。

## 2. 两次 mmap 确实映射同一页

GUI 在 `0x36e045`、`0x36e074` 对同一 fd 做两次 offset0 / MAP_SHARED 映射。
第一地址保存在 `kernel_address`；第二次成功地址被丢弃，失败则清 MY_KMOD_LOADED。
失败路径没有关闭已开 fd 或释放第一映射，因此重试还可能积累资源；尚未动态测量。

三份旧模块 mmap 都把 `file.private_data` 复制到 `vma.vm_private_data`，fault 再取其中同一页。
没有“第 1 次请求页、第 2 次响应页”的分配分支。DWARF 给出的偏移与指令吻合：

| 样本 | file.private_data | vma.vm_ops | vma.vm_private_data |
|---|---|---|---|
| peter_kernel.ko | 0xc8 | 0x78 | 0x90 |
| peter_kernel_new.ko | 0x18 | 0x48 | 0x60 |
| peter_kernel_old.ko | 0xc8 | 0x60 | 0x80 |

这是对各个已编译样本的解释，不是新增硬编码内核偏移。新模块按同一 open ctx 的 mbox PFN 映射，
两次映射也会别名到同一页。所以采集器保存最后一次、GUI 使用第一次，在这几个实现里不构成页选错。
这不证明采集器对 munmap、fd 复用和并发都健壮；这些边界仍需单独测试。

## 3. pagemap 的计算结果没有进入请求

初始化函数对第一映射调用三次 `virt_to_phys_user`，输出地址始终是局部栈位置 `[rsp+8]`。
随后真正写入 `user_request+8` 的是 `read(fd, buffer,4096)` 缓冲区的前 8 字节。
这三次调用的返回值和所得物理地址在已解码初始化函数内都没有被读取，也没有传给模块。
因此不能把 pagemap PFN 受限当成本握手必然失败的依据。
[Linux pagemap 文档](https://kernel.org/doc/html/v5.15/admin-guide/mm/pagemap.html)
说明无 CAP_SYS_ADMIN 时 PFN 可能被置零；该限制并不改变这里观察到的数据流。

旧 read 把用户索引放到 mailbox[0] 再复制出去；每个 open 的首次读取还建立请求索引到响应页的关系。
GUI 读取一次 4096 字节，HAL 读取一次 8 字节，都包含这个首字。
GUI 不核验读取长度，失败时可能把未初始化栈内容当令牌；新 HAL 短读则回退令牌 0。
新模块当前忽略 MMIO 的 user_id，旧模块按其索引分派；不能由新模块“能接受 0”推断旧模块也正确接入。

## 4. 旧模块之间还有发布顺序差异

`peter_kernel.ko` 与 `_new.ko` 的 mem_read32 在 `.text+0xb05` 先写 done=1，
再于 `+0xb0c` 写 result。`_old.ko` 顺序相反：`+0xb07` 写 result，`+0xb0b` 写 done。
parse_user_request 的 MMIO 分支创建并唤醒 kthread，write 路径没有等待该线程退出。
前两者可能让另一 CPU 先观察到 done 再读到旧 result；这是静态指令顺序发现的竞态风险，
没有本轮真机重现、概率测量，也不将它推广为所有操作码都同样。

新模块先写结果、smp_wmb 后发布完成字，成功路径保留这项改进。
对拍旧值偶发跳变不能立即归因到新模块或电压/温度单位；需记录准确旧模块哈希，重复采集并检查稳定项。
不把复制旧模块潜在竞态当作“逐字节兼容”的要求。

另一个限制：`read_mmio_peter_lib` 只把长度 1、2、4 分流，其余长度一律 malloc(8) 并读一个 64 位值。
`mem_chunk2` 的 open/fstat/mmap 等失败分支会调用它。但全体声明函数中的直接 call/jmp 检索，
只找到 Read_MMIO8/16/32/64 四个调用点，显式长度分别为 1/2/4/8，均在支持范围内：
`0x36ee54 / 0x36ef44 / 0x36f034 / 0x36f124`。这些上下文也在静态 JSON 中。
因此本轮没有证据把该长度限制认定为已有 SMBIOS 或平台页面的崩溃原因。
仅在未来复用为任意长度读取时需重做实现；本次直接调用清单不覆盖间接调用/指针别名。

## EL8–EL10 的实际含义与后续门禁

私有运行库解决的装载层仍有效；本轮发现的是装载之后的调用者/驱动契约问题。
云端旧 GUI 仍只到 Not supported 对话框，没有走到本模块路径；上述 emulation 不冒充那段启动成功。
十目标重构版编译/安装/窗口结果也不能反证这些未注入的失败分支。

新增手动 `legacy mailbox investigation` 工作流，只取已授权的草稿 ELF，在无网络、非 root、
零 capabilities 的独立容器执行模拟器，公开产物只有 JSON。它不启动原 ELF，也不加载旧模块。
调用方法：`gh workflow run legacy-mailbox.yml --ref main -f asset_id=600130240`。
公开范围已由作者在 2026-10-08 明确确认：“①允许公开反汇编证据及模拟结果，继续云端测试”。
此前自动审批曾因授权范围不明确拦截提交/推送；在作者补充授权后，继续公开本轮派生指令证据、
研究工具和模拟结果。原 ELF 仍只取自未发布草稿，原 `.ko` 仍不进入公开产物。
云端模拟和本轮完整回归待以下实际执行记录补齐，不能把已写好的工作流当成通过。
本地 56 组模拟完成；3 项需 Linux 编译器的 fixture 在 Windows 明确跳过，未冒充执行通过。

修复前不要把现有包标记成原版 GUI 的完整替换驱动。真机必须继续验证冷启动握手、一次真实成功应答、
GUI 返回及 stable live parity；失败分支用无硬件的可控后端注入，不通过任意坏物理地址制造内核故障。
所有这类测试都要区分“旧指令返回”“采集器 completed”“新 HAL errno”三个实际观察量。
