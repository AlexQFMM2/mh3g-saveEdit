# MH3G SQLite 游戏数据

运行时只读取 `mh3g.sqlite`。旧 `data/cn`、`data/en` 静态 CSV 已移除；道具箱和装备箱的用户交换 CSV 不受影响。

## ID 权威与边界

- 道具、五个防具部位和十二类武器的存档 ID 来自 `ID_res.arc` 完整数组下标，源文件 SHA-256 为 `81e316bdfb0e65c1b05f1b375265aaff2cc3a1dc4c8cb5b8be23f0abd9b73087`。
- Dex 只提供名称、属性、技能和生成关系；Dex 主键不会直接写入存档。
- 1,421 把自然武器已全部一对一映射。1,600 件 Dex 防具有明确存档映射；另有 52 件存档本地防具从审核过的 MH3G ExeFS 原生表取得参数，但缺少 Wii U 平台证据，在 Wii U 存档中显示黄色未确认。原生记录为空的条目继续保留为 `unknown`。
- Dex 的 230 条装饰珠记录存在同名重复。只有能唯一对应存档本地珠子表的条目被标记为确认，歧义条目保留为 `unknown`，不会用行号或偏移猜测。
- 护石合法性来自 261,448 条原生生成记录，去重为 121,952 种全游戏自然组合；不绑定当前角色的护石表。技能点使用有符号 8 位语义。

`manifest.json` 记录数据库哈希、原始 Dex 导出哈希、crosswalk 哈希、行数和每张表的稳定逻辑哈希。数据库以 `PRAGMA user_version=1` 发布，运行时只读打开，并执行格式和完整性检查。

配装器通过 Repository 使用分页候选接口，不在 UI 中直接执行 SQL。`idx_armor_skill_filter`、
`idx_charm_skill1_filter` 和 `idx_charm_skill2_filter` 分别服务防具以及护石两个技能位置的多条件
AND 查询；护石查询不要求技能处在第一或第二字段。

## 离线生成与验证

原始 Dex 导出、CCI、ExeFS 和存档样本不提交仓库。防具原生参数的离线导出器固定校验 `.code` SHA-256、五张表地址、24 字节记录长度，并用 1,600 件已映射防具逐字段核对基础防御、职业、性别、稀有度、孔位、耐性和技能点：

```bash
python3 tools/export_armor_native_parameters.py \
  --code /path/to/audited.code \
  --dex-dump /tmp/mh3g-dex-rebuild
```

审核过的 24 字节记录没有已确认的“强化次数上限”字段，因此数据库保留最大防御，但 `max_upgrade_level` 维持 `NULL`。修改器不编辑或检测防具强化等级，已有强化值原样保留，交由游戏内系统处理。

持有审核过的 MH3G Dex 运行时导出后执行：

```bash
python3 tools/build_sqlite_data.py --dex-dump /tmp/mh3g-dex-rebuild
python3 tools/validate_data.py data
./tests/run-equipment-validator-tests.sh
```

固定 Python/SQLite 环境下连续生成两次，`mh3g.sqlite` 与 `manifest.json` 应逐字节一致。
