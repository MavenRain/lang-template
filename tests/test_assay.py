"""Regressions for Assay dialect refusal and launcher paths."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


KIT = Path(__file__).resolve().parents[1] / "hosts/assay"


class AssayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)

    def refuse(self, source, *options):
        program = self.work / "program.asy"
        program.write_text(source)
        return subprocess.run(
            ["bash", str(KIT / "refuse.sh"), *map(str, options), str(program)],
            capture_output=True, text=True, timeout=15,
        )

    def test_protected_definitions_across_whitespace(self):
        for source in (
            "def\n total : Nat := 0\n",
            "def -- comment\n total : Nat := 0\n",
            "def fresh : Nat := 0 def total : Nat := 0\n",
            "def\n reflNat : Nat := 0\n",
            "def\n foldItems : Nat := 0\n",
            "def\n limit : Nat := 0\n",
        ):
            with self.subTest(source=source):
                result = self.refuse(source)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("redefines", result.stdout)

    def test_domain_names_use_tokens(self):
        domain = self.work / "domain"
        domain.mkdir()
        (domain / "Domain.asy").write_text(
            "mu Shade : Type 0 := | dark : Shade\n"
            "-- @carriers\n"
            "def\n domainValue : Nat := 0 def anotherValue : Nat := 1\n"
            "def primed' : Nat := 2\n"
        )
        for name in ("dark", "domainValue", "anotherValue", "primed'"):
            with self.subTest(name=name):
                result = self.refuse(f"def {name} : Nat := 0\n", "--domain", domain)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(f"the def {name} redefines", result.stdout)

    def test_host_identifiers_comments_and_strings(self):
        for source in (
            "def total' : Nat := 0\n",
            "def axiom' : Nat := 0\n",
            "def mu' : Nat := 0\n",
            '-- " axiom\ndef fresh : Nat := 0\n',
            '"axiom -- mu"\n',
            '"multiline\naxiom"\n',
        ):
            with self.subTest(source=source):
                result = self.refuse(source)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for source in (
            '-- "\naxiom forged : Nat\n-- "\n',
            '"--" axiom forged : Nat\n',
            "def -- comment\n rec loop : Nat := 0\n",
        ):
            with self.subTest(source=source):
                result = self.refuse(source)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("refused form", result.stdout)

    def test_launcher_quotes_node_preload_and_preserves_exit_status(self):
        launcher = self.work / "assay"
        launcher.write_text(
            "#!/bin/sh\nexec node -e 'process.exit(Number(process.argv[1]))' \"$@\"\n"
        )
        launcher.chmod(0o755)
        env = dict(os.environ, ASSAY_BIN=str(launcher), ASSAY_DIR=str(self.work),
                   ASSAY_SCRATCH=str(self.work / "scratch"))
        for name in ("space kit", 'quote"kit', "back\\slash"):
            with self.subTest(path=name):
                kit = self.work / name
                shutil.copytree(KIT, kit)
                result = subprocess.run(
                    ["bash", str(kit / "run.sh"), "path-check", "7"],
                    env=env, capture_output=True, text=True, timeout=15,
                )
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                self.assertIn("[probe] exit=7", result.stdout)


if __name__ == "__main__":
    unittest.main()
