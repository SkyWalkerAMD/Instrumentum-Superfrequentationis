# Intel 目标电压与模式：GUI / CLI 同步验证

生产提交 `452f59457c8f5c9ef0b586d22a80612a1ecb7ff5`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/intel-voltage-ci-452f594.json)记录工作流、逐系统测试、包与源码 SHA256 和限制。
[功能与研究](intel-voltage-recovery.md)说明字段范围、原版副作用和新事务规则。

| 验证层 | 结果 |
|---|---|
| 共同核心 | Ubuntu 22.04/24.04、Windows 2022、macOS 14：4/4，各 7 CTest、67 场景组 |
| Linux CLI | 12/12；十发行版各 9 CTest、7 组命令场景、49 个打开设备前拒绝、91 份 JSON |
| GUI / 驱动 | 23/23；十发行版及 EL8 基线各 38 Qt 测试；11 套内核 VM |
| 分析回归 | 云端 71 项全部通过；新增原目标电压指令实验 82 个场景 |
| 原目录本地检查 | 48 项，36 通过、12 因缺少 C++/Linux 工具链跳过；源码归档字节回读一致 |

新功能测试覆盖两模式两域、全部输入编码、取消、重复请求、锁、旧值变化、固件拒绝、
每个传输失败位置、32 位回读逐位损坏，以及取消/超时发生在提交前后的状态。
GUI 输入默认留空，必须选择模式；CLI 参数错误在打开设备前拒绝。
结果同时核对取整后的目标、模式、原有偏移与倍率，以及未选域不变。

十发行版 CLI 在无显示服务、无 Qt、无构建工具的新环境完成安装、运行、普通用户诊断和卸载重装。
GUI 下载归档 230 张截图，检查 Ubuntu 24.04 和 Rocky 8 打包程序的电压页布局、单位与模式选择。
测试中的固件响应均为合成数据，没有测量真实电压或验证调参稳定性。

## 交付与源码同步

`dist/gui-cli-voltage-452f594/` 含 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、
Rocky 8/9/10 的十个目录。每个目录包含 GUI、CLI 与可选 DKMS 各一个 DEB/RPM。
顶层提供对应源码及 SHA256SUMS。研究版版本仍为 2.0.1，使用提交目录区分构建。

- GUI 包：10,298,861–13,973,892 字节。
- CLI 包：142,440–177,372 字节，不含可选 DKMS 和系统依赖。
- 本轮 19 个功能/测试/研究文件与云端源码包逐字节一致；原目录工作流只合入新增分析步骤，
  保留了已有 NVL 研究步骤。其余本地源码打包加固与历史文档未覆盖。

GUI 入口：Intel Controls → Core / cache voltage and ratio → Read settings → Target (mV) 与模式。
CLI 接口如下，变量需要根据目标机器明确填写；输入范围不是推荐工作电压：

```text
octool-cli intel-oc-set --cpu N --domain core|cache --field target-mv \
  --value MILLIVOLTS --mode adaptive|override --apply
```

使用前先通过 `diagnose` 获取允许的 CPU。需要特权时从终端使用 `sudo`，不弹图形授权窗口。
`intel-oc-read` 和设置返回的 `before`/`after` 同时提供 `target_mv`、`target_mode`。
完整使用方式见[CLI 手册](headless-cli.md)。不要同时用多个调参进程操作同一邮箱。

## 保留的限制

这批产物为 Linux x86_64；Windows/macOS 仅执行共同核心测试。
支持范围仍限 Raptor Lake-S family 6/model B7，未开放 VF 点写入、逐核 override 或 Xeon server 电压控制。
AMD PStates/曲线设置、完整 PBO 高层参数和板级写入等原有缺口未在本轮宣称完成。
四台目标真机、Secure Boot 实机部署、调参稳定性和跨版本升级尚未验收。
