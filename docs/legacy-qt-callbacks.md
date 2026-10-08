# 原 Qt 回调索引与实际槽函数

日期：2026-10-08。继续离线研究，作者已确认暂时不能在四台机器采集 Linux 身份信息。
因此真实 DMI/PCI/寄存器验收继续标为待确认，当前工作不以这些输入为前提。

本轮把 Qt 元对象的字符串/方法索引，与原 ELF 中真正执行分派的跳表和虚函数表交叉核对。
39 个选定类的 **451 个方法索引全部到达同名类方法的入口**；另有156项边界检查，合计607项。
Windows与`2270c6b / 37761865986`云端均通过，607项观测除environment字段完全一致，
见[验证记录](validation/legacy-qt-msr-ci-2270c6b.json)。这是入口映射的完整结果，不是451个函数体全部逆向。

## 复现与证据

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/qt-callback-map.py \
  --binary build/input-audit/octool --export-fixture analysis/fixtures/legacy-qt-callbacks.json \
  --output build/qt-callbacks-full-image.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/qt-callback-map.py \
  --fixture analysis/fixtures/legacy-qt-callbacks.json --output build/qt-callbacks.json
```

- 原 ELF SHA固定为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
- [公开 fixture](../analysis/fixtures/legacy-qt-callbacks.json) SHA为
  `8db630f5cbcf923af436914314409abf36ef8431e2d267a4b46169b7ea175d45`，包含6,893字节的原分派指令、
  方法清单、选中跳表/虚表与函数地址，不含槽函数体/完整Qt库/原ELF。
- [Windows 结果](validation/legacy-qt-callbacks-windows.json)保存每个索引、元数据名、原执行地址轨迹、
  到达的符号、步骤数；0个元数据/目标不匹配。Linux重放使用相同fixture哈希。

工具从 `qt_meta_data_*` 的 method_count / method_data 以及每条5个uint32记录读取名称索引、
参数个数和原始flags；字符串来源是先前验证过的 `qt_meta_stringdata_*` 解码。
仅接受已识别的Qt5元数据修订号；原数据/指令均有哈希。

对于跳表，读取原 RIP 相对基址和有符号32位偏移，要求每个表项仍位于该类分派函数的声明范围。
引用 signal 函数地址用于比较的 LEA 单独处理，不能误读成跳表。
虚调用使用原 `_ZTV<class>` 的主对象 vptr=`vtable+16`，保留原函数指针；并不把 closeEvent 猜成直接调用。

Unicorn 仅执行 `qt_static_metacall` / `.part.*` 路由函数。构造假的 QObject/参数数组后，
在首次到达任何槽/库函数之前停止；没有任何Qt回调体、设备访问或内核代码执行。
边界地址放一个**不执行的合成 RET**，避免 emulator 预翻译零页穿过页界；原路由字节未改。
这解决了最初 `cpufunctions::on_save_profile_clicked` 地址0x765f00的预取故障，不是修改原算法。
单例上限512条指令/1秒，逃出已解码指令或未知入口即报错。

每类另外验证 method=-1、method_count、INT_MAX 及 call_kind=1 均返回且不进外部函数。
这里只核对 call_kind=0 的方法调用和这一个非调用边界，未声称覆盖全部 QMetaObject::Call 枚举。
篡改fixture、篡改表数据、移除允许的回调边界均有拒绝测试。

## 覆盖类与数量

| 类 | 方法数 |
|---|---:|
| MainWindow | 84 |
| cpufunctions | 50 |
| intel_ctl / intel_ctl2 / intel_ctl3 / intel_ctl5 / intel_ctl6 | 20 / 15 / 23 / 25 / 31 |
| intel_mainstream / intel_client / intel_hedt_window | 7 / 6 / 6 |
| intel_memtime / adl_timings / arl_memtime / gnr_memtime / nvl_memtime | 1 / 6 / 2 / 0 / 2 |
| amd_am3 / amd_am4 / amd_am5 / amd_umc / am5_tuners | 2 / 3 / 3 / 2 / 3 |
| amdvf / intel_vf_curve | 30 / 25 |
| tr5_mb / tr5_mb2 / w790_mb / w890_mb | 5 / 4 / 5 / 6 |
| vrmread / vrmread_am5 | 3 / 7 |
| ddr5_spd / pmic / pmic_amd | 7 / 14 / 15 |
| rw_msr / rw_memory / rw_pci / rw_ecram / rw_ioport | 4 / 4 / 3 / 3 / 4 |
| rw_ocmb / rw_ocmb_gnr / rw_biosmb | 7 / 7 / 7 |

MainWindow 的84项包含之前83个无参数 `on_*` 槽之外的事件回调；两份计数并不冲突。
`gnr_memtime` 元对象0项不表示它没有功能：显式 QObject::connect、lambda、虚函数/定时器仍可调业务函数。
方法名如 `on_pushButton_13_clicked` 尚不能映射成用户看到的按钮标题，需要继续核对 setupUi/connect。

## 后续边界

当前完成的是这39类**已声明元方法的索引→入口**核对。其余QObject类、信号到槽连接关系、
函数体、参数含义、寄存器/单位/失败恢复仍沿[覆盖台账](reverse-engineering-status.md)继续研究。
不能把“元对象出现此方法”或“入口路由正确”当成对应平台的可用性/安全写入证明。
