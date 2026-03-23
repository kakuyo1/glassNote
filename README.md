# glassNote

`glassNote` 是一个基于 `Qt Widgets + C++` 的 Windows 桌面透明便签原型项目，目标是轻量、现代、可维护，并坚持使用本地 JSON 持久化。

## 当前已实现

- 透明无边框悬浮主窗口
- 默认左下角启动，后续恢复上次窗口位置
- 多事项纵向展示
- 双击事项编辑文本
- 新建事项、删除事项、清空空事项
- 每条事项独立配色 `Hue`
- 右键空白区主菜单
- 右键事项专属菜单
- 本地 JSON 自动保存与恢复
- 整体缩放
- 新建、删除、重排、悬停动画
- Windows 玻璃增强尝试，失败时回退到纯 Qt 半透明方案
- 极速捕获输入条（`Ctrl+Alt+Q`）
- 剪贴板收集箱（检测新复制文本，一键导入）
- 窗口边缘拖拽捕获（文本/文件/图像占位）
- OCR 实验开关与入口（默认关闭）
- 更新检查（Manifest 阶段：检查 / 提示 / 忽略版本）

## 当前交互说明

- 在事项下方的底部透明区域左键拖动整个窗口
- 在事项下方的底部透明区域右键打开主菜单
- 双击事项进入编辑：普通段落按 `Enter` 换行；列表/清单按 `Enter` 新建下一项；`Shift+Enter` 在当前项内换行；`Ctrl+Enter` 提交；`Esc` 退出编辑
- 编辑器粘贴会自动按纯文本落地（不继承外部字体/颜色等样式）；工具栏支持字体、字号、文字颜色与清除格式
- 全局热键 `Ctrl+Alt+N`：快速新建事项
- 全局热键 `Ctrl+Alt+Q`：打开极速输入条并直接创建事项
- 托盘菜单支持：剪贴板收集箱导入、收集箱开关、OCR 实验开关/入口
- 右键菜单与托盘菜单支持“检查更新...”，并可设置“启动时自动检查更新”
- 将文本或图片拖拽到窗口边缘可快速捕获为事项

## 数据文件位置

数据使用 `QStandardPaths::AppDataLocation` 保存，文件名为 `state.json`。

运行时也可以通过右键主菜单中的“打开 JSON 数据目录”直接打开所在目录。

## 构建方式

### 依赖

- Qt 6 Widgets
- CMake 3.21+
- MSVC 2022
- Inno Setup 6（生成 `setup.exe`）

### 示例命令

```bash
cmake -S B:\glassNote -B B:\glassNote\build
cmake --build B:\glassNote\build --config Debug
```

可执行文件示例位置：`B:\glassNote\build\Debug\glassNote.exe`

## 生成安装包（Windows）

项目提供一键打包脚本：`scripts\package_release.bat`。

### 前置要求

- `iscc`（Inno Setup 编译器）在 `PATH` 中可用
- `windeployqt` 可用（脚本默认使用 `B:\qtt\6.9.0\msvc2022_64\bin\windeployqt.exe`）

### 示例命令

```bat
B:\glassNote\scripts\package_release.bat 0.1.0
```

说明：传入的版本号会同时写入安装包命名、manifest，以及程序内部运行时版本（用于“检查更新”比较）。

如需在失败时保留窗口便于排查，可附加参数：

```bat
B:\glassNote\scripts\package_release.bat 0.1.0 --pause
```

脚本会自动完成：

- `Release` 构建
- 安装到 staging 目录（`build-release\stage`）
- Qt 运行时依赖拷贝（`windeployqt`）
- 便携包 ZIP 生成（`CPack`）
- 安装包 EXE 生成（Inno Setup）
- 安装包 SHA256 计算
- `update-manifest.json` 自动生成

输出目录：`B:\glassNote\dist`

- `glassNote-<version>-win64-setup.exe`
- `glassNote-<version>-win64-portable.zip`
- `update-manifest.json`
- `update-manifest-<version>.json`
- `package_release.log`（完整打包日志）

说明：脚本会把 `update-manifest.json` 自动同步复制为 `update-manifest-<version>.json`，便于版本归档。

### 可选环境变量（更新清单地址）

- `GLASSNOTE_RELEASE_REPO_URL`：仓库根地址（默认 `https://github.com/kakuyo1/glassNote`）
- `GLASSNOTE_RELEASE_TAG`：发布 tag（默认 `V<version>`）

示例：

```bat
set GLASSNOTE_RELEASE_TAG=V1.0.1
B:\glassNote\scripts\package_release.bat 1.0.1
```

## 自动上传 Release 资产（GitHub）

项目提供上传脚本：`scripts\upload_release_assets.bat`，自动上传以下两个文件到指定 tag：

- `glassNote-<version>-win64-setup.exe`
- `update-manifest.json`

### 前置要求

- `gh` CLI 可用，并已登录（`gh auth login`）
- 对应 GitHub Release tag 已存在
- `powershell.exe` 可用（`.bat` 会调用 `upload_release_assets.ps1`）

### 示例命令

```bat
B:\glassNote\scripts\upload_release_assets.bat 1.0.1
```

可选环境变量：

- `GLASSNOTE_RELEASE_REPO`：仓库（默认 `kakuyo1/glassNote`）
- `GLASSNOTE_RELEASE_TAG`：发布 tag（默认 `V<version>`）

例如：

```bat
set GLASSNOTE_RELEASE_REPO=kakuyo1/glassNote
set GLASSNOTE_RELEASE_TAG=V1.0.1
B:\glassNote\scripts\upload_release_assets.bat 1.0.1
```

## 一键全流程（打包 + 生成清单 + 上传）

如果你已经准备好对应的 GitHub Release tag，可直接执行：

```bat
B:\glassNote\scripts\release_pipeline.bat 1.0.1
```

该脚本会顺序调用：

1. `package_release.bat`（生成安装包、SHA256、manifest、版本化 manifest）
2. `upload_release_assets.bat`（上传安装包和 `update-manifest.json`）

如需失败后暂停窗口：

```bat
B:\glassNote\scripts\release_pipeline.bat 1.0.1 --pause
```

### 卸载数据策略

卸载安装包时**默认保留用户数据**（`%AppData%\glassNote\state.json`），便于重装后继续使用原有事项。

## 更新检查（阶段 2）

当前版本已支持：

- 检查远端 `update-manifest.json`
- 发现新版本后弹窗提示更新说明
- 支持“忽略此版本”
- 支持“前往下载页”
- 应用内下载安装包并执行 SHA256 校验
- 校验通过后可直接启动安装器

默认清单地址：

`https://github.com/kakuyo1/glassNote/releases/latest/download/update-manifest.json`

可通过环境变量覆盖：

`GLASSNOTE_UPDATE_MANIFEST_URL=https://your-host/update-manifest.json`

示例清单（最小可用，支持应用内下载）：

```json
{
  "version": "0.2.0",
  "notes": "修复若干问题\n优化性能",
  "releasePageUrl": "https://github.com/kakuyo1/glassNote/releases/latest",
  "windows": {
    "x64": {
      "installerUrl": "https://github.com/kakuyo1/glassNote/releases/download/v0.2.0/glassNote-0.2.0-win64-setup.exe",
      "sha256": "PUT_REAL_SHA256_HEX_HERE"
    }
  }
}
```

## 测试与质量

- 已接入 `Qt Test`，并按 `unit / integration / perf` 拆分为独立测试目标
- 覆盖内容：JSON 序列化、便签顺序规范化、控制器状态编排逻辑
- 集成流脚本：`scripts\run_load_edit_save_reload_integration.bat`（覆盖 load/edit/save/reload）
- 性能检查：`many-note` 场景下的顺序同步与持久化保存基准

### 运行测试

```bash
cmake -S B:\glassNote -B B:\glassNote\build
cmake --build B:\glassNote\build --config Debug --target glassNoteUnitTests glassNoteIntegrationTests glassNotePerfTests
ctest --test-dir B:\glassNote\build -C Debug --output-on-failure
```

### 运行集成流脚本

```bash
B:\glassNote\scripts\run_load_edit_save_reload_integration.bat B:\glassNote\build
```

## 日志策略

- 启动加载、外部文件重载、回退恢复、保存失败均输出分类日志
- 控制器分类：`glassnote.app.controller`
- 存储分类：`glassnote.storage`
- 仍保留面向用户的消息框提示（失败与恢复场景）

## 当前目录结构

```text
src/
  app/                  应用控制器与业务编排
  animation/            动画协调器
  common/               常量定义
  model/                应用状态与事项数据结构
  platform/windows/     Windows 平台增强
  storage/              JSON 读写与自动保存
  theme/                卡片色板与主题辅助
  ui/                   主窗口、列表容器、事项卡片
```

## 后续可继续优化

- 更真实的亚克力/毛玻璃视觉
- 删除与重排动画进一步细化
- 多屏恢复策略增强
- 卡片内容编辑体验微调
- 导出/导入 JSON 数据
