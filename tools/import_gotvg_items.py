#!/usr/bin/env python3
"""Import high-confidence MH3G item Chinese names from gotvg.

The gotvg wiki uses Japanese item names as its primary key while this project
uses English item names. This script therefore treats gotvg as a trusted
Chinese-name vocabulary and only writes automatic updates when the English-side
rule translation is confirmed by an exact gotvg Chinese name.
"""

from __future__ import annotations

import csv
import html
import re
import sys
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_EN = ROOT / "data/en/items.txt"
DATA_CN = ROOT / "data/cn/items.txt"
DATA_SOURCES = ROOT / "data/cn/items_sources.txt"
DUMP_DIR = ROOT / "dump"
PAGE_DIR = DUMP_DIR / "gotvg_pages"
RAW_CSV = DUMP_DIR / "gotvg_items.csv"
CANDIDATES_CSV = DUMP_DIR / "gotvg_items_candidates.csv"

BASE_URL = "http://wiki.gotvg.com/mh3g/index.php?title="
SEED_TITLES = [
    "道具一",
    "道具二",
    "道具·素材篇",
]


@dataclass
class GotvgRow:
    page: str
    section: str
    japanese: str
    chinese: str
    fields: list[str]


class WikiTableParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.in_td = False
        self.in_tr = False
        self.current: list[str] = []
        self.buffer: list[str] = []
        self.rows: list[list[str]] = []
        self.current_section = ""
        self.section_for_row: list[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        attrs_dict = dict(attrs)
        if tag == "span" and attrs_dict.get("class") == "mw-headline":
            self.buffer = []
            self.in_td = False
        if tag == "tr":
            self.in_tr = True
            self.current = []
        if tag in ("td", "th") and self.in_tr:
            self.in_td = True
            self.buffer = []
        if tag == "br" and self.in_td:
            self.buffer.append(" ")

    def handle_endtag(self, tag: str) -> None:
        if tag in ("td", "th") and self.in_td:
            text = normalize_cell("".join(self.buffer))
            self.current.append(text)
            self.in_td = False
            self.buffer = []
        if tag == "tr" and self.in_tr:
            if self.current:
                self.rows.append(self.current)
                self.section_for_row.append(self.current_section)
            self.in_tr = False
        if tag == "span" and not self.in_td:
            section = normalize_cell("".join(self.buffer))
            if section:
                self.current_section = section

    def handle_data(self, data: str) -> None:
        if self.in_td or not self.in_tr:
            self.buffer.append(data)


def normalize_cell(text: str) -> str:
    return " ".join(html.unescape(text).replace("\xa0", " ").split()).strip()


def page_filename(title: str) -> Path:
    safe = urllib.parse.quote(title, safe="")
    return PAGE_DIR / f"{safe}.html"


def download_page(title: str) -> str | None:
    PAGE_DIR.mkdir(parents=True, exist_ok=True)
    path = page_filename(title)
    if path.exists() and path.stat().st_size > 10_000:
        return path.read_text(encoding="utf-8", errors="replace")

    # Bootstrap from the manual exploration cache when available.
    if title == "道具一":
        tmp_cache = Path("/tmp/mh3g-gotvg-items1.html")
        if tmp_cache.exists() and tmp_cache.stat().st_size > 10_000:
            text = tmp_cache.read_text(encoding="utf-8", errors="replace")
            path.write_text(text, encoding="utf-8")
            return text

    url = BASE_URL + urllib.parse.quote(title)
    last_error = None
    for attempt in range(5):
        try:
            request = urllib.request.Request(
                url,
                headers={"User-Agent": "mh3u-se gotvg item importer"},
            )
            with urllib.request.urlopen(request, timeout=30) as response:
                raw = response.read()
            text = raw.decode("utf-8", errors="replace")
            path.write_text(text, encoding="utf-8")
            return text
        except Exception as exc:  # Network is flaky on this host.
            last_error = exc
            time.sleep(2 + attempt)

    print(f"warning: failed to fetch {title}: {last_error}", file=sys.stderr)
    return None


def discover_titles(html_text: str) -> list[str]:
    titles = set()
    for encoded in re.findall(r"title=([^\"&<> ]+)", html_text):
        title = urllib.parse.unquote(encoded)
        if title.startswith("道具"):
            titles.add(title)
    return sorted(titles)


def parse_rows(title: str, html_text: str) -> list[GotvgRow]:
    parser = WikiTableParser()
    parser.feed(html_text)
    rows: list[GotvgRow] = []
    for row, section in zip(parser.rows, parser.section_for_row):
        if len(row) < 2:
            continue
        japanese, chinese = row[0], row[1]
        if japanese in ("道具名（日）", "道具名", ""):
            continue
        if chinese in ("道具名（中）", "道具名", ""):
            continue
        if len(japanese) > 40 or len(chinese) > 40:
            continue
        rows.append(GotvgRow(title, section, japanese, chinese, row))
    return rows


MONSTER_ALIASES = [
    ("Great Wroggi", "毒狗龙"), ("G. Wroggi", "毒狗龙"), ("Wroggi", "小毒狗龙"),
    ("Great Baggi", "眠狗龙"), ("G. Baggi", "眠狗龙"), ("Baggi", "小眠狗龙"),
    ("Great Jaggi", "狗龙"), ("G. Jaggi", "狗龙"), ("Jaggia", "雌狗龙"), ("Jaggi", "小狗龙"),
    ("Crimson Qurupeco", "红彩鸟"), ("C. Peco", "红彩鸟"), ("Qurupeco", "彩鸟"), ("Peco", "彩鸟"),
    ("Royal Ludroth", "水兽"), ("R. Ludroth", "水兽"), ("Purple Ludroth", "紫水兽"), ("P. Ludroth", "紫水兽"), ("Ludroth", "水生兽"),
    ("Jade Barroth", "冰碎龙"), ("J. Barroth", "冰碎龙"), ("Barroth", "土砂龙"),
    ("Steel Uragaan", "钢锤龙"), ("S. Uragaan", "钢锤龙"), ("Uragaan", "爆锤龙"),
    ("Rust Duramboros", "尾斧龙"), ("R. Duram", "尾斧龙"), ("Duramboros", "尾槌龙"), ("Duram", "尾槌龙"),
    ("Pink Rathian", "樱火龙"), ("P. Rathian", "樱火龙"), ("P. Rath", "樱火龙"),
    ("Gold Rathian", "金火龙"), ("G. Rathian", "金火龙"), ("G. Rath", "金火龙"), ("Rathian", "雌火龙"),
    ("Azure Rathalos", "苍火龙"), ("A. Rathalos", "苍火龙"), ("A. Rath", "苍火龙"),
    ("Silver Rathalos", "银火龙"), ("S. Rathalos", "银火龙"), ("S. Rath", "银火龙"), ("Rathalos", "雄火龙"), ("Rath", "火龙"),
    ("Black Diablos", "黑角龙"), ("B. Diablos", "黑角龙"), ("Diablos", "角龙"),
    ("Baleful Gigginox", "电怪龙"), ("B. Giggi", "电怪龙"), ("Gigginox", "毒怪龙"), ("Giggi", "毒龙幼崽"),
    ("Sand Barioth", "风牙龙"), ("S. Barioth", "风牙龙"), ("Barioth", "冰牙龙"),
    ("Green Plesioth", "翠水龙"), ("G. Plesio", "翠水龙"), ("Plesioth", "水龙"), ("Plesio", "水龙"),
    ("Ivory Lagiacrus", "白海龙"), ("I. Lagi", "白海龙"), ("Abyssal Lagiacrus", "冥海龙"), ("A. Lagi", "冥海龙"),
    ("Lagiacrus", "海龙"), ("Lagia", "海龙"), ("Lagi", "海龙"),
    ("Glacial Agnaktor", "冻戈龙"), ("G. Agnak", "冻戈龙"), ("Agnaktor", "炎戈龙"), ("Agnak", "炎戈龙"),
    ("Green Nargacuga", "绿迅龙"), ("G. Narga", "绿迅龙"),
    ("Lucent Nargacuga", "月迅龙"), ("L. Narga", "月迅龙"), ("L. Narg", "月迅龙"), ("L. Narha", "月迅龙"),
    ("Nargacuga", "迅龙"), ("Narga", "迅龙"), ("Narg", "迅龙"),
    ("Stygian Zinogre", "狱狼龙"), ("S. Zinogre", "狱狼龙"), ("S. Zin", "狱狼龙"), ("Zinogre", "雷狼龙"), ("Zin", "雷狼龙"),
    ("Goldbeard Ceadeus", "皇海龙"), ("G. Ceadeus", "皇海龙"), ("Ceadeus", "大海龙"),
    ("Savage Deviljho", "怒食恐暴龙"), ("S. Deviljho", "怒食恐暴龙"), ("Deviljho", "恐暴龙"),
    ("Hallowed Jhen Mohran", "灵山龙"), ("Hallowed Jhen", "灵山龙"), ("Jhen Mohran", "峯山龙"), ("Mohran", "峯山龙"),
    ("Dire Miralis", "炼黑龙"), ("Miralis", "炼黑龙"), ("Alatreon", "煌黑龙"),
    ("Brachydios", "碎龙"), ("Gobul", "灯鱼龙"), ("Nibelsnarf", "潜口龙"), ("Lagombi", "白兔兽"), ("Volvidon", "赤甲兽"), ("Arzuros", "青熊兽"),
    ("Uroktor", "熔岩兽"), ("Delex", "砂鱼龙"), ("Rhenoplos", "草食龙"), ("Rheno", "草食龙"),
    ("Gargwa", "丸鸟"), ("Bullfango", "小野猪"), ("Slagtoth", "垂皮龙"), ("Anteka", "长毛鹿"),
    ("Bnahabra", "飞甲虫"), ("Bnaha", "飞甲虫"), ("Altaroth", "甲虫"), ("Aptonoth", "食草龙"),
    ("Kelbi", "羚鹿"), ("Epioth", "水蜥兽"), ("Popo", "猛犸"),
]
MONSTER_ALIASES.sort(key=lambda pair: len(pair[0]), reverse=True)

MATERIALS = [
    ("Electrofur +", "雷电毛+"), ("Eletrofur +", "雷电毛+"), ("Electrofur", "雷电毛"), ("Eletrofur", "雷电毛"),
    ("D-Shocker", "雷电壳"), ("Shocker +", "带电毛+"), ("Shocker", "带电毛"), ("Dynamo", "雷电核"),
    ("Dragonhair", "龙毛"), ("Dragnshell", "龙壳"), ("Skymerald", "天玉"), ("Sapphire", "苍玉"), ("Jasper", "碧玉"),
    ("Kingwhiskr", "王须"), ("Whisker", "须"), ("Lantern +", "灯笼+"), ("Lantern", "灯笼"), ("Beacon", "灯笼"),
    ("Hardhorn", "坚角"), ("Horn +", "角+"), ("Horn", "角"), ("Tusks +", "牙+"), ("Tusks", "牙"),
    ("Tailspike", "尾棘"), ("Tailspear", "尾枪"), ("Tailcase +", "尾甲+"), ("Tailcase", "尾甲"), ("Tailbone", "尾骨"), ("Tail", "尾巴"),
    ("Scale +", "鳞+"), ("Scale+", "鳞+"), ("Scale", "鳞"), ("Shard", "厚鳞"),
    ("Shell +", "甲壳+"), ("Shell+", "甲壳+"), ("Shell", "甲壳"), ("Carapace", "坚壳"), ("Cortex", "重壳"),
    ("Hide +", "皮+"), ("Hide+", "皮+"), ("Piel", "皮"), ("Hide", "皮"), ("Pelt +", "毛皮+"), ("Pelt+", "毛皮+"), ("Pelt", "毛皮"), ("Fur", "毛皮"),
    ("Claw +", "爪+"), ("Claw+", "爪+"), ("Claw", "爪"), ("Talon", "钩爪"),
    ("Fang +", "牙+"), ("Fang+", "牙+"), ("Fang", "牙"),
    ("Spike +", "棘+"), ("Spike+", "棘+"), ("Surspike", "秘棘"), ("Spike", "棘"),
    ("Razor +", "刃翼+"), ("Razor", "刃翼"), ("Dapples", "斑纹"), ("Medulla", "延髓"), ("Marrow", "骨髓"),
    ("Mantle", "天鳞"), ("Plate", "逆鳞"), ("Ruby", "红玉"), ("Webbing", "翼膜"), ("Fellwing", "刚翼"), ("Wing", "翼"),
    ("Lash", "韧尾"), ("Ripper", "裂爪"), ("Gleem", "煌液"), ("Scalp", "头壳"), ("Head", "头"),
    ("Jaw", "颚"), ("Stomach", "胃袋"), ("Fin", "鳍"), ("Fluid", "体液"), ("Blood", "血"),
]
MATERIALS.sort(key=lambda pair: len(pair[0]), reverse=True)

EXACT_MAP = {
    "Book of Combos 1": "调合书1 入门篇",
    "Book of Combos 2": "调合书2 初级篇",
    "Book of Combos 3": "调合书3 中级篇",
    "Book of Combos 4": "调合书4 上级篇",
    "Book of Combos 5": "调合书5 达人篇",
    "Potion": "回复药",
    "Mega Potion": "回复药G",
    "Nutrients": "营养剂",
    "Mega Nutrients": "营养剂G",
    "Antidote": "解毒药",
    "Dash Juice": "强走药",
    "Mega Dash Juice": "强走药G",
    "Demondrug": "鬼人药",
    "Mega Demondrug": "鬼人药G",
    "Armorskin": "硬化药",
    "Mega Armorskin": "硬化药G",
    "Cool Drink": "冷饮",
    "Hot Drink": "热饮",
    "Raw Meat": "生肉",
    "Rare Steak": "烤熟的肉",
    "Burnt Meat": "焦肉",
    "Honey": "蜂蜜",
    "Bomb Sac": "爆弹袋",
    "Screamer": "鸣袋",
    "Poison Sac": "毒袋",
    "Toxin Sac": "猛毒袋",
    "Paralysis Sac": "麻痹袋",
    "Omniplegia Sac": "强力麻痹袋",
    "Sleep Sac": "睡眠袋",
    "Coma Sec": "昏睡袋",
    "Flame Sac": "火炎袋",
    "Inferno Sac": "爆炎袋",
    "Conflagrant Sac": "业炎袋",
    "Aqua Sac": "水袋",
    "Torrent Sac": "大水袋",
    "Thunder Sac": "电击袋",
    "Lightning Sac": "雷电袋",
    "Frost Sac": "冰结袋",
    "Freezer Sac": "冻结袋",
    "Dash Extract": "狂走浸出物",
    "Pale Extract": "白色浸出物",
    "Wyvern Fang": "飞龙牙",
    "Wyvern Claw": "飞龙爪",
    "Bird Wyvern Fang": "鸟龙种牙",
    "ElderDragonBlood": "古龙之血",
    "Monster Bone S": "龙骨【小】",
    "Monster Bone M": "龙骨【中】",
    "Monster Bone L": "龙骨【大】",
    "Monster Bone +": "上龙骨",
    "Monster Keenbone": "尖龙骨",
    "Monster hardbone": "坚龙骨",
    "Monster slogbne": "重龙骨",
    "Mystery Bone": "迷之骨",
    "Unknown Skull": "未知头骨",
    "Bone": "骨",
    "Dragonbone Relic": "古龙骨",
    "Razor Jewel 1": "匠珠【1】",
    "Razor Jewel 3": "匠珠【3】",
    "Charmer Jewel 1": "护石珠【1】",
    "KO Jewel 2": "KO珠【2】",
    "Dust of Life": "生命粉尘",
    "Dracophage Bug": "食龙虫",
}


def display_name(line: str) -> str:
    if " (" in line and line.endswith(")"):
        return line.rsplit(" (", 1)[0].strip()
    return line.strip()


def has_suspicious_latin(text: str) -> bool:
    normalized = re.sub(r"LV[0-9]+", "", text)
    normalized = re.sub(r"(^|[^A-Za-z])KO([^A-Za-z]|$)", " ", normalized)
    normalized = re.sub(r"[GSL]$", "", normalized)
    return bool(re.search(r"[A-Za-z]", normalized))


def translate_material(rest: str) -> str | None:
    rest = rest.strip()
    quality = ""
    for prefix, zh in (("Hvy ", "重"), ("Heavy ", "重")):
        if rest.startswith(prefix):
            quality = zh
            rest = rest[len(prefix):].strip()
            break
    for english, zh in MATERIALS:
        if rest == english:
            return quality + zh
    return None


def translate_english_item(english: str) -> str | None:
    if not english:
        return ""
    if english.startswith("DUMMY "):
        return "占位 " + english.split(" ", 1)[1]
    if english in EXACT_MAP:
        return EXACT_MAP[english]

    working = english
    leading_quality = ""
    for prefix, zh in (("Hvy ", "重"), ("Heavy ", "重")):
        if working.startswith(prefix):
            leading_quality = zh
            working = working[len(prefix):].strip()
            break

    for alias, monster_zh in MONSTER_ALIASES:
        if working == alias:
            return monster_zh
        if working.startswith(alias + " "):
            material = translate_material(working[len(alias):].strip())
            if material:
                if leading_quality and not material.startswith(leading_quality):
                    material = leading_quality + material
                return monster_zh + material
    return None


def write_raw_csv(rows: list[GotvgRow]) -> None:
    DUMP_DIR.mkdir(parents=True, exist_ok=True)
    with RAW_CSV.open("w", encoding="utf-8", newline="") as fp:
        writer = csv.writer(fp)
        writer.writerow(["page", "section", "japanese", "chinese", "fields"])
        for row in rows:
            writer.writerow([row.page, row.section, row.japanese, row.chinese, " | ".join(row.fields)])


def main() -> int:
    titles = list(SEED_TITLES)
    pages: dict[str, str] = {}
    for title in list(titles):
        text = download_page(title)
        if not text:
            continue
        pages[title] = text
        for discovered in discover_titles(text):
            if discovered not in titles and discovered in SEED_TITLES:
                titles.append(discovered)

    all_rows: list[GotvgRow] = []
    for title, text in pages.items():
        all_rows.extend(parse_rows(title, text))
    write_raw_csv(all_rows)

    gotvg_names = {row.chinese for row in all_rows if row.chinese}
    gotvg_by_name: dict[str, list[GotvgRow]] = {}
    for row in all_rows:
        gotvg_by_name.setdefault(row.chinese, []).append(row)

    english_lines = DATA_EN.read_text(encoding="utf-8").splitlines()
    current_lines = DATA_CN.read_text(encoding="utf-8").splitlines()
    if len(english_lines) != len(current_lines):
        raise RuntimeError(f"line count mismatch: en={len(english_lines)} cn={len(current_lines)}")

    new_lines: list[str] = []
    source_lines: list[str] = []
    candidate_rows: list[list[str]] = []
    changed = confirmed = high = rule = review = placeholder = 0

    for index, (english, current) in enumerate(zip(english_lines, current_lines)):
        current_display = display_name(current)
        generated = translate_english_item(english)
        source = "need-review"
        output_display = current_display

        if not english:
            source = ""
            output_display = current_display
        elif english.startswith("DUMMY ") or current_display.startswith("占位"):
            source = "placeholder"
            placeholder += 1
        elif current_display in gotvg_names:
            source = "gotvg-confirmed"
            confirmed += 1
        elif generated and generated in gotvg_names:
            source = "gotvg-high"
            output_display = generated
            high += 1
        elif generated:
            source = "rule"
            rule += 1
        else:
            review += 1

        if source in ("need-review", "rule") or (generated and generated not in gotvg_names):
            matches = gotvg_by_name.get(generated or "", [])
            candidate_rows.append([
                index,
                english,
                current_display,
                generated or "",
                source,
                "; ".join(f"{r.page}/{r.section}/{r.japanese}" for r in matches[:5]),
            ])

        if output_display != current_display:
            changed += 1
        if english:
            new_lines.append(f"{output_display} ({english})")
        else:
            new_lines.append(output_display)
        source_lines.append(source)

    DATA_CN.write_text("\n".join(new_lines) + "\n", encoding="utf-8")
    DATA_SOURCES.write_text("\n".join(source_lines) + "\n", encoding="utf-8")

    with CANDIDATES_CSV.open("w", encoding="utf-8", newline="") as fp:
        writer = csv.writer(fp)
        writer.writerow(["id", "english", "current_cn", "rule_cn", "source", "gotvg_matches"])
        writer.writerows(candidate_rows)

    print(
        "gotvg_pages={pages} gotvg_rows={rows} changed={changed} "
        "gotvg_high={high} gotvg_confirmed={confirmed} rule={rule} "
        "need_review={review} placeholder={placeholder}".format(
            pages=len(pages),
            rows=len(all_rows),
            changed=changed,
            high=high,
            confirmed=confirmed,
            rule=rule,
            review=review,
            placeholder=placeholder,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
