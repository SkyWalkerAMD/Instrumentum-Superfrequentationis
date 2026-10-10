# 无桌面版 OCTool

`octool-cli` 是独立的 Linux x86_64 命令行程序，直接复用现有核心、HAL 和 `.ko` 驱动。
构建和运行均不依赖 Qt、X11、Wayland、桌面会话、polkit 或常驻服务，可从 SSH 或本地终端运行。
与 GUI 可以共存，分别安装 `octool-cli` 和 `octool`。不要同时运行多个硬件调参程序；
现有事务锁只覆盖单进程，不能协调其它进程或内核驱动对间接寄存器的操作。

此前 `octool --diagnose` 只是 GUI 二进制的无显示诊断入口，仍带 GUI 包依赖；本程序提供实际模块命令。
本版没有增加硬件支持范围，原版尚未还原的功能及真机验收缺口见[还原状态](recovery-status.md)。

## 安装与构建

选择与发行版匹配的 `octool-cli-2.0.1-1*.deb` 或 `.rpm`：

```sh
# Debian / Ubuntu，先进入对应发行版安装包目录
sudo apt install ./octool-cli-*.deb
# Rocky / EL
sudo dnf install ./octool-cli-*.rpm
```

CLI 包只依赖系统 C/C++ 运行库，不自动安装 GUI 或 DKMS。
现有 `octool-hwio-dkms` 包可单独安装，为目标内核生成 `octool_hwio.ko`。
诊断、普通 CPUID 和可读的 sysfs 信息无需驱动；MSR/PCI 等访问仍取决于权限、硬件、
内核策略及已加载的驱动。程序不自动加载模块，也不修改权限。
需要特权的操作可通过终端的 `sudo octool-cli ...` 执行，不弹图形授权窗口。

仅构建 CLI 的例子（CMake >=3.16、C/C++ 编译器；测试另外需要 Python >=3.8）：

```sh
cmake -S cli -B build/cli -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/cli --parallel 2
sudo cmake --install build/cli --prefix /usr/local
```

运行测试时启用 `BUILD_TESTING`，在 `build/cli` 目录执行 `ctest --output-on-failure`。
打包使用 `python3 port/tools/build_cli.py --format deb`（或 `rpm`），需要对应原生打包工具。
每个包使用目标发行版自身的 glibc/libstdc++，不能把较新系统的包当作 EL8 通用包。

## 开始使用

```sh
octool-cli --help
octool-cli diagnose
octool-cli inventory > inventory.json
octool-cli spd-decode --file memory.spd > spd.json
```

CPU 硬件命令必须明确指定 `--cpu`，从 `diagnose` 的 `data.allowed_cpus` 选择。
原始 MMIO/PCI 命令分别指定物理地址或 BDF，不使用 `--cpu`。
以下 `0` 仅为示例；受限容器、CPU 亲和性和多路机器未必允许 CPU 0。

```sh
octool-cli cpu --cpu 0
sudo octool-cli amd-pstates --cpu 0
sudo octool-cli intel-read --cpu 0
sudo octool-cli intel-oc-read --cpu 0 --domain core
sudo octool-cli intel-vf-read --cpu 0 --domain core
sudo octool-cli intel-vf-read --cpu 0 --domain cache --point 8
sudo octool-cli amd-smu-probe --cpu 0 --profile shimada
sudo octool-cli amd-smu-read --cpu 0 --profile shimada --message 2
octool-cli amd-topology --cpu 0
sudo octool-cli amd-curve-read --cpu 0 --ccd 0 --core 0
sudo octool-cli amd-umc-read --cpu 0 --bank 0 --refresh-slot 0 > umc.json
```

曲线的 CCD/core 是手动确认的固件索引，不由 Linux CPU 号或 CPUID 拓扑推导。
UMC bank 是地址索引，不能直接当作内存通道/DIMM 标签。输出原始编码，不假设物理单位。
曲线及 UMC 查询会写入查询邮箱或地址索引，没有修改曲线或内存设置。
CLI 的 UMC JSON 是下述统一命令格式，目前不能直接导入 GUI 的专用 UMC 快照导入器。

`intel-vf-read` 与 GUI 的 Intel Controls → V/F points 共用[同一查询实现](intel-vf-recovery.md)。
仅开放 Raptor Lake-S family 6/model B7；`--point` 取 1..15，省略时逐个查询 15 个候选点，
这不表示硬件一定支持 15 点。输出每点的 `ratio`、`offset_mv`、`raw` 和固件返回状态。
`--cpu` 是执行查询的逻辑 CPU，不是逐核 VF 目标编号；不修改 VF 或 override。
固件拒绝的点数值为 `null`，仍继续查询后续点；传输失败/超时/取消则终止查询。
`scan_completed` 仅表示全部请求得到完成状态；存在任一点错误时 `ok:false`、退出码 3。
该 JSON 可保存作记录，目前没有 GUI 文件导入或写回功能。

## 已还原的设置入口

`intel-set --cpu N --field FIELD --value VALUE --apply`：

| FIELD | VALUE |
|---|---|
| `pl1`, `pl2` | W，按实际 RAPL 单位编码 |
| `pl1-window`, `pl2-window` | 秒，按核心的窗口规则向下取可编码值 |
| `pl1-enable`, `pl2-enable`, `pl1-clamp`, `pl2-clamp` | 0 或 1 |
| `hwp-min`, `hwp-max`, `hwp-desired`, `hwp-epp` | 整数 0..255，额外检查实际硬件能力和字段关系 |
| `hwp-window-us` | 整数 0..1270000000 微秒，0 为自动；需要活动窗口能力和 desired=0，向下编码 |

`intel-oc-set --cpu N --domain core|cache --field offset-mv|max-ratio --value VALUE --apply`：
沿用 Raptor Lake-S family 6/model B7 配置的型号/锁/旧值检查，保留其它字段并回读。
offset 的可编码范围是 -1000..999.0234375 mV，最大 OC 倍频是整数 1..85；
这些是编码范围，不是某颗 CPU 的稳定参数建议。W790/W890 的 server 电压域尚不支持。
RAPL/HWP 与 Intel OC 均检查完整寄存器回读；`verified` 只表示回读一致，不代表稳定性验证。
RAPL/HWP 的 `readback_value` 是实际编码后的配置值，相同配置不重复写入。
HWP 配置属于硬件提示，不等于实测频率或功耗。

原始寄存器命令 `register-read` / `register-write` 已与 GUI 对齐，支持 MSR、MMIO、PCI；
完整参数、宽度和提交语义见[控制与寄存器说明](control-register-recovery.md)。

目标电压与模式使用同一命令的 `--field target-mv --value MILLIVOLTS --mode adaptive|override --apply`，
同时保留 `--cpu N --domain core|cache`。目标必须是整数 1..2000 mV，模式必须明确选择，
零值自动/默认语义未开放。该范围不是安全工作电压范围。
只修改选中的域，保留 offset 与倍率；Adaptive 配置 turbo 目标，Override 配置固定目标。
`before` / `after` 含取整后的 `target_mv` 和 `target_mode`（失败为 `null`）。
详见[目标电压与模式还原](intel-voltage-recovery.md)。

AMD 原始固件入口：

```text
octool-cli amd-smu-send --cpu N --profile shimada|phoenix|gpt \
  --message MESSAGE --arg0 VALUE [--arg1 VALUE ... --arg5 VALUE] --apply
```

只接受核心已有白名单，参数保持原始 32 位编码。`--arg0` 必填，其余默认 0。
`amd-smu-read` 只接受查询消息 1/2，参数全为 0。
Shimada 的已恢复调参消息在当前核心白名单内；Phoenix/GPT 仅开放查询。
固件返回成功仅表示命令获接收，`hardware_effect_measured:false`。
失败后不自动重试或回滚，结果中的 `write_attempted` / `message_attempted` 用于判断是否曾提交。
尚不提供 PStates 设置、曲线设置、完整 PBO 高层参数、Intel server VF/fabric 或板级专用写入。

PCI 目标选项 `--bus`/`--device`/`--function` 默认全为 0，域固定为 0；曲线查询固定 0000:00:00.0。
目标索引等无符号整数使用十进制或 `0x` 前缀十六进制；设置的 `--value` 使用十进制，
offset 等有符号字段可接受负数。禁止截断、重复/未知选项；目标电压和倍率须为整数值。
设置命令缺少 `--apply` 会在打开硬件前返回错误。不会通过交互提示阻塞批处理。

## 输出与自动化

除帮助和版本外，stdout 为一行 JSON，`--json` 可显式指定但不是必需。
统一格式为 `schema_version`, `command`, `ok`, `error`（负 errno）, `error_message`, `data`。
原始寄存器用十六进制字符串，避免 64 位数值被 JSON 客户端丢失精度。
失败字段使用 `null`；PStates/Intel/清单读取可能保留部分有效结果，必须同时检查顶层和逐项错误。
可读 SPD 不存在时列表为空；SPD CRC 未验证和验证失败会明确区分。

退出码：`0` 成功，`2` 命令或参数不合法，`3` 操作失败或部分读取失败。
主板属性被内核隐藏/拒绝访问时 `inventory` 可返回 3 并保留成功行。
诊断不会测试寄存器访问；`register_access_tested:false` 不能作为驱动可用性证明。
硬件命令接受 `--timeout-ms 1..120000`，默认 10000；超时为协作检查，不能强制中断阻塞的内核调用。

可用 shell 重复调用完成采样，或由任务调度器保存 JSON。没有自动应用启动参数、
远程监听端口或自带守护进程。硬件型号和实机验证限制与 GUI 相同。

## 验证范围

专用 `headless CLI` 工作流对 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、Rocky 8/9/10
分别构建、执行核心与命令行测试、打包，并在干净容器安装/使用/卸载/重装。
容器不提供显示服务或宿主寄存器设备，运行阶段不装 Qt、编译器或 DKMS。
测试包含模拟设备的实际核心调用、设置保护、失败输出、JSON 精度、稀疏 CPU 亲和性、
SPD CRC 和 sysfs 清单。硬件参数测试使用模拟设备；不能替代四台目标机器的真机验收。
实际通过状态以对应提交的 CI 记录和本地产物清单为准。
