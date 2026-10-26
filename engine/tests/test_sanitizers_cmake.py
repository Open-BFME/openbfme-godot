"""Configure-only checks: python3 -m unittest discover -s engine/tests -p test_sanitizers_cmake.py."""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


MODULE = Path(__file__).resolve().parents[1] / "cmake" / "Sanitizers.cmake"


@unittest.skipUnless(all(shutil.which(t) for t in ("cmake", "ninja", "clang", "clang++")),
                     "requires CMake, Ninja and Clang")
class SanitizerCmakeTests(unittest.TestCase):
    def configure(self, value=None):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / "child").mkdir()
        (root / "main.cpp").write_text("int main() { return 0; }\n")
        (root / "helper.c").write_text("int helper(void) { return 0; }\n")
        (root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.17)\nproject(probe C CXX)\n'
            'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
            f'include("{MODULE.as_posix()}")\n'
            'add_library(helper STATIC helper.c)\nadd_subdirectory(child)\n')
        (root / "child" / "CMakeLists.txt").write_text(
            f'include("{MODULE.as_posix()}")\n'
            'add_executable(probe ../main.cpp)\ntarget_link_libraries(probe PRIVATE helper)\n')
        command = ["cmake", "-S", str(root), "-B", str(root / "build"), "-G", "Ninja",
                   "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"]
        if value is not None:
            command.append(f"-DOPENBFME_SANITIZE={value}")
        result = subprocess.run(command, text=True, capture_output=True)
        return root / "build", result

    def test_compile_and_link_flags(self):
        for value, expected in ((None, None), ("OFF", None), ("address", "address"),
                                ("undefined", "undefined"), ("address,undefined", "address,undefined"),
                                ("thread", "thread"), ("undefined;thread", "undefined,thread")):
            with self.subTest(value=value):
                build, result = self.configure(value)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                commands = json.loads((build / "compile_commands.json").read_text())
                self.assertEqual(len(commands), 2)
                link = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "probe"], text=True).splitlines()[-1]
                for command in [c["command"] for c in commands] + [link]:
                    if expected:
                        self.assertEqual(command.count(f"-fsanitize={expected}"), 1)
                    else:
                        self.assertNotIn("-fsanitize=", command)
                for command in commands:
                    self.assertEqual("-fno-omit-frame-pointer" in command["command"], bool(expected))

    def test_incompatible_or_unknown_modes_are_errors(self):
        for value, message in (("address,thread", "require separate builds"),
                               ("memory", "Unknown OPENBFME_SANITIZE")):
            with self.subTest(value=value):
                _, result = self.configure(value)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
