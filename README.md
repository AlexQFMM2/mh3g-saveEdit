# mh3g-saveEdit

Qt 5 save viewer/editor for Monster Hunter 3G / Monster Hunter 3 Ultimate.
The editor automatically detects both supported character-file formats:

- Nintendo 3DS `user1`/`user2`/`user3`: 35,328 bytes (`0x8A00`), little-endian.
- Wii U `user1`/`user2`/`user3`: 35,364 bytes (`0x8A24`), a 36-byte header followed by big-endian character data.

Always keep an untouched backup of the complete save directory before editing.

The Chinese management interface keeps Character, Item Chest, and Equipment Box in one window.
Saving atomically replaces the currently opened file; there is no Save As command and the editor
does not create a backup. A successful save is confirmed by a message box.

## 资料库功能已移除

项目曾实验性加入武器、防具资料库、实时模型预览和人物试衣间。该方向需要为每一代游戏
持续维护装备数据、模型映射、骨骼、材质和主机专用渲染规则，工作量已经接近单独重做一套
游戏资源查看器，而且 3G 的实现不能低成本、可靠地扩展到 4G 和 GU。为了让项目重新聚焦于
存档修改的正确性和稳定性，正式版本已移除资料库与模型资源包。

从下一次 Windows 构建开始，Action 不再下载或合并 `MH3GResources`，portable 包也不再
包含图鉴数据库、OpenGL 模型查看器或试衣间。已经完成的调查仍保留在 Git 历史中，停止原因、
技术结论和未来边界见 [资料库功能决策记录](docs/ENCYCLOPEDIA_DECISION.md)。

## 下一阶段：本地配装器

下一步从数据量最小、验证路径最短的 MH3G 开始制作本地配装器。首版允许选择武器、五件
防具、护石和装饰珠，实时汇总技能、防御与耐性，并把整套装备事务式加入当前 3DS 或 Wii U
存档的装备箱。它不恢复图鉴、图片、模型或资源包。

本地闭环实机验收后，再依次建设统一配装协议、用户注册登录与邮箱验证、管理后台和在线
配装广场。广场中的配装可以导入修改器并一键加入装备箱，服务器不会接收完整存档或角色
信息。完整阶段、接口边界和验收标准见
[本地配装器与在线平台路线图](docs/LOADOUT_PLATFORM_ROADMAP.md)。

## SQLite 数据与装备合法性

运行时静态数据已统一迁移到 `data/mh3g.sqlite`，不再解析 `data/cn`、
`data/en` CSV。数据库同时保存中英文名称、武器/防具参数、技能、装饰珠和
全游戏护石生成组合；程序通过只读数据仓库访问，不在页面中散落 SQL。

装备箱新增合法性列和“只显示合法”筛选：明确非法显示红色，缺少平台或
字段证据显示黄色，已确认自然合法正常显示。武器、防具和护石弹窗会实时
说明原因，装备表单导入也会汇总风险。所有结果都只作提示，不自动修正，
也不阻止应用、导入或保存。

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
