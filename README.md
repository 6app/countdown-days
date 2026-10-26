# 倒计日 CountDay

嵌入 Windows 桌面的倒数日卡片。卡片放在桌面图标层，永远在其他窗口下方，按 Win+D 也不会消失。
主要面向 Win10 和 4K 高缩放屏幕。

- 单个 exe，约 400 KB，不需要安装任何运行库
- 空闲时 CPU 为 0：只在本地零点、修改系统时间、睡眠唤醒、解锁时重绘一次
- 每显示器 DPI 感知（PerMonitorV2），150%/200% 缩放下文字清晰，多屏可用

## 下载

在 [Releases](https://github.com/6app/countdown-days/releases) 页面下载 `CountDay.exe`，双击即可运行。

## 使用

| 操作 | 方式 |
|---|---|
| 新建卡片 | 托盘菜单或卡片右键菜单 →「新建倒计日」 |
| 编辑 | 双击卡片，或右键 →「编辑」 |
| 换配色、调不透明度 | 卡片右键菜单 |
| 移动 / 调整大小 | 托盘菜单取消勾选「锁定位置和大小」后，拖动卡片移动，拖边缘或右下角缩放；靠近屏幕边缘会自动吸附，按住 Alt 可关闭吸附 |
| 开机自启 | 托盘菜单 →「开机自动启动」（写入当前用户的 Run 注册表项） |

日期没到时显示「还有 N 天」，当天显示「就是今天」，非重复卡片过期后显示「已到期」。
勾选「每年重复」后，过了目标日会自动算到下一年；2 月 29 日在平年按 2 月 28 日算。

配置保存在 `%APPDATA%\CountDay\config.json`。如果 exe 同目录下有 `config.json`，就改用这个文件（便携模式）。

## 构建

**Windows（Visual Studio 2019+）**

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config MinSizeRel
```

**macOS / Linux 交叉编译**（用 [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)）

```bash
LLVM_MINGW=/path/to/llvm-mingw sh tools/build_mingw.sh
```

产物是 `build-mingw/CountDay.exe`。

**发布新版本**：推送 `v*` 标签（如 `git tag v1.0.1 && git push origin v1.0.1`），GitHub Actions 会在 Windows 上用 MSVC 编译，然后自动创建 Release 并上传 exe。说明文字取自 `.github/release-notes.md`。修改图标的话，执行 `python3 tools/make_icon.py` 重新生成 `res/app.ico`。

## 实现要点

| 文件 | 职责 |
|---|---|
| `src/desktop_host.cpp` | 找到桌面的 `SHELLDLL_DefView`（在 Progman 或 WorkerW 下），卡片作为它的子窗口 |
| `src/card.cpp` | 分层子窗口：拖动/缩放、吸附、右键菜单、显示器变化后的位置修正 |
| `src/renderer.cpp` | Direct2D（软件模式）+ DirectWrite 绘制到 DIB，再用 `UpdateLayeredWindow` 提交；画完立即释放位图 |
| `src/daycalc.cpp` | 天数计算、每年重复、零点定时 |
| `src/main.cpp` | 隐藏主窗口：托盘、定时器、系统广播（`TaskbarCreated`、时间变化、电源、解锁） |
| `src/config.cpp` | JSON 配置读写（内置小解析器），先写临时文件再替换 |

- **资源管理器重启**：桌面窗口被销毁时，卡片会一起销毁。程序收到 `TaskbarCreated` 或发现卡片被销毁后，会重新找桌面并重建卡片。
- **退化模式**：启动后 30 秒内一直找不到桌面窗口时，卡片改成置底的普通顶层窗口。这种模式下 Win+D 会把卡片隐藏。
- 分层子窗口需要在清单里声明 Win8+ 兼容（见 `res/app.manifest`）。删掉这一项后子窗口会创建失败，卡片只能退化成置底的顶层窗口。

## 需要在真机上验证的点

1. 4K 屏 150% / 200% 缩放下的文字清晰度和默认尺寸
2. Win+D、资源管理器重启（任务管理器里重启 Windows 资源管理器）之后卡片是否还在
3. 多显示器、拔插显示器之后的位置
4. 桌面右键 →「查看」→ 取消「显示桌面图标」时，卡片会跟图标一起隐藏（这是预期行为）
5. 装了 Wallpaper Engine 之类的动态壁纸软件时，卡片是否仍在图标层上方
