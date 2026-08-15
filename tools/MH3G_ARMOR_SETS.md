# MH3G 防具套装表

防具图鉴以仓库内的两张 CSV 为权威数据，不在程序运行时按名称、稀有度或
`plXXX` 模型号推测套装：

- `mh3g_armor_sets.csv`：一行代表图鉴中的一套，负责档位、职业、模型和显示顺序。
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

- `set_id`：稳定主键。修改名称、模型或成员时不要改它。
- `rank`：`low`、`high`、`g` 或 `special`。
- `combat`：`blade`、`gunner` 或 `both`。
- `model_id`：资源包中的 `plXXX` 数字部分，可人工改正。
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

同一模型组内的独立单件要拆成特殊套装。例如三眼套、剑圣耳环和增弹耳环都使用
`pl115`，但正式表中是三条套装记录，不能仅因模型相同而合并。

每次调整后运行：

```bash
python3 tools/validate_armor_sets.py
```

校验会保证 1,651 个 Dex 防具恰好各出现一次、存档映射不重复、普通套装对其适用
性别具备五个部位，并拒绝同一性别下重复部位。
