#!/usr/bin/env python3
"""Convert Rime YAML to TSV and precompile TSV dictionaries to binary."""

import argparse
from pathlib import Path
import struct


MAGIC = b"BGDICT\0\0"
FORMAT_VERSION = 1
HEADER_FORMAT = "<8s14I"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
INDEX_FORMAT = "<IIIIq"
INDEX_SIZE = struct.calcsize(INDEX_FORMAT)
UINT32_MAX = (1 << 32) - 1
INT64_MIN = -(1 << 63)
INT64_MAX = (1 << 63) - 1


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


def read_tsv(source: Path, scheme: str) -> dict[tuple[str, str], int]:
    entries: dict[tuple[str, str], int] = {}
    with source.open("r", encoding="utf-8-sig", newline="") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\r\n")
            if not line:
                continue
            fields = line.split("\t")
            if len(fields) != 3:
                raise ValueError(f"{source}:{line_number}: expected code, text and weight")
            code, text, weight_text = fields
            if (not code.isascii() or not code.isalpha() or not code.islower() or
                    not text or (scheme == "pinyin" and len(code) > 32) or
                    (scheme == "wubi86" and len(code) > 4) or
                    (scheme == "english" and (len(code) > 32 or text != code))):
                raise ValueError(f"{source}:{line_number}: invalid code or text")
            try:
                weight = int(weight_text)
            except ValueError as error:
                raise ValueError(f"{source}:{line_number}: invalid weight") from error
            if not INT64_MIN <= weight <= INT64_MAX:
                raise ValueError(f"{source}:{line_number}: weight outside int64")
            key = (code, text)
            entries[key] = max(weight, entries.get(key, INT64_MIN))
    return entries


def write_bin(path: Path, entries: dict[tuple[str, str], int]) -> None:
    ordered = sorted(
        entries.items(),
        key=lambda item: (item[0][0].encode("ascii"), -item[1], item[0][1].encode("utf-8")),
    )
    if len(ordered) > UINT32_MAX:
        raise ValueError("too many dictionary entries")

    index = bytearray()
    code_blob = bytearray()
    text_blob = bytearray()
    for (code, text), weight in ordered:
        if not INT64_MIN <= weight <= INT64_MAX:
            raise ValueError("dictionary weight outside int64")
        code_bytes = code.encode("ascii")
        text_bytes = text.encode("utf-8")
        if (len(code_blob) > UINT32_MAX or len(text_blob) > UINT32_MAX or
                len(code_bytes) > UINT32_MAX or len(text_bytes) > UINT32_MAX):
            raise ValueError("dictionary blob exceeds uint32 offsets")
        index.extend(struct.pack(
            INDEX_FORMAT,
            len(code_blob),
            len(code_bytes),
            len(text_blob),
            len(text_bytes),
            weight,
        ))
        code_blob.extend(code_bytes)
        text_blob.extend(text_bytes)

    index_offset = HEADER_SIZE
    code_offset = index_offset + len(index)
    text_offset = code_offset + len(code_blob)
    if text_offset + len(text_blob) > UINT32_MAX:
        raise ValueError("dictionary exceeds uint32 offsets")
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        len(ordered),
        INDEX_SIZE,
        index_offset,
        len(index),
        code_offset,
        len(code_blob),
        text_offset,
        len(text_blob),
        0,
        0,
        0,
        0,
    )
    path.write_bytes(header + index + code_blob + text_blob)


def emit_bin(input_dir: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for scheme, input_name, output_name in (
        ("pinyin", "pinyin.tsv", "pinyin.bin"),
        ("wubi86", "wubi86.tsv", "wubi86.bin"),
        ("english", "english.tsv", "english.bin"),
    ):
        entries = read_tsv(input_dir / input_name, scheme)
        output = output_dir / output_name
        write_bin(output, entries)
        print(f"{scheme}: {len(entries)} entries, {output.stat().st_size} bytes -> {output}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emit-bin", action="store_true", help="compile existing TSV files")
    parser.add_argument("--input-dir", type=Path, help="directory containing dictionary TSV files")
    parser.add_argument("--pinyin", type=Path, help="rime-pinyin-simp YAML")
    parser.add_argument("--wubi", type=Path, help="rime-wubi YAML")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if args.emit_bin:
        if args.input_dir is None:
            parser.error("--emit-bin requires --input-dir")
        if args.pinyin is not None or args.wubi is not None:
            parser.error("--emit-bin reads TSV and cannot be combined with YAML inputs")
        emit_bin(args.input_dir, args.output_dir)
        return

    if args.input_dir is not None or args.pinyin is None or args.wubi is None:
        parser.error("YAML conversion requires --pinyin and --wubi without --input-dir")
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
