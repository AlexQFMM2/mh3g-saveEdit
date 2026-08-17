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

## Game-resource ID tables

The item, five armor-part, and twelve weapon CSV tables are generated from the
zero-based GMD arrays in the audited MH3G `ID_res.arc`; Dex database keys and
filtered third-party row numbers are not treated as save IDs. The full arrays
include all `DUMMY` and unused slots so later names can never shift when a
placeholder is hidden in the UI.

The authoritative counts are 1,533 item slots; 380 head, 382 chest, 363 arms,
371 waist, and 377 legs slots; plus the complete 12 weapon arrays. The rebuild
restores 7 valid collaboration weapons, 9 valid collaboration items, and the
late collaboration armor entries omitted by the old lists.

See [`data/README.md`](data/README.md) for source hashes, the replacement-font
limitation, deterministic rebuild commands, and validation details.

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
