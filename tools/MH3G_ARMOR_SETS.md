# MH3G 防具套装表

防具图鉴以仓库内的两张 CSV 作为“套装分组与显示顺序”的权威数据，不在程序
运行时按名称、稀有度或 `plXXX` 模型号推测套装：

- `mh3g_armor_sets.csv`：一行代表图鉴中的一套，负责档位、职业、名称和显示顺序。
- `mh3g_armor_set_members.csv`：把每个 Dex 防具实体明确放入某套，并记录部位、性别和真实存档 ID。

`suggest_armor_sets.py` 只用于从 Dex 和模型引用生成第一版候选。不要把它加入
Action，也不要在日常构建中覆盖正式表。需要重新分析时先输出到临时文件，再把
确认过的差异人工合并到正式表：

```bash
python3 tools/suggest_armor_sets.py \
  --dex-dump /tmp/mh3g-armor-dex \
  --data data \
  --sets /tmp/mh3g_armor_sets.suggested.csv \
  --members /tmp/mh3g_armor_set_members.suggested.csv
```

## 套装字段

- `set_id`：稳定主键。修改名称或成员时不要改它。
- `rank`：`low`、`high`、`g` 或 `special`。
- `combat`：`blade`、`gunner` 或 `both`。
- `name_cn/name_en`：套装显示名。
- `display_order`：全局稳定顺序，必须从 0 连续排列。
- `review_status`：自动初稿为 `candidate`，人工核对后改为 `reviewed`。
- `source/notes`：记录判定依据和特殊情况。

## 成员字段

- `part`：`head/chest/arms/waist/legs`。
- `dex_id`：属性、技能点和生产素材使用的 Dex 实体 ID。
- `gender`：`both/male/female`；男女名称不同时允许同一套包含十行。
- `slot_order`：套内稳定顺序，从 0 连续排列。
- `save_type/save_id`：加入装备箱时写入的真实存档类型和 ID。未确认时 `save_id`
  留空并设 `mapping_source=unmapped`，界面仍展示资料但禁用快速加入。

套装归属和模型绑定是两层独立数据。即使多个条目复用模型，也不能仅因模型相同
而合并套装；反过来，同一套中的每一件也必须读取自己的游戏参数记录。三眼套和
增弹耳环的游戏模型字段实际为 0，代表没有普通独立防具模型，不能再按旧表绑定到
`pl115`；完整人物试衣间会使用该部位的 `pl000` 基础组件补全人物。

真实防具模型映射位于 ExeFS 的五张 24 字节参数表，记录的 byte 2/3 分别是男性和
女性 `plXXX` 编号。证据、地址、验证结果及迁移步骤见
[`MH3G_ARMOR_MODEL_RESEARCH.md`](MH3G_ARMOR_MODEL_RESEARCH.md)。数据库 v3 已完成迁移，
套装 CSV 不再包含或驱动任何模型编号。

每次调整后运行：

```bash
python3 tools/validate_armor_sets.py
```

校验会保证 1,651 个 Dex 防具恰好各出现一次、存档映射不重复、普通套装对其适用
性别具备五个部位，并拒绝同一性别下重复部位。
