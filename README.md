# mh3g-saveEdit

Qt 5 save viewer/editor for Monster Hunter 3G / Monster Hunter 3 Ultimate.
The editor automatically detects both supported character-file formats:

- Nintendo 3DS `user1`/`user2`/`user3`: 35,328 bytes (`0x8A00`), little-endian.
- Wii U `user1`/`user2`/`user3`: 35,364 bytes (`0x8A24`), a 36-byte header followed by big-endian character data.

Always keep an untouched backup of the complete save directory before editing.

The Chinese management interface keeps Character, Item Chest, and Equipment Box in one window.
Saving atomically replaces the currently opened file; there is no Save As command and the editor
does not create a backup. A successful save is confirmed by a message box.

## 武器资料库

左侧“资料库”无需读取存档即可使用。首版收录 3G 的 12 类、1,421 件武器，支持中英日文搜索、
属性与稀有度筛选、横向强化树、路线高亮、七色斩味、生产/强化素材，以及素材到相关武器的
双向跳转。详情地址使用稳定形式，例如 `mhdb://mh3g/weapon/7/12`。

读取 3DS 或 Wii U 角色文件后，可从详情页把武器或素材加入第一个空箱格。快速加入只修改
内存中的箱子数据，不会直接穿戴，也不会自动保存；确认无误后仍需点击“保存修改”。武器写入
使用经 `ID_res.arc` 校验的真实类型和 ID，不使用 Dex 的全局行号。

### 实时 3D 武器模型

武器详情右侧可以实时浏览游戏模型；使用上下左右四向箭头旋转，每次旋转 15°并支持长按。
模型区域不响应鼠标拖动或滚轮，切换武器时会自动恢复初始视角。完整整合包已经把 558 个武器
ARC 放在程序旁的固定目录，解压后即可使用，
不需要选择 CCI、解包目录或执行资源导入：

```text
MH3USaveEditorGUI.exe
resources/mh3g/weapon-mod/v1/
├─ manifest.json
├─ w00/*.arc
├─ ...
└─ w12/*.arc
```

程序只读取这一固定相对路径，不修改资源文件。模型浏览不会把存档标记为已修改。没有资源、
单件解析失败或 OpenGL 3.3 不可用时，图鉴属性、路线、素材和快速加入仍可正常使用。

模型映射来自游戏 ExeFS 武器参数记录中的真实 `model_id`，完整覆盖 1,421 个武器形态到
558 个复用模型；不使用 Dex 行号、`WpnImg_*` 顺序或目录偏移猜测。渲染器按 MOD Primitive
的真实材质索引读取 MRL，并依据 MRL 的 BlendState 决定材质是否透明。武器 `_BM` 主贴图的
Alpha 对绝大多数不透明材质是高光/反射遮罩，只有 MRL 明确标记为透明的材质才会把它用于
透明混合。渲染器同时使用 MRL 材质常量控制颜色、粗糙度与反射，并在线性空间完成照明、曝光
和 Gamma 转换。当前武器 ARC 没有独立法线或高光 TEX，相关材质槽已保留供后续防具等资源
使用。游戏专用粒子、发光和动态机关仍使用静态近似。

游戏资源不进入 Git 仓库，而是独立发布为 `MH3GResources-v1.zip` Release Asset。它与程序
完全分离，内部只有上述 `resources/` 树；以后防具模型、道具图片等也沿用这套资源包结构。
本地可从已解包的武器资源确定性生成该 Asset：

```bash
python3 tools/build_resource_pack.py \
  --source /path/to/romfs/arc/weapon/mod \
  --output MH3GResources-v1.zip
```

脚本会校验 ARC v0x10、MOD v0xE6、TEX v0xA5、MRL v0x20 并生成 manifest。Windows Action
先构建不含资源的程序，再下载 `mh3g-resources-v1` Release 下的这个 Asset，校验每个文件的
尺寸和 SHA-256，最后把程序和资源统一压缩为可直接使用的完整 portable 包。

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

The packaged Windows build is written to `release/windows/`. The script runs `windeployqt`, ensures the `platforms/qwindows.dll` and `sqldrivers/qsqlite.dll` plugins and `data/encyclopedia.sqlite` are present, and then copies additional MinGW/MSYS2 runtime DLLs detected by `objdump`.

## Save-format regression test

Pass known-good 3DS and Wii U character files to the test runner:

```bash
./tests/run-save-format-tests.sh /path/to/3ds/user1 /path/to/wiiu/user1
```

The test verifies byte-identical unchanged round trips, read/write byte-order conversion for money, Moga Points, items, equipment, and jewels, and item/equipment transfers in both directions. It also verifies encyclopedia previews and quick adds on both formats: only the chosen empty slot may change, full boxes and invalid IDs must remain byte-identical, and the source file cannot change before the normal save command. Test inputs are never overwritten.

模型解析器的合成 fixture 测试不需要游戏资源：

```bash
./tests/run-model-tests.sh
```

本地拥有解包资源时，可额外对全部 558 个 ARC 执行解析、纹理和导入往返测试；测试缓存位于
临时目录并会清除：

```bash
./tests/run-model-tests.sh /path/to/romfs/arc/weapon/mod
python3 tools/validate_model_crosswalk.py --resources /path/to/romfs/arc/weapon/mod
```
