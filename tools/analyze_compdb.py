#!/usr/bin/env python3

import argparse
import csv
from dataclasses import dataclass
import enum
import io
import json
from multiprocessing import Pool
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import typing
from typing import Any

from compdb import *


class Measurement(enum.Enum):
    SLOC = "sloc"
    LINES = "lines"
    PP_SLOC = "pp_sloc"
    PP_LINES = "pp_lines"
    PP_TIME = "pp_time"
    SEMA_TIME = "sema_time"
    INDEX_TIME = "index_time"

    def __str__(self):
        return self.value


class PathList:
    groups: List[List[Path]]

    def from_compdb(compdb: CompilationDatabase):
        paths = [
            entry.filepath
            if entry.filepath.is_absolute()
            else entry.directory.joinpath(entry.filepath)
            for entry in compdb.entries
        ]
        return PathList.from_paths(paths)

    def from_paths(paths: List[Path]):
        result = PathList()
        result.groups = [[]]
        last_group_size = 0
        for path in paths:
            if not path.exists():
                continue
            l = len(bytes(path))
            # The arglength limit on macOS is 1MiB, be more conservative
            if last_group_size + l > 900 * 1024:
                p.groups.append([path])
                last_group_size = l
            else:
                result.groups[-1].append(path)
                last_group_size += l
        assert (
            len(result.groups[0]) > 0
        )  # all files in compilation database do not exist
        return result


# ASSUMPTION: There aren't multiple compilation commands in the compilation
# database which index the same file in two different ways.
# Otherwise, we'd need to plumb through indexes properly rather than
# working with a Path key.
def compute_sloc(pathlist: PathList) -> dict[str, int]:
    sloc_dict = {}
    for group in pathlist.groups:
        # Tokei runs in parallel, so avoid extra process spawning overhead.
        cmd = ["tokei", "--files", "--output", "json"] + [str(s) for s in group]
        result = subprocess.run(cmd, capture_output=True, encoding="utf8")
        for _lang, lang_data in json.loads(result.stdout).items():
            for entry in lang_data["reports"]:
                sloc_dict[entry["name"]] = entry["stats"]["code"]
    return sloc_dict


def compute_lines_impl(path: Path) -> int:
    result = subprocess.run(
        ["wc", "-l", str(path)], capture_output=True, encoding="utf8"
    )
    # Q: Should we pass in the regex from outside?
    match = re.search(r"\s*(\d+)\s+(.*)\n", result.stdout)
    return int(match.group(1))


def compute_lines(pool: Pool, pathlist: PathList) -> dict[str, int]:
    paths = [path for group in pathlist.groups for path in group]
    lines = pool.map(compute_lines_impl, paths)
    return {str(p): lines[i] for (i, p) in enumerate(paths)}


A = typing.TypeVar("A")
B = typing.TypeVar("B")


def transpose(list_of_tuples: List[tuple[A, B]]) -> tuple[List[A], List[B]]:
    return tuple([list(x) for x in zip(*list_of_tuples)])


def compute_pp_time_impl(entry: CompilationDatabaseEntry) -> tuple[Path, float]:
    handle, tmp_path = tempfile.mkstemp(prefix=entry.filepath.stem, suffix=".pp.cpp")
    os.close(handle)
    start = time.monotonic()
    entry.run_preprocessor_only(tmp_path)
    delta = time.monotonic() - start
    return (Path(tmp_path), delta)


#
def compute_pp_time(
    pool: Pool, entries: List[CompilationDatabaseEntry]
) -> tuple[List[Path], dict[str, str], dict[str, List[float]]]:
    list_of_tuples = pool.map(compute_pp_time_impl, entries)
    tmp_files, timings = transpose(list_of_tuples)
    path_to_tmp_dict = {
        str(entries[i].filepath): str(p) for (i, p) in enumerate(tmp_files)
    }
    timing_dict = {
        str(entries[i].filepath): round(t, 3) for (i, t) in enumerate(timings)
    }
    return (tmp_files, path_to_tmp_dict, timing_dict)


def compute_sema_time_impl(entry: CompilationDatabaseEntry) -> float:
    start = time.monotonic()
    entry.run_sema_only()
    return time.monotonic() - start


def compute_sema_time(
    pool: Pool, entries: List[CompilationDatabaseEntry]
) -> List[float]:
    timings = pool.map(compute_sema_time_impl, entries)
    return {str(entries[i].filepath): round(t, 3) for (i, t) in enumerate(timings)}


def compute_index_time(compdb_path: Path) -> dict[str, float]:
    with tempfile.NamedTemporaryFile(
        mode="r", encoding="utf8", suffix="stats.json"
    ) as stats_file:
        cmd = [
            "scip-clang",
            f"--compdb-path={compdb_path}",
            f"--print-statistics-path={stats_file.name}",
        ]
        scip_clang_result = subprocess.run(
            cmd, capture_output=True, env=os.environ.copy()
        )
        if scip_clang_result.returncode != 0:
            print(scip_clang_result.stdout)
            print(scip_clang_result.stderr)
            scip_clang_result.check_returncode()
        stats = json.load(stats_file)
        return {x["filepath"]: float(x["stats"]["total_time_s"]) for x in stats}


def parse_arguments() -> tuple[Path, dict[str, bool]]:
    parser = argparse.ArgumentParser(
        prog="analyze_compdb",
        description="Analyze various statistics about translation units in a compilation database",
    )
    parser.add_argument(
        "--compdb-path",
        help="Path to compilation database",
        default="compile_commands.json",
    )
    cases = [str(m) for m in list(Measurement)]
    case_help_text = ", ".join(cases[:-1]) + " and " + cases[-1] + " (or 'all')"
    parser.add_argument(
        "--measure",
        help=f"One or more of {case_help_text}. Can be supplied multiple times",
        action="append",
    )
    args = parser.parse_args()
    requested_measurements = {str(c): False for c in cases}
    for a in args.measure:
        if a not in requested_measurements:
            if a == "all":
                requested_measurements = {k: True for k in requested_measurements}
            else:
                raise ValueError(f"Expected one of {case_help_text} but found {a}")
        else:
            requested_measurements[a] = True
    return (Path(args.compdb_path), requested_measurements)


@dataclass
class AnalysisResult:
    columns: List[str]
    result_table: List[List[Any]]

    def write_csv(self, writer):
        writer.writerow(["path"] + self.columns)
        writer.writerows(self.result_table)

    def to_dict(self) -> List[dict[str, Any]]:
        return [dict(zip(self.columns, row[1:])) for row in self.result_table]


# Returns two lists. The first list is a list of column names.
#
# The first column name is the path.
# Other columns correspond to the list of requested measurements.
#
# The second list contains one row per command object.
# The first element of the row is the path.
# The other elements are various measurements.
def analyze(
    compdb_path: Path, requested_measurements: dict[str, bool]
) -> AnalysisResult:
    compdb = CompilationDatabase.load(compdb_path)
    results = {}
    tu_main_file_pathlist = PathList.from_compdb(compdb)
    tmp_map = {}
    with Pool() as pool:
        if requested_measurements[str(Measurement.LINES)]:
            results[Measurement.LINES] = compute_lines(pool, tu_main_file_pathlist)
        if requested_measurements[str(Measurement.SLOC)]:
            results[Measurement.SLOC] = compute_sloc(tu_main_file_pathlist)
        pp_tmp_files = []
        if requested_measurements[str(Measurement.PP_TIME)]:
            pp_tmp_files, tmp_map, results[Measurement.PP_TIME] = compute_pp_time(
                pool, compdb.entries
            )
        if (
            requested_measurements[str(Measurement.PP_SLOC)]
            or requested_measurements[str(Measurement.PP_LINES)]
        ):
            if not pp_tmp_files:
                pp_tmp_files, tmp_map, _ = compute_pp_time(pool, compdb.entries)
            pp_tmp_pathlist = PathList.from_paths(pp_tmp_files)
            if requested_measurements[str(Measurement.PP_SLOC)]:
                results[Measurement.PP_SLOC] = compute_sloc(pp_tmp_pathlist)
            if requested_measurements[str(Measurement.PP_LINES)]:
                results[Measurement.PP_LINES] = compute_lines(pool, pp_tmp_pathlist)
        for pp_tmp_file in pp_tmp_files:
            pp_tmp_file.unlink()
        if requested_measurements[str(Measurement.SEMA_TIME)]:
            results[Measurement.SEMA_TIME] = compute_sema_time(pool, compdb.entries)
        if requested_measurements[str(Measurement.INDEX_TIME)]:
            results[Measurement.INDEX_TIME] = compute_index_time(compdb_path)

    results = {str(k): v for k, v in results.items()}

    columns = [m for m, requested in requested_measurements.items() if requested]

    result_table = []
    for group in tu_main_file_pathlist.groups:
        for path in group:
            result_table.append([str(path)])

    for col in columns:
        data = results[col]
        for i, row in enumerate(result_table):
            p = row[0]
            try:
                val = data[p]
            except KeyError:
                try:
                    tmp_path = tmp_map[p]
                    val = data[tmp_path]
                except KeyError:
                    val = -999
            row.append(val)

    return AnalysisResult(columns=columns, result_table=result_table)


def default_main():
    compdb_path, requested_measurements = parse_arguments()
    analysis_result = analyze(compdb_path, requested_measurements)
    writer = csv.writer(sys.stdout)
    analysis_result.write_csv(writer)


if __name__ == "__main__":
    default_main()
