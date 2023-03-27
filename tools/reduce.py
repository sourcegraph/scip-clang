#!/usr/bin/env python3

import argparse
import contextlib
import copy
from dataclasses import dataclass
import json
import logging
import os
import pathlib
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
from typing import List

from compdb import *

# https://clang.llvm.org/docs/ClangCommandLineReference.html
# fmt: off
PATH_FLAGS = [
    "-fcrash-diagnostics-dir",
    "-B",
    "-F",
    "-I" "/I", "--include-directory",
    "--amdgpu-arch-tool",
    "--cuda-path",
    "--cxx-isystem",
    "-fbuild-session-file",
    "-fmodule-file", # ignoring -fmodule-file=name=x
    "-fmodules-cache-path",
    "-fmodules-user-build-path",
    "-fprebuilt-module-path",
    "--hip-path",
    "-idirafter",
    "-idirafter", "--include-directory-after",
    "-iframework,"
    "-iframeworkwithsysroot",
    "-imacros", "--imacros",
    "-include", "--include",
    "-include-pch",
    "-iprefix", "--include-prefix",
    "-iquote",
    "-isysroot",
    "-isystem",
    "-system-after",
    "-ivfsoverlay",
    "-iwithprefix", "--include-with-prefix", "--include-with-prefix-after",
    "-iwithprefixbefore", "--include-with-prefix-before",
    "-iwithsysroot",
    "--libomptarget-amdgpu-bc-path", "--libomptarget-amdgcn-bc-path",
    "--libomptarget-nvptx-bc-path",
    "--nvptx-arch-tool",
    "--ptxas-path",
    "--rocm-path",
    "-stdlib++-isystem",
    "--system-header-prefix", "--no-system-header-prefix"
]
# fmt: on


def check_scip_clang_output(
    scip_clang: pathlib.Path, compdb_path: str, pattern: re.Pattern
) -> bool:
    """Check if the stdout or stderr has a match for the pattern"""
    result = subprocess.run(
        [str(scip_clang), "--compdb-path", compdb_path, "--worker-mode=compdb"],
        capture_output=True,
        encoding="utf8",
    )
    return bool(re.search(pattern, result.stdout) or re.search(pattern, result.stderr))


@dataclass
class CReduce:
    entry: CompilationDatabaseEntry
    scip_clang: pathlib.Path
    project_root: pathlib.Path
    scip_clang_output_pattern: re.Pattern
    extra_args: List[str]

    def __post_init__(self):
        # Lenient behavior instead of hard-coding which flags allow =
        flags = [re.escape(flag) + "=?" for flag in PATH_FLAGS]
        self.path_flag_pattern = re.compile(
            "(?P<flag>{})(?P<maybe_path>.*)".format("|".join(flags))
        )

    def run(self, start_tu_path: pathlib.Path):
        assert start_tu_path.is_absolute()
        absolute_start_tu_path = start_tu_path
        start_tu_path = start_tu_path.relative_to(self.project_root)
        assert not start_tu_path.is_absolute()

        original_tu_path = str(self.entry.filepath)
        for i, arg in enumerate(self.entry.arguments):
            if original_tu_path in arg:
                self.entry.arguments[i] = str(start_tu_path)
        self.entry.filepath = start_tu_path

        self.fix_paths()

        with contextlib.ExitStack() as exitstack:
            tempdir = tempfile.mkdtemp(prefix="scip-clang-reduce-")
            exitstack.callback(
                lambda: logging.info(
                    f"scip-clang temporary directory persisted at {tempdir}"
                )
            )
            tempdir = pathlib.Path(tempdir)
            self.entry.directory = tempdir

            shutil.copyfile(absolute_start_tu_path, tempdir.joinpath(start_tu_path))
            compdb = tempdir.joinpath("compile_commands.json")
            with open(compdb, "w") as compdb_file:
                json.dump([self.entry.to_dict()], compdb_file)

            run_sh = tempdir.joinpath("run.sh")
            with open(run_sh, "w") as run_sh_file:
                assert self.scip_clang.is_absolute()
                run_sh_file.write(
                    textwrap.dedent(
                        f"""\
                    #!/usr/bin/env bash
                    {self.scip_clang} --worker-mode=compdb --compdb-path={compdb} > out.log 2>&1
                    set -e
                    grep -E '{self.scip_clang_output_pattern.pattern}' out.log
                    """
                    )
                )
            run_sh.chmod(run_sh.stat().st_mode | stat.S_IEXEC)

            result = subprocess.run(
                ["creduce", str(run_sh), str(start_tu_path)] + self.extra_args,
                cwd=tempdir,
            )
            if result.returncode != 0:
                sys.exit(result.returncode)

    def fix_paths(self):
        """
        Adjusts the paths in the argument list so that the compilation database
        entry can be relocated correctly to a temporary directory.

        Post-condition:
        - Include paths, such as using -I and -isysroot, use absolute paths
        - The path for the main TU has a relative path

        Creduce creates temporary directories, so we want all the paths to
        work properly from those directories too.
        """
        original_path = self.entry.filepath
        if self.entry.filepath.is_absolute():
            self.entry.filepath = self.entry.filepath.relative_to(self.project_root)
        tu_path_str = str(self.entry.filepath)

        def fix_path(maybe_path: str) -> str:
            if os.path.isabs(maybe_path):
                return maybe_path
            abs_path = self.entry.directory.joinpath(maybe_path)
            if abs_path.exists():
                return str(abs_path)
            # Maybe it wasn't a path argument after all...
            logging.warning(
                "expected path but found {} in argument list; ignoring".format(
                    maybe_path
                )
            )
            return maybe_path

        for i, arg in enumerate(self.entry.arguments):
            if tu_path_str in arg:
                if os.path.isabs(arg):
                    self.entry.arguments[i] = tu_path_str
            else:
                match = re.match(self.path_flag_pattern, arg)
                if match is None:
                    continue
                if match.group("maybe_path"):
                    self.entry.arguments[i] = match.group("flag") + fix_path(
                        match.group("maybe_path")
                    )
                elif i + 1 < len(self.entry.arguments):
                    self.entry.arguments[i + 1] = fix_path(self.entry.arguments[i + 1])


def default_main():
    parser = argparse.ArgumentParser(
        prog="reduce", description="Reduce scip-clang bugs"
    )
    parser.add_argument(
        "compdb", help="Path to compilation database containing a single entry"
    )
    default_pattern = "Exception::raise\(\)"
    parser.add_argument(
        "--pattern",
        help=f"Extended regexp to match against stdout+stderr (default = {default_pattern})",
        default=default_pattern,
    )
    parser.add_argument(
        "creduce_args",
        help="-- followed by extra arguments for creduce",
        nargs=argparse.REMAINDER,
    )
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO)

    compdb = CompilationDatabase.load(args.compdb)

    n_entries = len(compdb.entries)
    if n_entries != 1:
        raise ValueError(
            f"Expected compilation database to have 1 entry but found {n_entries}"
        )

    cwd = pathlib.Path.cwd()
    project_root = cwd

    entry = CompilationDatabaseEntry(compdb.entries[0])

    scip_clang = pathlib.Path(
        __file__, "..", "..", "bazel-bin", "indexer", "scip-clang"
    ).resolve()
    pattern = re.compile(args.pattern)

    with tempfile.NamedTemporaryFile(
        prefix=entry.filepath.with_suffix("").name + "-",
        suffix=".min" + entry.filepath.suffix,
        dir=project_root,
    ) as start_tu_file:
        start_tu_path = pathlib.Path(start_tu_file.name)
        assert start_tu_path.is_absolute()

        entry.run_preprocessor_only(start_tu_path)

        new_entry = copy.deepcopy(entry)
        new_entry.change_tu_filepath(str(start_tu_path))
        with tempfile.NamedTemporaryFile(
            mode="w", prefix="compile_commands-", suffix=".json", dir=project_root
        ) as temp_compdb:
            json.dump([new_entry.to_dict()], temp_compdb)
            temp_compdb.flush()
            minimize_preprocessed = check_scip_clang_output(
                scip_clang, temp_compdb.name, pattern
            )
            if not minimize_preprocessed:
                logging.warning(
                    "could not reproduce error with pre-processed output; will try to minimize original file"
                )
                shutil.copyfile(entry.filepath, start_tu_path)
            else:
                logging.info("reproduced problem with pre-processed file")

        extra_args = args.creduce_args

        creduce = CReduce(
            entry=entry,
            scip_clang=scip_clang,
            project_root=project_root,
            scip_clang_output_pattern=pattern,
            extra_args=extra_args,
        )
        creduce.run(start_tu_path)


if __name__ == "__main__":
    default_main()
