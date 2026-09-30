# 原版 ELF 的 EL8–EL10 兼容性追踪

更新：2026-09-30。作者本轮要求继续反汇编并解决 EL8–EL10 兼容性。
本页专门记录**源码已丢失的原版 GUI**。基础版重构 GUI 的十目标全绿记录仍见
[verification-status.md](verification-status.md)，不能用它代替原版验证。

## 样本与复现

原始 `octool-linux.zip` 中 `octool` 的 SHA-256：
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
不改其 ELF、调用点、寄存器操作或 96 字节 / mmap 协议。
分析工具只读文件；原始二进制不进 Git，也不进公开 CI 产物。

```sh
python3 -m pip install --target build/reference-tools -r analysis/tools/requirements-reference.txt
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/elf-runtime-audit.py \
  build/input-audit/octool --el8-focus --site-limit 8 \
  --output docs/validation/legacy-el-runtime.json
```

[工具](../analysis/tools/elf-runtime-audit.py)通过 `.gnu.version_r` / `.gnu.version` 取符号版本，
通过 PLT 指令中的 RIP 相对 GOT 重定位找导入项，不假定 PLT 顺序或固定项长。
扫描 42,115 个去除地址/长度重复的函数范围，未出现解码不完整；共 1,195 个未定义动态符号。
[证据](validation/legacy-el-runtime.json)保留 111 个相关导入、完整引用计数、每项最多 8 个位置，
以及 14 个启动/硬件函数的地址、代码哈希、直接调用和字符串引用。
省略 `--site-limit` 与 `--el8-focus` 可生成完整报告。
直接引用不等于运行时可达；虚调用、函数指针、回调和运行中加载的库不据此推断。

## 新定位的障碍

| 障碍 | 反汇编证据 | 处理方向 |
|---|---|---|
| EL9 缺 `hypot@GLIBC_2.35` | `QLineF::length` 的 `0x13f9a27`、`QLineF::unitVector` 的 `0x13f9b83` | 属于静态 Qt，不是某个超频面板；原文件有 NOW 绑定，不能靠“不点该页”避开 |
| EL8 缺 `GLIBC_2.34` | `_start` 调 `__libc_start_main`；Qt QThread、QLibrary 还需要新版本的 pthread/dl 符号 | 只捆绑 ICU/jpeg 不足以启动 |
| EL8 缺 `GLIBC_2.33/2.32` | Qt 文件系统、旧模块加载函数用新 stat/fstat；共享指针及 UART 代码引用 `__libc_single_threaded` | 包含数据符号，不把所有依赖都当作可随意别名的函数 |
| EL8 缺 `GLIBCXX_3.4.26/28` | UART 的 filesystem；Qt 字体 fallback 与 QStringList 的 PMR 分配器/析构/vtable | 换 soname 或只补异常函数不够 |
| EL8 缺 `GLIBCXX_3.4.29` | `__throw_bad_array_new_length` 有 1,087 处静态引用 | 广泛分布于应用容器代码，不能按单个页面修补 |
| ICU70 / JPEG8 | 动态导入含 `_70` 后缀；JPEG 还要求 `LIBJPEG_8.0` | 需要真实匹配的库；EL 的 JPEG62 不能用软链冒充 JPEG8 |
| 加载器改变程序路径 | `initilize_kernel_driver` 在 `0x36defe` 读 `/proc/self/exe`，再 `dirname`/`chdir`；`load_kmod` 也读取它 | 私有加载器与原 ELF 同目录，记录实际 `/proc/PID/exe`；模块由目标 DKMS 安装/外部加载，不能依赖三个旧 `.ko` |
| 启动时硬件访问 | MainWindow 在 `0x8e41d7` 调 `iopl`；`Rdmsr` 在 `0x36f812` 格式化 `/dev/cpu/%d/msr`，随后 open/lseek/read | 运行库修复不改变权限/lockdown 语义，也不能使这些调用自动转向新模块 |

这些是原样本观察，不是新增硬件寄存器定义。仍保留原有 MMIO 对拍和 HAL 自测。

## 私有运行库实验

原源码无法重编时，先验证应用目录内的匹配加载器 + 完整依赖闭包：

- [build-runtime.sh](../port/legacy/build-runtime.sh)只在可丢弃 Ubuntu22.04 容器内安装发行版库，
  用加载器 `--list` 解析原 ELF 的传递依赖，复制实际文件并记录来源、大小、SHA-256、包版本。
  不使用旧包 `mylib` 的过时 libc，也不执行旧 `run_lib.sh`。
- [run.sh](../port/legacy/run.sh)显式调用私有 `ld-linux-x86-64.so.2 --inhibit-cache --library-path`，
  原 ELF 与加载器同目录，glibc/libm/libstdc++/ICU70/JPEG8/其余依赖配套。
  不导出全局 LD_LIBRARY_PATH；拒绝继承 LD_PRELOAD / LD_AUDIT / LD_LIBRARY_PATH。
  采用原版已有的 xcb 插件，禁用本实验中的 GL 集成，EL10 通过 Mutter/Xwayland。
- [独立诊断 workflow](../.github/workflows/legacy-runtime.yml)手动触发，三条 Rocky 容器各自执行。
  旧 ELF 经仓库的**未发布草稿附件**鉴权下载，原文件 SHA 固定，令牌不传入容器。
  仅上传文本证据；原 ELF、运行库、核心转储和凭证都不上传公开 artifact。
- 测试进程 UID10001，无任何 capabilities、无宿主硬件设备、禁止网络、不模拟寄存器。
  校验 DT_NEEDED 全部来自私有目录，再检查实际 `/proc/PID/maps` 中没有混入宿主 `.so`。
  记录 loader/Qt 输出、窗口所有者 PID、标题、OS、镜像、宿主内核和实际可执行路径。
- `Work Tool` 主窗口持续可见 5 秒才算窗口门禁通过。只显示 Error 等对话框会单独记录为
  `dialog-only` 并使任务失败，不能把不支持硬件的提示当作主窗口成功。

初始提交时这条实验尚未取得云端结果，不能称原版已经兼容。实测结果随后追加在本页。
原 ELF 草稿附件上传曾被自动审批拒绝，理由是云端测试授权未明确涵盖该文件外传；
已经向作者提交具体上传范围的选择。未经该项明确授权不执行上传；源码和分析工具验证可继续。
私有库只解决用户态装载；没有解决 iopl/MSR、硬件寄存器正确性、Secure Boot 或原全部页面。
字体/区域设置、OpenGL、外部程序、动态 NSS/驱动模块还需逐项验证。容器使用云端宿主内核，
不能把 EL8 容器窗口成功描述成已在 EL8 的 4.18 内核完成硬件验收。
正式重构版仍走 EL8 静态 Qt 基线与系统 glibc，不把这套实验运行库塞进已有 RPM/DEB。

## 相关原始资料

- [glibc：显式启动动态加载器](https://sourceware.org/glibc/manual/2.39/html_node/Dynamic-Linker-Invocation.html)。
- [glibc：显式加载器与 `/proc/self/exe` 的差异](https://sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker-Introspection.html)。
- [GitHub：草稿 release 仅有 push 权限的账户可列出](https://docs.github.com/en/rest/releases/releases)。
- [原始多发行版分析](../analysis/docs/linux-porting.md)，保留历史实测边界。
