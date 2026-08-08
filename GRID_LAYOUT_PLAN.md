# Weasel 5×5 候选矩阵（卷轴模式）开发规划

> 状态: 规划完成, 未开工 | 分支: `grid-layout` | fork: `siegfriedwhisper/weasel`
> 本地源码: `C:\Users\weisz\weasel-src` (含 librime 子模块, 已拉全)
> 编译方式: GitHub Actions 云端编译 (本地无 VS, 已装 gh CLI 并登录 siegfriedwhisper)

## 功能目标

按 ↓ 方向键, 候选词从单行横排展开为 5×5 矩阵 (仿搜狗智慧版"卷轴"); ↑ 收起。

- **↓**: 展开 5×5 矩阵 (page_size=25)
- **↑↓←→**: 矩阵内移动高亮 (左右 ±1, 上下 ±5)
- **空格/回车**: 上屏选中项
- **↑(顶行时) 或 Esc**: 收起

## 已完成

- [x] 读通布局体系: `Layout`(抽象) → `StandardLayout` → `Horizontal/Vertical/VHorizontal/FullScreenLayout`
- [x] 读通按键链路: TSF `KeyEventSink::_ProcessKeyEvent` → IPC → `RimeWithWeaselHandler::ProcessKeyEvent` → `_UpdateUI` → `WeaselPanel::_CreateLayout()`
- [x] 读通 IPC 协议: `WeaselIPC.h` 枚举 + `WeaselClientImpl.cpp` + `WeaselServerImpl.cpp` (PIPE_MSG_HANDLE 宏) + `RimeWithWeasel` handler
- [x] 确认 page_size 机制: librime `Menu::Prepare` 惰性生成, schema `menu/page_size` (当前 rime_mint=8), 提到 25 无性能问题
- [x] 确认导航简化: 矩阵内高亮 = 调现有 API `HighlightCandidateOnCurrentPage(index)`, index 直接 ±1/±5, 引擎零改动
- [x] 性能/内存评估: 无风险 (惰性候选生成; 布局矩形数组编译期固定 8KB, 与候选数无关; 新增内存 <100KB)
- [x] `WeaselIPCData.h` 已加 `LAYOUT_GRID` 枚举 (=5, 在 LAYOUT_TYPE_LAST=6 前, 静态检查通过, 编译验证待 Actions)

## 待开发 (预计一个工作日)

| # | 内容 | 预估 |
|---|------|------|
| 1 | `WeaselUI/GridLayout.cpp/.h` 新类: 5×5 网格布局 + 高亮矩形定位, 仿 HorizontalLayout 结构, 继承 StandardLayout | 1-1.5h |
| 2 | TSF 拦截方向键: `WeaselTSF` 捕获 VK_DOWN/UP/LEFT/RIGHT → 计算 index (±1/±5, 边界裁剪) → `m_client.HighlightCandidateOnCurrentPage(index)`; ↓ 展开/↑ 收起状态机 | 0.5h |
| 3 | IPC 切换命令: `WeaselIPC.h` 枚举 + `Client::ToggleGrid` + `WeaselClientImpl.cpp` + `WeaselServerImpl.cpp` PIPE_MSG_HANDLE + `RimeWithWeasel` handler (改 `style.layout_type` + `_UpdateUI`), 仿 CHANGE_PAGE 模式 | 0.5-1h |
| 4 | `WeaselPanel::_CreateLayout()` 注册 `LAYOUT_GRID` → `new GridLayout` | 0.3h |
| 5 | page_size 配置: `weasel.custom.yaml` patch `menu/page_size: 25` (或改 rime_mint.schema.yaml) | 0.1h |
| 6 | GitHub Actions workflow: windows-latest + VS2022, 编译 weasel+librime, 上传 artifact (首次配置需调试) | 1-2h |
| 7 | 下载产物 → 替换 `C:\Program Files\Rime\weasel-0.17.4\` → 部署验证 | 0.5h |

## 关键文件清单

- 布局枚举: `include/WeaselIPCData.h` (LayoutType, 已改)
- 布局基类: `WeaselUI/Layout.h`, `WeaselUI/StandardLayout.h`, `WeaselUI/Layout.cpp`
- 参考实现: `WeaselUI/HorizontalLayout.cpp/.h` (多行折行逻辑可参考)
- UI 装配: `WeaselUI/WeaselPanel.cpp` (_CreateLayout, ~L110-131)
- IPC 枚举: `include/WeaselIPC.h` (~L20-36)
- IPC 客户端: `WeaselIPC/WeaselClientImpl.cpp` (~L83-104 照抄 ChangePage)
- IPC 服务端: `WeaselIPCServer/WeaselServerImpl.cpp` (~L394-399 注册 handler)
- 引擎 handler: `RimeWithWeasel/RimeWithWeasel.cpp` (ProcessKeyEvent ~L264, _UpdateUI ~L518, style 加载 ~L1161-1330)
- TSF 按键: `WeaselTSF/KeyEventSink.cpp` (_ProcessKeyEvent ~L11-63)
- TSF 高亮调用: `WeaselTSF/CandidateList.cpp` (~L380-436)
- 候选上限: `WeaselUI/StandardLayout.h` MAX_CANDIDATES_COUNT=100 (25 够用)
- page_size: librime `src/rime/schema.cc` 读 `menu/page_size`, 惰性生成在 `src/rime/menu.cc`

## 注意事项 / 坑

- `WeaselPanel` 布局选择在 `_CreateLayout()`, 每次 UI 更新都会重建布局对象 → 切换 layout_type 后自动生效
- style 从配置加载在 `RimeWithWeasel.cpp` `_UpdateUIStyle`, 有 `_layoutArr` 数组 (L1263), 如需配置项支持 "grid" 要加映射; 但矩阵是运行时按键切换, 不需要配置文件入口
- 方向键在 Rime 默认是"下移高亮/翻页", 拦截后需吃掉按键 (pfEaten=TRUE) 避免冲突
- 深色模式/color_format 等皮肤知识见技能 `weasel-skin-config`
- fork 推送: `git remote add fork https://siegfriedwhisper:<TOKEN>@github.com/siegfriedwhisper/weasel.git` (token 在 `C:\Users\weisz\.gh_token`)
