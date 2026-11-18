# 声核悬浮菜单

独立原生皮肤，版本 **0.1.1**，Windows x64，原生 Skin ABI **0.2**。原生皮肤在宿主进程内执行，请只安装可信来源。

![预览](assets/preview.png)

## 声核与开发

- [声核官网](https://audcore.utae.cn/) · [下载声核](https://audcore.utae.cn/download/) · [使用指北](https://audcore.utae.cn/guide/)
- [skin-dev 皮肤开发入口与 SDK](https://audcore.utae.cn/skin-dev/)
- [皮肤开发与发布规范](https://github.com/audcore/audcore-document/blob/main/docs/皮肤发布规范.md)
- [皮肤列表](https://github.com/audcore/audcore-document/blob/main/skins.md)

## 独立构建与安装

安装 Visual Studio C++ Build Tools（MSVC 19.44.35222、Windows SDK 10.0.26100.0）和 Python。工具版本在 manifest.json 声明；当前是显式脚本 recipe，使用现有低占用、串行构建入口，正式发布仍须人工核对实际工具版本。

```powershell
git clone https://github.com/audcore/audcore-skin-floating.git
cd audcore-skin-floating
powershell -NoProfile -File build.ps1
```

最终输出 `dist/cn.audcore.floating-menu-v0.1.1.skin.audcore`，中间产物 `build/skin.dll`。原生 .skin.audcore 本质为 DLL；在声核中打开安装并选用，不直接运行 DLL。本仓库是皮肤的唯一维护来源，不依赖私有源码。源码迁移不代表已更新官方在线目录。

构建遵守全机单项编译锁、BelowNormal 优先级和资源限制；不并行构建其他项目。第三方依赖和许可随源码保留。

## 反馈与许可

[皮肤问题](https://github.com/audcore/audcore-skin-floating/issues) · [声核本体问题](https://github.com/audcore/audcore-document/issues)。自有代码采用 MIT，第三方代码保留各自许可证。

## 原有使用与测试说明

# 声核悬浮菜单 v0.1.1

安装 `Audcore-Floating-v0.1.1.skin.audcore` 后，在声核设置的「皮肤」中选择「声核悬浮菜单」。按住图标拖动，松开后保留位置。右键打开菜单。图标取得焦点后，也可按菜单键或 Shift+F10。Esc 取消菜单。

| 入口 | 操作 |
| --- | --- |
| 工程 | 新建空白预设、打开、保存、撤销、重做。最近工程、预设模板及另存为由宿主生成补充原生菜单。打开及另存为使用系统文件选择器。 |
| 所有通道 | 按工程顺序显示每个通道，包括隐藏通道。静音、独奏、独奏常驻、反相、录音、效果链旁通、发送启用和通道显隐直接在菜单中切换。发送取样点使用单选。 |
| 通道扩展操作 | 输入设备、独占与解除独奏、录音取样与分组、链接分组、输出设备、添加发送、复制通道和效果器离线等由宿主生成补充原生菜单。依赖扩展的输入配置可能打开宿主选择或配置窗口。 |
| 编辑 | 通道名称、增益、发送电平、效果器混合量使用小型 Windows 编辑对话框。通道颜色、图标、格式、链接设置、快捷键录入、添加效果器及效果链导出标题沿用宿主编辑界面。效果器的「打开界面」启动其编辑器。 |
| 全局 | 播放状态可直接切换，停止和重播使用宿主原生菜单。显示菜单提供主题与图标缩放。设置分类使用宿主原生菜单，具体设置及性能使用声核窗口。 |

补充原生菜单和小窗的宿主编辑界面需要包含本次宿主改动的声核版本。皮肤使用实验性原生 ABI 0.2。Windows 控制系统菜单的外观。皮肤隐藏或销毁时释放菜单，宿主状态变化时关闭打开的菜单，避免从旧工程触发操作。

## 构建与验证

从声核仓库根目录运行下面的命令。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File skin-contract/build-floating-skin.ps1 -Test
```

构建复用直播编译互斥锁，在 BELOW NORMAL 优先级和 1 GiB Windows Job 内串行编译。脚本校验皮肤版本资源及 PE，测试通过后生成版本化安装包。测试使用真实 HMENU 检查子菜单、勾选与单选，无窗口检查命令身份、菜单取消、键盘入口及声核图标的预乘透明像素。


已用声明的 MSVC、Windows SDK 和 Python 独立编译，并通过最终 PE 产物检查。未进行真实宿主窗口验收。
