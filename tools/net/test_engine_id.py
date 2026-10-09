"""MP-1 review r2 fix 1: the engine compatibility id of the profile identity (engine/cmake/EngineId.cmake).

The id is a hash of the simulation sources and configuration, not of the Git state: an exported source tree without Git has the id of its contents,
two different edits (clean or dirty) have different ids, a release pipeline's override is honoured, and a tree with nothing to hash fails the configure step.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(REPO, "engine", "cmake", "EngineId.cmake")
CMAKE = shutil.which("cmake")


def engine_id(root, extra=()):
    r = subprocess.run([CMAKE, "-DOPENBFME_ID_ROOT=" + root, *extra, "-P", SCRIPT], capture_output=True, text=True)
    m = re.search(r"engine-id ([0-9a-f]{64})", r.stdout + r.stderr)
    return r.returncode, (m.group(1) if m else None), (r.stdout + r.stderr)


@unittest.skipIf(CMAKE is None, "cmake is not available")
class EngineIdTest(unittest.TestCase):
    def setUp(self):
        # under $HOME: the cmake wrapper of the Linux toolchain sees only the home directory
        self.tmp = tempfile.mkdtemp(prefix="obfme-engineid-", dir=os.path.expanduser("~"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def tree(self, name, files):
        root = os.path.join(self.tmp, name)
        for rel, text in files.items():
            path = os.path.join(root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", newline="") as f:
                f.write(text)
        return root

    BASE = {"CMakeLists.txt": "project(x)\n", "src/a.cpp": "int a() { return 1; }\n", "cmake/b.cmake": "set(B 1)\n", "data/c.json": "{}\n"}

    def test_export_without_git_equals_checkout_with_git(self):
        plain = self.tree("export", self.BASE)
        rc, a, _ = engine_id(plain)
        self.assertEqual(rc, 0)
        withgit = self.tree("checkout", dict(self.BASE, **{".git/HEAD": "ref: refs/heads/x\n", ".git/config": "[core]\n"}))
        rc, b, _ = engine_id(withgit)
        self.assertEqual(a, b, "the Git directory (provenance) is not part of the identity")
        self.assertIsNotNone(a)
        self.assertNotEqual(a, "not-a-git-checkout")

    def test_distinct_sources_have_distinct_ids(self):
        a = engine_id(self.tree("one", self.BASE))[1]
        b = engine_id(self.tree("two", dict(self.BASE, **{"src/a.cpp": "int a() { return 2; }\n"})))[1]       # a "dirty" edit
        c = engine_id(self.tree("three", dict(self.BASE, **{"src/a.cpp": "int a() { return 3; }\n"})))[1]     # another edit of the same file
        d = engine_id(self.tree("four", dict(self.BASE, **{"src/extra.cpp": "\n"})))[1]                          # an added file
        e = engine_id(self.tree("five", dict(self.BASE, **{"data/c.json": "{ }\n"})))[1]                        # changed data
        self.assertEqual(len({a, b, c, d, e}), 5)

    def test_line_endings_and_untracked_docs_do_not_matter(self):
        a = engine_id(self.tree("lf", self.BASE))[1]
        crlf = {k: v.replace("\n", "\r\n") for k, v in self.BASE.items()}
        b = engine_id(self.tree("crlf", crlf))[1]
        c = engine_id(self.tree("docs", dict(self.BASE, **{"tests/t.cpp": "x\n", "README.md": "hi\n"})))[1]
        self.assertEqual(a, b)
        self.assertEqual(a, c)

    def test_configuration_is_part_of_the_id(self):
        root = self.tree("cfg", self.BASE)
        a = engine_id(root)[1]
        self.assertNotEqual(a, engine_id(root, ["-DOPENBFME_ID_X86_32=ON"])[1])
        self.assertNotEqual(a, engine_id(root, ["-DOPENBFME_ID_OPTIONS=fast"])[1])
        self.assertEqual(a, engine_id(root, ["-DOPENBFME_ID_X86_32=OFF"])[1])

    def test_a_tree_with_nothing_to_hash_fails(self):
        empty = self.tree("empty", {"README.md": "no engine here\n"})
        rc, ident, out = engine_id(empty)
        self.assertNotEqual(rc, 0)
        self.assertIsNone(ident)
        self.assertIn("no trustworthy", " ".join(out.split()))  # CMake wraps the message by path length

    def test_the_real_tree_has_an_id_and_cmake_honours_the_release_override(self):
        rc, real, _ = engine_id(os.path.join(REPO, "engine"))
        self.assertEqual(rc, 0)
        self.assertRegex(real, r"^[0-9a-f]{64}$")
        net = open(os.path.join(REPO, "engine", "cmake", "Net.cmake")).read()
        self.assertIn("OPENBFME_ENGINE_ID", net)
        self.assertNotIn("not-a-git-checkout", net)
        self.assertNotIn("-dirty", net)
        ident = open(os.path.join(REPO, "engine", "src", "GameNetwork", "ProfileIdentity.cpp")).read()
        self.assertNotIn("not-a-git-checkout", ident)
        self.assertIn("#error", ident)

    def test_configure_override_is_validated_and_used(self):
        # a miniature project that includes the real Net.cmake id logic is not needed: configure the real tree's id section in script-free form
        probe = os.path.join(self.tmp, "probe")
        os.makedirs(probe)
        with open(os.path.join(probe, "CMakeLists.txt"), "w") as f:
            f.write("cmake_minimum_required(VERSION 3.16)\nproject(p NONE)\nset(OPENBFME_SIM_X86_32 OFF)\nset(CMAKE_CURRENT_SOURCE_DIR_SAVE ${CMAKE_CURRENT_SOURCE_DIR})\n"
                    "set(OPENBFME_ENGINE_ID \"\" CACHE STRING \"\")\n")
            net = open(os.path.join(REPO, "engine", "cmake", "Net.cmake")).read()
            a = net.index("include(cmake/EngineId.cmake)")
            b = net.index("# provenance")
            body = net[a:b].replace("include(cmake/EngineId.cmake)", "include(\"%s\")" % SCRIPT.replace("\\", "/"))
            f.write(body)
            f.write("message(STATUS \"ID=${OPENBFME_ENGINE_ID_VALUE} SRC=${OPENBFME_ENGINE_ID_SOURCE}\")\n")
        self.tree("probe/src", {"a.cpp": "x\n"})
        good = "ab" * 32

        def configure(*args):
            build = os.path.join(self.tmp, "pb")
            shutil.rmtree(build, ignore_errors=True)
            r = subprocess.run([CMAKE, "-S", probe, "-B", build, *args], capture_output=True, text=True)
            return r.returncode, r.stdout + r.stderr

        rc, out = configure("-DOPENBFME_ENGINE_ID=" + good)
        self.assertEqual(rc, 0, out)
        self.assertIn("ID=" + good, out)
        self.assertIn("release override", out)
        rc, out = configure("-DOPENBFME_ENGINE_ID=notahash")
        self.assertNotEqual(rc, 0)
        self.assertIn("64 hex", out)
        rc, out = configure()
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r"ID=[0-9a-f]{64} SRC=computed")
        os.remove(os.path.join(probe, "src", "a.cpp"))
        os.makedirs(os.path.join(probe, "src"), exist_ok=True)
        rc, out = configure()
        self.assertNotEqual(rc, 0, "no sources and no override: no identity")
        self.assertIn("no trustworthy", " ".join(out.split()))  # CMake wraps the message by path length


if __name__ == "__main__":
    unittest.main()
