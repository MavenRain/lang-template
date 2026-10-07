"""Regression checks for the shared template tools."""
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def run(*command, **kwargs):
    return subprocess.run(command, text=True, capture_output=True, timeout=15, **kwargs)


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.template = self.base / "template"
        self.template.mkdir()
        for name in ("bin", "design", "docs", "formers", "probe"):
            shutil.copytree(ROOT / name, self.template / name)
        for name in (".gitignore", "SPEC.template.md", "LICENSE-MIT", "LICENSE-APACHE"):
            shutil.copy2(ROOT / name, self.template / name)
        self.kit = self.template / "hosts" / "mech"
        self.kit.mkdir(parents=True)
        (self.kit / "README.md").write_text("Host guide\n")
        (self.kit / "FORMERS.md").write_text("Host formers\n")
        self.dest = self.base / "new language"

    def generate(self, **kwargs):
        return run("bash", str(self.template / "bin/new-lang.sh"),
                   "example-lang", "mech", str(self.dest), **kwargs)

    def test_export_and_git_generation(self):
        (self.kit / "domain").mkdir()
        (self.kit / "domain/example.txt").write_text("{{LANG}} {{HOST}}\n")
        binary = b"\x00{{LANG}}\xff"
        (self.kit / "domain/blob").write_bytes(binary)
        for tracked in (False, True):
            with self.subTest(tracked=tracked):
                if self.dest.exists():
                    shutil.rmtree(self.dest)
                if tracked:
                    self.assertEqual(run("git", "init", "-q", str(self.template)).returncode, 0)
                    self.assertEqual(run("git", "-C", str(self.template), "add", ".").returncode, 0)
                    (self.kit / ".gitignore").write_text("ignored\n")
                    (self.kit / "ignored").write_text("omit\n")
                result = self.generate()
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((self.dest / "domain/example.txt").read_text(), "example-lang mech\n")
                self.assertEqual((self.dest / "domain/blob").read_bytes(), binary)
                self.assertEqual((self.dest / "docs/host/README.md").read_text(), "Host guide\n")
                self.assertEqual((self.dest / "formers/mech.md").read_text(), "Host formers\n")
                self.assertTrue(os.access(self.dest / "probe/guard.py", os.X_OK))
                self.assertNotIn("{{", (self.dest / "SPEC.md").read_text())
                self.assertEqual(run("git", "-C", str(self.dest), "ls-files").stdout, "")
                self.assertEqual(run("git", "-C", str(self.dest), "symbolic-ref", "--short", "HEAD").stdout, "main\n")
                if tracked:
                    self.assertFalse((self.dest / "ignored").exists())
                shutil.rmtree(self.dest)

    def test_existing_destination_is_preserved(self):
        self.dest.mkdir()
        (self.dest / "keep").write_text("untouched")
        self.assertNotEqual(self.generate().returncode, 0)
        self.assertEqual((self.dest / "keep").read_text(), "untouched")

    def test_template_collision_is_rejected(self):
        (self.kit / "SPEC.md").write_text("collision")
        self.assertNotEqual(self.generate().returncode, 0)
        self.assertFalse(self.dest.exists())

    def test_file_directory_collision_is_rejected(self):
        (self.kit / "formers").write_text("would land inside the directory")
        self.assertNotEqual(self.generate().returncode, 0)
        self.assertFalse(self.dest.exists())

    def test_mapped_host_collision_is_rejected(self):
        (self.kit / "docs").mkdir()
        (self.kit / "docs/README.md").write_text("different guide\n")
        result = self.generate()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse(self.dest.exists())

    def test_destination_created_during_generation_is_preserved(self):
        wrappers = self.base / "wrappers"
        wrappers.mkdir()
        real_mkdir = shlex.quote(shutil.which("mkdir"))
        real_git = shlex.quote(shutil.which("git"))
        (wrappers / "mkdir").write_text(
            "#!/bin/sh\n"
            'for arg do\n'
            '  if [ "$arg" = "$RACE_DEST" ] && [ ! -e "$RACE_DEST" ]; then\n'
            f'    {real_mkdir} -p "$RACE_DEST"\n'
            '    printf untouched > "$RACE_DEST/keep"\n'
            '  fi\n'
            'done\n'
            f'exec {real_mkdir} "$@"\n')
        (wrappers / "git").write_text(
            '#!/bin/sh\nif [ "$3" = init ]; then exit 19; fi\n'
            f'exec {real_git} "$@"\n')
        for wrapper in wrappers.iterdir():
            wrapper.chmod(0o755)
        result = self.generate(env={**os.environ, "PATH": str(wrappers) + os.pathsep + os.environ["PATH"],
                                    "RACE_DEST": str(self.dest)})
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.dest / "keep").exists(), result.stderr)
        self.assertEqual((self.dest / "keep").read_text(), "untouched")
        self.assertFalse((self.dest / "SPEC.md").exists())


@unittest.skipUnless(sys.platform == "darwin", "guard uses macOS libproc")
class GuardTests(unittest.TestCase):
    def guard(self, script, *args, options=()):
        return run(sys.executable, str(ROOT / "probe/guard.py"), *options,
                   "--", sys.executable, "-c", script, *args)

    def test_command_arguments_are_preserved(self):
        result = self.guard("import json, sys; print(json.dumps(sys.argv[1:]))", "--", "--literal", "--")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), ["--", "--literal", "--"])

    def test_exit_code_is_preserved(self):
        result = self.guard("raise SystemExit(7)")
        self.assertEqual(result.returncode, 7)
        self.assertEqual(json.loads(result.stderr)["exit"], 7)

    def test_signal_exit_uses_shell_status(self):
        result = self.guard("import os, signal; os.kill(os.getpid(), signal.SIGTERM)")
        self.assertEqual(result.returncode, 128 + signal.SIGTERM, result.stderr)
        self.assertEqual(json.loads(result.stderr)["exit"], result.returncode)

    def test_timeout_reports_kill(self):
        result = self.guard("import time; time.sleep(10)", options=("--timeout", "0.1"))
        self.assertEqual(result.returncode, 137, result.stderr)
        self.assertEqual(json.loads(result.stderr)["verdict"], "timeout")


if __name__ == "__main__":
    unittest.main()
