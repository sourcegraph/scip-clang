#!/usr/bin/env python3

import argparse
from dataclasses import dataclass
from multiprocessing import Pool
from pathlib import Path
import typing
from ruamel.yaml import YAML
import sys

from compdb import *


def parse_arguments() -> tuple[Path, Path]:
    parser = argparse.ArgumentParser(
        prog="analyze_compdb",
        description="Analyze various statistics about translation units in a compilation database",
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--compdb-path",
        help="Path to compilation database",
        default="",
    )
    group.add_argument(
        "--yaml-path",
        help="Path to YAML file emitted by pp-trace to check for violations",
        default="",
    )
    args = parser.parse_args()
    return Path(args.compdb_path), Path(args.yaml_path)


@dataclass
class Violation:
    pp_trace_entry: any
    pass

    def toJSON(self):
        return json.dumps(self, default=lambda o: o.__dict__, sort_keys=True, indent=4)


@dataclass
class PrevDisconnectedViolation(Violation):
    loc: str
    prev_fid: str


@dataclass
class PostEndOfMainFileViolation(Violation):
    pass


@dataclass
class UnbalancedViolation(Violation):
    pass


def compute_pp_trace_balance_impl(entry: CompilationDatabaseEntry) -> List[Violation]:
    # invoke pp-trace
    cmd = ["pp-trace", "-callbacks", "FileChanged,EndOfMainFile"]
    for arg in entry.arguments[1:]:
        cmd.append("--extra-arg={}", shlex.quote(arg))
    result = subprocess.run(cmd, capture_output=True)
    result.check_returncode()
    return compute_violations(result.stdout)


def compute_violations(input: str | typing.IO) -> List[Violation]:
    yaml = YAML(pure=True)
    data = yaml.load(input)
    saw_end_of_main_file = False
    stack = []
    violations = []
    for obj in data:
        callback = obj["Callback"]
        if saw_end_of_main_file:
            if callback == "EndOfMainFile" or callback == "FileChanged":
                violations.append(PostEndOfMainFileViolation(pp_trace_entry=obj))
            continue
        if callback == "FileChanged":
            if len(stack) > 0:
                last_loc = stack[-1]["Loc"]
                last_loc_path = (
                    last_loc.split(":")[0] if last_loc.startswith("/") else None
                )
                prev_fid = obj["PrevFID"] if obj["PrevFID"].startswith("/") else None
                if last_loc_path and prev_fid and last_loc_path != prev_fid:
                    violations.append(
                        PrevDisconnectedViolation(
                            pp_trace_entry=obj, loc=last_loc, prev_fid=prev_fid
                        )
                    )
            reason = obj["Reason"]
            if reason == "EnterFile":
                stack.append(obj)
            elif reason == "ExitFile":
                if len(stack) > 0:
                    stack.pop()
                else:
                    violations.append(UnbalancedViolation(pp_trace_entry=obj))
        elif callback == "EndOfMainFile":
            saw_end_of_main_file = True
            if len(stack) == 0:
                violations.append(UnbalancedViolation(pp_trace_entry=obj))
                continue
            stack.pop()
            for obj in stack:
                violations.append(UnbalancedViolation(pp_trace_entry=obj))

    return violations


def compute_pp_trace_balance(
    pool: Pool, entries: List[CompilationDatabaseEntry]
) -> dict[str, List[Violation]]:
    results = pool.map(compute_pp_trace_balance_impl, entries)
    return {str(entries[i].filepath): r for (i, r) in enumerate(results)}


def default_main():
    compdb_path, yaml_path = parse_arguments()
    results = None
    if len(yaml_path.as_posix()) > 0:
        with open(yaml_path) as yaml_file:
            results = compute_violations(yaml_file)
    else:
        assert len(compdb_path.as_posix()) > 0
        compdb = CompilationDatabase.load(compdb_path)
        with Pool() as pool:
            results = compute_pp_trace_balance(pool, compdb.entries).items()
    yaml_unsafe = YAML(typ="unsafe")
    if len(results) == 0:
        print("No errors!")
        return
    yaml_unsafe.dump(results, sys.stdout)


if __name__ == "__main__":
    default_main()
