#!/usr/bin/env python3

import argparse
from dataclasses import dataclass
import enum
import json
from multiprocessing import Pool
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import typing
from typing import Any

import analyze_compdb
from compdb import *


class Indexer(enum.Enum):
    SCIP_CLANG = "scip-clang"
    LSIF_CLANG = "lsif-clang"

    def __str__(self):
        return self.value


class CommandLineArgs:
    chunk_count: int
    compdb_path: Path
    templates: List[tuple[Indexer, List[str]]]

    def __init__(self, compdb_path: str, chunk_count: int, templates: List[str]):
        self.compdb_path = Path(compdb_path)
        self.chunk_count = chunk_count
        assert len(templates) > 0, "Pass --bench one or more times"
        self.templates = []
        for template in templates:
            args = shlex.split(template)
            if "scip-clang" in args[0]:
                indexer = Indexer.SCIP_CLANG
            elif "lsif-clang" in args[0]:
                indexer = Indexer.LSIF_CLANG
            else:
                raise ValueError(
                    "unable to recognize indexer from --bench argument '{}'".format(
                        template
                    )
                )
            self.templates.append((indexer, args))


def parse_arguments() -> CommandLineArgs:
    parser = argparse.ArgumentParser(
        prog="benchmark",
        description="Benchmark different indexers (different builds of scip-clang or lsif-clang)."
        " This script should be called from the project root."
        " Benchmarking output is printed to stdout in JSON format.",
    )
    parser.add_argument(
        "--compdb-path",
        help="Path to main compilation database",
        default="compile_commands.json",
    )
    parser.add_argument(
        "--bench",
        help="One or more indexer invocation templates. Can be supplied multiple times.\n\n"
        "scip-clang templates typically look like:\n\n"
        "    /path/to/scip-clang"
        "lsif-clang templates typically look like:\n\n"
        '    /path/to/lsif-clang --extra-arg="-resource-dir=$(clang -print-resource-dir)\n\n'
        "The template should not have a --jobs flag, as parallelism is controlled"
        "by the benchmarking harness.",
        action="append",
    )
    parser.add_argument(
        "--chunk-count",
        help="How many smaller compilation databases should we break the larger database into?"
        " Each smaller compilation database will have a singular ~deterministic value for timing."
        " Larger values have more overhead, as the indexer needs to re-index standard"
        " headers more often.",
        default=os.cpu_count(),
    )
    args = parser.parse_args()
    return CommandLineArgs(
        compdb_path=Path(args.compdb_path),
        templates=args.bench,
        chunk_count=args.chunk_count,
    )


A = typing.TypeVar("A")


def try_divide(xs: List[A], num_chunks: int) -> List[List[A]]:
    assert len(xs) > num_chunks
    quot, rem = divmod(len(xs), num_chunks)
    out = []
    for i in range(num_chunks):
        out.append(xs[(quot * i) : (quot * (i + 1))])
    for i in range(rem):
        out[i].append(xs[quot * num_chunks + i])
    return out


@dataclass
class MeasureIndexingArgs:
    indexer: Indexer
    args_template: List[str]
    compdb_entries: List[Any]
    # List of relative paths
    files_to_symlink: List[str]


def measure_indexing_stats_impl(data: MeasureIndexingArgs) -> dict[str, Any]:
    with tempfile.TemporaryDirectory() as tempdir:
        for p in data.files_to_symlink:
            assert not os.path.isabs(p)
            os.symlink(p, os.path.join(tempdir, p))
        try:
            os.remove("compile_commands.json")
        except FileNotFoundError:
            pass
        with open(os.path.join(tempdir, "compile_commands.json"), "w") as out:
            json.dump(data.compdb_entries, out)

        args = data.args_template + ["--jobs", "1"]
        if data.indexer == Indexer.LSIF_CLANG:
            output = os.path.join(tempdir, "dump.lsif")
            args = (
                [args[0]] + ["compile_commands.json", "--out", "dump.lsif"] + args[1:]
            )
        else:
            assert data.indexer == Indexer.SCIP_CLANG
            output = os.path.join(tempdir, "index.scip")
        start = time.monotonic()
        completed = subprocess.run(args, capture_output=True, cwd=tempdir)
        delta = time.monotonic() - start
        if completed.returncode != 0:
            print(
                "Process failed: reproduce by running '{}'".format(shlex.join(args)),
                file=sys.stderr,
            )
        completed.check_returncode()
        index_size = os.stat(output).st_size
        return {
            "jobs": 1,
            "time_s": delta,
            "size_bytes": index_size,
            "invocation": args,
        }


def measure_indexing_stats(
    pool: Pool,
    template: tuple[Indexer, List[str]],
    partitioned_data: List[List[any]],
    files_to_symlink: List[str],
) -> List[dict[str, Any]]:
    return pool.map(
        measure_indexing_stats_impl,
        [
            MeasureIndexingArgs(
                indexer=template[0],
                args_template=template[1],
                compdb_entries=es,
                files_to_symlink=files_to_symlink,
            )
            for es in partitioned_data
        ],
    )


def default_main():
    args = parse_arguments()
    Measurement = analyze_compdb.Measurement
    requested_measurements = {str(m): False for m in list(Measurement)}
    for m in [
        Measurement.LINES,
        Measurement.PP_LINES,
        Measurement.SLOC,
        Measurement.PP_SLOC,
        Measurement.SEMA_TIME,
    ]:
        requested_measurements[str(m)] = True
    analysis_result = analyze_compdb.analyze(
        args.compdb_path, requested_measurements
    ).to_dict()

    compdb = CompilationDatabase.load(args.compdb_path)
    num_entries = len(compdb.entries)
    assert num_entries == len(analysis_result)

    all_data = analysis_result

    for i, result in enumerate(all_data):
        result["entry"] = compdb.entries[i].to_dict()

    partitioned_data = [
        {"per_command": data} for data in try_divide(all_data, args.chunk_count)
    ]
    assert len(partitioned_data) == args.chunk_count

    for p in partitioned_data:
        keys = list(p["per_command"][0].keys())
        p["aggregate"] = {}
        for k in keys:
            if k not in requested_measurements:
                continue
            p["aggregate"][k] = sum([cmd_data[k] for cmd_data in p["per_command"]])

    subcompdb_entries = [
        [e["entry"] for e in x["per_command"]] for x in partitioned_data
    ]

    files_to_symlink = os.listdir(".")
    for p in files_to_symlink:
        assert not os.path.isabs(p)

    with Pool() as pool:
        all_stats = []
        for template in args.templates:
            stats = measure_indexing_stats(
                pool, template, subcompdb_entries, files_to_symlink
            )
            concat_args = shlex.join(template[1])
            for i, chunk_stats in enumerate(stats):
                chunk_stats["template"] = concat_args
                p = partitioned_data[i]["aggregate"]
                if "indexing" in p:
                    p["indexing"].append(chunk_stats)
                else:
                    p["indexing"] = [chunk_stats]

    json.dump(partitioned_data, sys.stdout)


if __name__ == "__main__":
    default_main()
