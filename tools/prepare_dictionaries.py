#!/usr/bin/env python3
"""Convert fixed Rime YAML dictionaries to the embedded TV1 TSV format."""

import argparse
from pathlib import Path


def convert(source: Path, scheme: str) -> dict[tuple[str, str], int]:
    entries: dict[tuple[str, str], int] = {}
    in_data = False
    with source.open("r", encoding="utf-8-sig") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\r\n")
            if not in_data:
                if line.strip() == "...":
                    in_data = True
                continue
            if not line or line.startswith("#"):
                continue

            fields = line.split("\t")
            if len(fields) < 2:
                raise ValueError(f"{source}:{line_number}: expected tab-separated columns")
            text = fields[0].strip()
            if scheme == "pinyin":
                code = "".join(fields[1].strip().lower().split())
            else:
                code = fields[1].strip().lower()
            if not text or not code.isascii() or not code.isalpha() or not code.islower():
                continue
            if scheme == "wubi86" and len(code) > 4:
                continue
            if scheme == "pinyin" and len(code) > 32:
                continue

            weight = 0
            if len(fields) >= 3 and fields[2].strip():
                try:
                    weight = int(float(fields[2].strip()))
                except ValueError as error:
                    raise ValueError(f"{source}:{line_number}: invalid weight") from error
            key = (code, text)
            entries[key] = max(weight, entries.get(key, -1))
    if not in_data:
        raise ValueError(f"{source}: missing Rime data separator")
    return entries


def write_tsv(path: Path, entries: dict[tuple[str, str], int]) -> None:
    ordered = sorted(entries.items(), key=lambda item: (item[0][0], -item[1], item[0][1]))
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for (code, text), weight in ordered:
            stream.write(f"{code}\t{text}\t{weight}\n")


def validate(entries: dict[tuple[str, str], int], scheme: str) -> None:
    required = {
        "pinyin": {
            "wen": {"文", "问"},
            "jiang": {"江", "讲", "将"},
            "nihao": {"你好"},
        },
        "wubi86": {
            "a": {"工"},
            "wq": {"你"},
            "ggtt": {"五笔"},
        },
    }[scheme]
    for code, expected in required.items():
        actual = {text for (entry_code, text) in entries if entry_code == code}
        missing = expected - actual
        if missing:
            raise ValueError(f"{scheme}:{code}: missing required candidates {sorted(missing)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pinyin", type=Path, required=True, help="rime-pinyin-simp YAML")
    parser.add_argument("--wubi", type=Path, required=True, help="rime-wubi YAML")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for scheme, source, output_name in (
        ("pinyin", args.pinyin, "pinyin.tsv"),
        ("wubi86", args.wubi, "wubi86.tsv"),
    ):
        entries = convert(source, scheme)
        validate(entries, scheme)
        write_tsv(args.output_dir / output_name, entries)
        print(f"{scheme}: {len(entries)} entries -> {args.output_dir / output_name}")


if __name__ == "__main__":
    main()
