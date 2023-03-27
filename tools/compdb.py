import copy
import json
import pathlib
import shlex
import subprocess
from typing import List


class CompilationDatabaseEntry:
    filepath: pathlib.Path
    directory: pathlib.Path
    arguments: List[str]

    def __init__(self, entry):
        self.filepath = pathlib.Path(entry["file"])
        self.directory = pathlib.Path(entry["directory"])
        try:
            self.arguments = shlex.split(entry["command"])
        except KeyError:
            self.arguments = entry(["arguments"])

    def change_tu_filepath(self, new_path: str):
        old_path = str(self.filepath)
        # ASSUMPTION: In a normal compilation command, we only expect there to
        # be a path to a single TU in the argument list, since the compilation
        # command will involve generating object code for a single TU.
        #
        # Moreover, files typically aren't present at the project root.
        #
        # These two factors mean that it's very unlikely for the argument list
        # to have two files like 'cake.c' and 'dessert/cake.c'.
        self.arguments = [arg.replace(old_path, new_path) for arg in self.arguments]
        self.filepath = pathlib.Path(new_path)

    def to_dict(self):
        return {
            "directory": str(self.directory),
            "file": str(self.filepath),
            "arguments": self.arguments[:],  # defensive copy
        }

    def run_preprocessor_only(self, preprocessed_tu_path: pathlib.Path | str):
        args = copy.deepcopy(self.arguments)
        args += ["-E", "-o", str(preprocessed_tu_path)]
        subprocess.run(args, cwd=self.directory).check_returncode()

    def run_sema_only(self):
        args = copy.deepcopy(self.arguments)
        args += ["-fsyntax-only", "-o", "/dev/null"]
        subprocess.run(args, cwd=self.directory).check_returncode()


class CompilationDatabase:
    entries: List[CompilationDatabaseEntry]

    def load(path: pathlib.Path | str):
        db = CompilationDatabase()
        db.entries = []
        with open(path) as compdbFile:
            for entry in json.load(compdbFile):
                db.entries.append(CompilationDatabaseEntry(entry))
        return db
