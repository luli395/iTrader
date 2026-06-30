#!/usr/bin/env python3
"""Merge per-instrument AGTICK CSV folders into daily multi-symbol AGTICK files.

Input layout example:

    OIPYTICK/
      OITICK2526/OI_20250103.csv
      PTICK2526/P_20250103.csv
      YTICK2526/Y_20250103.csv

Output example:

    merged_daily/20250103_merged.csv

Each output file keeps the standard iTrader AGTICK header:

    time,symbol,current,high,low,volume,money,position,a1_v,a1_p,b1_v,b1_p
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Iterable


AGTICK_COLUMNS = [
    "time",
    "symbol",
    "current",
    "high",
    "low",
    "volume",
    "money",
    "position",
    "a1_v",
    "a1_p",
    "b1_v",
    "b1_p",
]

DATE_RE = re.compile(r"(?<!\d)((?:19|20)\d{6})(?!\d)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Merge per-symbol AGTICK CSV files into one sorted multi-symbol file per day."
    )
    parser.add_argument(
        "--source-dir",
        required=True,
        type=Path,
        help="Root directory containing per-symbol subfolders.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="Directory for merged daily files. Defaults to <source-dir>/merged_daily.",
    )
    parser.add_argument(
        "--name-template",
        default="{date}_merged.csv",
        help="Output filename template. Available field: {date}.",
    )
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Scan CSV files recursively under source-dir. By default only source-dir/*/*.csv is scanned.",
    )
    parser.add_argument(
        "--include-root",
        action="store_true",
        help="Also scan CSV files directly under source-dir.",
    )
    parser.add_argument(
        "--keep-time-format",
        action="store_true",
        help="Keep source timestamp strings instead of normalizing to YYYY-MM-DD HH:MM:SS.mmm.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing output files.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned merge work without writing files.",
    )
    return parser.parse_args()


def extract_date(path: Path) -> str | None:
    match = DATE_RE.search(path.name)
    if match is None:
        return None
    return match.group(1)


def is_relative_to(child: Path, parent: Path) -> bool:
    try:
        child.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def is_output_path(path: Path, source_dir: Path, output_dir: Path, name_template: str) -> bool:
    resolved_output_dir = output_dir.resolve()
    if source_dir.resolve() == resolved_output_dir:
        # Allow writing merged files directly in source-dir when raw files live in
        # per-symbol subfolders. Root-level CSVs and previously generated daily
        # files are treated as existing output.
        if path.resolve().parent == resolved_output_dir:
            return True
        date = extract_date(path)
        if date is not None and path.name == name_template.format(date=date):
            return True
        return False
    return is_relative_to(path, output_dir)


def iter_source_csv_files(
    source_dir: Path,
    output_dir: Path,
    name_template: str,
    recursive: bool,
    include_root: bool,
) -> Iterable[Path]:
    if recursive:
        candidates = source_dir.rglob("*.csv")
    else:
        candidates = []
        if include_root:
            candidates.extend(source_dir.glob("*.csv"))
        candidates.extend(source_dir.glob("*/*.csv"))

    for path in candidates:
        if not path.is_file():
            continue
        if is_output_path(path, source_dir, output_dir, name_template):
            continue
        if extract_date(path) is None:
            continue
        yield path


def fractional_millis(raw_fraction: str) -> int:
    digits = "".join(ch for ch in raw_fraction if ch.isdigit())[:3]
    if not digits:
        return 0
    return int(digits.ljust(3, "0"))


def normalize_timestamp(raw: str) -> tuple[int, str]:
    value = raw.strip()
    if not value:
        raise ValueError("empty timestamp")

    millis = 0
    base = value
    if "." in value:
        base, fraction = value.split(".", 1)
        millis = fractional_millis(fraction)

    digits = "".join(ch for ch in base if ch.isdigit())
    if len(digits) < 14:
        raise ValueError(f"unsupported timestamp: {raw}")

    digits = digits[:14]
    year = int(digits[0:4])
    month = int(digits[4:6])
    day = int(digits[6:8])
    hour = int(digits[8:10])
    minute = int(digits[10:12])
    second = int(digits[12:14])

    # Lexicographic YYYYMMDDHHMMSSmmm integer is enough for stable sorting.
    sort_key = int(f"{year:04d}{month:02d}{day:02d}{hour:02d}{minute:02d}{second:02d}{millis:03d}")
    normalized = f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}.{millis:03d}"
    return sort_key, normalized


def header_index(header: list[str], path: Path) -> dict[str, int]:
    normalized = [column.strip().lower() for column in header]
    index = {column: offset for offset, column in enumerate(normalized)}
    missing = [column for column in AGTICK_COLUMNS if column not in index]
    if missing:
        raise ValueError(f"{path} missing AGTICK columns: {', '.join(missing)}")
    return index


def read_rows(path: Path, normalize_time: bool) -> list[tuple[int, str, list[str]]]:
    rows: list[tuple[int, str, list[str]]] = []
    with path.open("r", encoding="utf-8-sig", newline="") as input_file:
        reader = csv.reader(input_file)
        try:
            header = next(reader)
        except StopIteration:
            return rows

        index = header_index(header, path)
        for line_number, fields in enumerate(reader, start=2):
            if not fields or all(not field.strip() for field in fields):
                continue
            if len(fields) < len(header):
                fields.extend([""] * (len(header) - len(fields)))

            output_row = [fields[index[column]].strip() for column in AGTICK_COLUMNS]
            try:
                sort_key, normalized_time = normalize_timestamp(output_row[0])
            except ValueError as exc:
                raise ValueError(f"{path}:{line_number}: {exc}") from exc

            if normalize_time:
                output_row[0] = normalized_time
            rows.append((sort_key, output_row[1], output_row))
    return rows


def write_daily_file(output_path: Path, rows: list[tuple[int, str, list[str]]], overwrite: bool) -> None:
    if output_path.exists() and not overwrite:
        raise FileExistsError(f"output exists, pass --overwrite to replace it: {output_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    with temp_path.open("w", encoding="utf-8", newline="") as output_file:
        writer = csv.writer(output_file, lineterminator="\n")
        writer.writerow(AGTICK_COLUMNS)
        for _, _, row in rows:
            writer.writerow(row)
    os.replace(temp_path, output_path)


def main() -> int:
    args = parse_args()
    source_dir = args.source_dir.resolve()
    output_dir = (args.output_dir or (source_dir / "merged_daily")).resolve()

    if not source_dir.exists():
        print(f"source dir does not exist: {source_dir}", file=sys.stderr)
        return 2

    files_by_date: dict[str, list[Path]] = defaultdict(list)
    for path in iter_source_csv_files(source_dir, output_dir, args.name_template, args.recursive, args.include_root):
        date = extract_date(path)
        if date is not None:
            files_by_date[date].append(path)

    if not files_by_date:
        print(f"no dated CSV files found under {source_dir}", file=sys.stderr)
        return 2

    total_rows = 0
    normalize_time = not args.keep_time_format
    for date in sorted(files_by_date):
        paths = sorted(files_by_date[date])
        output_path = output_dir / args.name_template.format(date=date)
        if args.dry_run:
            print(f"{date}: {len(paths)} source file(s) -> {output_path}")
            continue

        rows: list[tuple[int, str, list[str]]] = []
        for path in paths:
            rows.extend(read_rows(path, normalize_time))
        rows.sort(key=lambda item: (item[0], item[1]))
        write_daily_file(output_path, rows, args.overwrite)
        total_rows += len(rows)
        print(f"{date}: merged {len(rows)} row(s) from {len(paths)} file(s) -> {output_path}")

    if args.dry_run:
        print(f"planned {len(files_by_date)} daily output file(s)")
    else:
        print(f"done: wrote {len(files_by_date)} daily file(s), {total_rows} row(s) total")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
