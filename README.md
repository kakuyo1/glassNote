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

## 当前交互说明

- 在事项下方的底部透明区域左键拖动整个窗口
- 在事项下方的底部透明区域右键打开主菜单
- 双击事项进入编辑，`Enter` 提交，`Shift+Enter` 换行，`Esc` 退出编辑

## 数据文件位置

数据使用 `QStandardPaths::AppDataLocation` 保存，文件名为 `state.json`。

运行时也可以通过右键主菜单中的“打开 JSON 数据目录”直接打开所在目录。

## 构建方式

### 依赖

- Qt 6 Widgets
- CMake 3.21+
- MSVC 2022

### 示例命令

```bash
cmake -S B:\glassNote -B B:\glassNote\build
cmake --build B:\glassNote\build --config Debug
```

可执行文件示例位置：`B:\glassNote\build\Debug\glassNote.exe`

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
