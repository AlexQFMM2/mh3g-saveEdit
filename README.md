# mh3g-saveEdit

Qt 5 save viewer/editor for Monster Hunter 3G / Monster Hunter 3 Ultimate.
The editor automatically detects both supported emulator character-file formats:

- Citra 3DS emulator `user1`/`user2`/`user3`: 35,328 bytes (`0x8A00`), little-endian.
- Cemu Wii U emulator `user1`/`user2`/`user3`: 35,364 bytes (`0x8A24`), a 36-byte header followed by big-endian character data.

实体机原始存档不在支持范围内。读取存档后，主界面顶部会持续显示当前文件对应的模拟器和格式。

Always keep an untouched backup of the complete save directory before editing.

The Chinese management interface keeps Character, Item Chest, and Equipment Box in one window.
Saving atomically replaces the currently opened file; there is no Save As command and the editor
does not create a backup. A successful save is confirmed by a message box.

## 项目状态

截至 2026-09-04，MH3G 修改器的计划内功能阶段性完成，当前版本为 `v1.3.0-beta.7`，项目进入
缺陷修复、数据确证和兼容性维护阶段。桌面存档编辑、本地配装器、配装广场、一键加入装备箱、
账号与举报治理的完成范围、验证门禁、明确不做项及 MH4G 交接顺序见
[MH3G 修改器阶段完成记录](docs/MH3G_COMPLETION_RECORD.md)。

## 资料库功能已移除

项目曾实验性加入武器、防具资料库、实时模型预览和人物试衣间。该方向需要为每一代游戏
持续维护装备数据、模型映射、骨骼、材质和主机专用渲染规则，工作量已经接近单独重做一套
游戏资源查看器，而且 3G 的实现不能低成本、可靠地扩展到 4G 和 GU。为了让项目重新聚焦于
存档修改的正确性和稳定性，正式版本已移除资料库与模型资源包。

从下一次 Windows 构建开始，Action 不再下载或合并 `MH3GResources`，portable 包也不再
包含图鉴数据库、OpenGL 模型查看器或试衣间。已经完成的调查仍保留在 Git 历史中，停止原因、
技术结论和未来边界见 [资料库功能决策记录](docs/ENCYCLOPEDIA_DECISION.md)。

## MH3G 手动配装器

左侧“配装器”在没有读取存档时也可使用。页面固定包含武器、头、胸、腕、腰、腿和护石七格：

- 选择武器后，五个防具弹窗默认只显示同职业或通用防具；头、胸、腕、腰、腿的部位由点击的格子锁定。
- 防具支持任意多个技能点条件，比较符为 `>`、`≥`、`=`、`≤`、`<`，所有条件按 AND 查询。
- 护石使用紧凑表单，直接选择护石类型、孔数、两项技能及点数；是否属于自然组合只作红色提示。
- 每格可配置最多三个装饰珠；技能矩阵、胴系统倍加、发动阈值、初始/最终防御、五耐性和孔位会实时重算。
- 读取存档后，武器、防具和护石选择窗口可打开独立的“从装备箱选择”弹窗；弹窗按原始页/格顺序
  显示具体实例及已安装装饰珠，选择后自动带回装备 ID、护石技能、孔位和珠子。
- 配装可以保存为 `.mhloadout.json`；本地配装的 dirty 状态与存档修改状态彼此独立。
- 读取存档后“一键加入装备箱”始终可点击；若七格未填满，会明确列出缺少的部位。完整时先预留七个空格再一次性写入内存；不自动穿戴或保存磁盘。

工具栏的“自动配装”会在本地以当前具体武器为固定条件搜索自然合法组合；可按性别筛选，通过“添加技能”持续增加发动技能条件，也可手动固定头、胸、腕、腰、腿和护石并编辑珠子。手动珠子允许超孔，固定护石允许非自然技能组合；未固定部位仍只自动补齐自然合法候选。任务支持暂停、取消、倒计时和结果回填，添加结果时保留名称、导出路径和搜索窗口。

孔位超限、职业/性别冲突及未确认数据只作颜色和原因提示，不代替用户修正。只有 ID 无法解析、部位错误、
装备箱空间不足或数组边界错误才会阻止一键写入。自动搜索只覆盖自然合法域，也不恢复图鉴、图片、模型或资源包。

公开配装大厅、筛选、只读详情、一键加入装备箱、个人信息、发布查重、点赞和举报已经上线；
大厅只显示公开昵称与数字 ID，服务器不会接收完整存档或角色信息。邮箱注册、邮箱登录、绑定
邮箱、忘记密码和新版蓝白账号中心随 `v1.3.0-beta.2` 发布；`v1.3.0-beta.3` 增加本地自动配装搜索、
独立倒计时和结果多维排序，`v1.3.0-beta.4` 补齐 Windows 最大化按钮并精简重复的目标技能列；
`v1.3.0-beta.5` 增加固定装备/护石、手动超孔珠子与重复装珠解剪枝。
`v1.3.0-beta.6` 优化非模态自动配装窗口、孔位圆点状态显示，以及装饰珠选择器的左右加入/移除交互；
`v1.3.0-beta.7` 增加 Citra/Cemu 模拟器格式常驻标识和读取确认提示。
该版本作为 GitHub prerelease 发布，Windows 便携包
通过 SQLite、SSL、主窗口、配装器、账号中心和长列表交互构建门禁。

修改器默认连接 `https://mhed.desk.65h26i.top`，仍可用 `MHED_DESK_API_URL` 覆盖。生产邮件
功能当前等待管理员配置 AOKSend 后启用，因此注册、验证码和找回密码暂不可用；已有账号登录、
昵称、获赞统计和全部配装大厅功能不受影响。
完整阶段、接口边界和验收标准见
[本地配装器与在线平台路线图](docs/LOADOUT_PLATFORM_ROADMAP.md)。
页面、查询、计算和写入设计见
[手动配装器详细设计](docs/MH3G_LOADOUT_BUILDER_PLAN.md)。

## 长列表输入选择组件

道具、装备、装饰珠、技能、护石、脸型、发型和声音等名称型长列表统一支持输入中文或英文
关键词筛选。点击输入区不会强制展开完整列表或主动失焦；关键词只有在点击候选或按回车后
才提交，未确认文字在失焦时恢复原值。页面保存时读取候选的真实 `UserRole` ID，不把显示文字、
无效索引或临时关键词写入存档。

男/女、比较符、孔数、语言等短固定枚举仍使用普通不可输入下拉框。完整的适用范围、焦点、
键盘、提交、状态分离和接入要求见
[长列表输入选择组件规范](docs/SEARCHABLE_COMBOBOX_RULES.md)。

复选框由应用统一绘制为白底边框、蓝色选中态和白色勾号，不依赖 Windows 或 Linux 原生主题，
避免只显示勾号却看不到可点击边界。

## 奇面族修改研究

已从汉化版 CCI 的原生 XFS 参数和菜单 GMD 中确认茶茶、卡扬巴使用的 23 个面具逻辑 ID、
5 个舞蹈 ID 和 30 个正常可装备特技 ID。舞蹈与特技名称不再依赖 Wiki 行号猜测；Wiki 只作
参数和译名交叉核对。3DS 茶茶当前面具字段及共享面具仓库中的火龙面具获得位已经由单变量
存档差分确认。由于公共解锁与剧情、任务、交易和成长进度关联，贸然写入可能造成流程状态
不一致；该方向已经归档为只读研究，不加入编辑入口。
证据路径、完整编号、资源编号陷阱及待验证边界见
[MH3G 奇面族修改研究记录](docs/SHAKALAKA_RESEARCH.md)。

## SQLite 数据与装备合法性

运行时静态数据已统一迁移到 `data/mh3g.sqlite`，不再解析 `data/cn`、
`data/en` CSV。数据库同时保存中英文名称、武器/防具参数、技能、装饰珠和
全游戏护石生成组合；程序通过只读数据仓库访问，不在页面中散落 SQL。

装备箱新增合法性列和“只显示合法”筛选：明确非法显示红色，缺少平台或
字段证据显示黄色，已确认自然合法正常显示。武器、防具和护石弹窗会实时
说明原因，装备表单导入也会汇总风险。所有结果都只作提示，不自动修正，
也不阻止应用、导入或保存。

道具箱和装备箱使用虚拟表格：模型只保存格子索引，名称、装饰品和合法性
按当前可见行读取并缓存，切换页面时不再预先创建数千个单元格对象。

存档 ID 权威仍是审核过的 `ID_res.arc` 完整数列；Dex 主键只提供属性和关联，
绝不会直接写入存档。DUMMY 和未使用槽继续保留，避免后续 ID 错位。护石
技能点按有符号 8 位显示，写回保持原始补码。完整来源、覆盖范围和确定性
重建命令见 [`data/README.md`](data/README.md)。

固定哈希的 MH3G ExeFS 五张原生防具表已进入离线生成链路，用于校验并补齐
存档本地参数；ExeFS 本身不会随修改器发布。修改器不提供防具强化等级编辑或
合法性判断，存档已有强化值原样保留，强化交由游戏内系统完成。

## 3DS / Wii U item and equipment transfer

The item-chest and equipment-box windows can export all 1,000 slots to versioned CSV transfer forms. Open the destination character file (3DS or Wii U), import the matching form, confirm the listed-slot replacement, and then save the character file from the main window.

The forms contain normalized logical values rather than platform-specific save bytes. This lets the editor recreate items and equipment with the destination platform's correct byte order without attempting an unsafe full-save conversion. Import is validated before any slot is changed; listed slots are overwritten and slots omitted from a form are left unchanged.

## Linux build

Install the Qt 5 development tools, then run:

```bash
qmake MH3USaveEditorGUI.pro
make -j
./run-linux.sh
```

## Windows build

Install a Qt 5 MinGW kit first, then run from the repository root. For MSYS2 MINGW64, the Qt bin path is usually `C:\msys64\mingw64\bin`; MSYS2 may provide the tools as `qmake-qt5.exe` and `windeployqt-qt5.exe`, which this script also supports.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-windows.ps1 -QtBin C:\Qt\5.15.2\mingw81_64\bin
```

If the script can find Qt under `C:\Qt`, `-QtBin` can be omitted:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-windows.ps1
```

The packaged Windows build is written to `release/windows/`. The script runs `windeployqt`, ensures the `platforms/qwindows.dll` plugin is present, and then copies additional MinGW/MSYS2 runtime DLLs detected by `objdump`.

## Save-format regression test

Pass known-good 3DS and Wii U character files to the test runner:

```bash
./tests/run-save-format-tests.sh /path/to/3ds/user1 /path/to/wiiu/user1
```

The test verifies byte-identical unchanged round trips, read/write byte-order conversion for money, Moga Points, items, equipment, and jewels, and item/equipment transfers in both directions. Test inputs are never overwritten.

## Loadout regression test

The repository, multi-skill filters, calculator, local JSON and transactional write tests do not require a save sample:

```bash
./tests/run-loadout-tests.sh
```

To additionally verify that only seven equipment records change on both real formats, pass private samples. The inputs are read-only and the test writes to a temporary directory:

```bash
./tests/run-loadout-tests.sh /path/to/3ds/user1 /path/to/wiiu/user1
```

## Searchable combo interaction test

The searchable item, equipment, decoration, and skill selectors have an offscreen Qt interaction regression test:

```bash
./tests/run-searchable-combo-tests.sh
```
