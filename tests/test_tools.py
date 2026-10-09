"""Regression checks for the shared template tools."""
import json
import os
from pathlib import Path
import re
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

    def test_tcc_json_host(self):
        kit = self.template / "hosts" / "tcc-json"
        (kit / "src").mkdir(parents=True)
        (kit / "README.md").write_text("Host guide\n")
        (kit / "FORMERS.md").write_text("Host formers\n")
        (kit / "src/json.c").write_text('static const char LANG_NAME[] = "{{LANG}}";\n')
        result = run("bash", str(self.template / "bin/new-lang.sh"),
                     "example-lang", "tcc-json", str(self.dest))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.dest / "formers/tcc-json.md").read_text(), "Host formers\n")
        self.assertEqual((self.dest / "docs/host/README.md").read_text(), "Host guide\n")
        self.assertEqual((self.dest / "src/json.c").read_text(),
                         'static const char LANG_NAME[] = "example-lang";\n')
        self.assertIn("Run the gate in", result.stdout)
        self.assertIn(": make check\n", result.stdout)

    def test_tcc_evm_contract_host(self):
        kit = self.template / "hosts" / "tcc-evm-contract"
        (kit / "src").mkdir(parents=True)
        (kit / "docs").mkdir()
        (kit / "README.md").write_text("Host guide\n")
        (kit / "FORMERS.md").write_text("Host formers\n")
        (kit / "docs/CAPABILITY.md").write_text("Host probe\n")
        (kit / "src/evm.c").write_text('static const char LANG_NAME[] = "{{LANG}}";\n')
        result = run("bash", str(self.template / "bin/new-lang.sh"),
                     "example-lang", "tcc-evm-contract", str(self.dest))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.dest / "formers/tcc-evm-contract.md").read_text(), "Host formers\n")
        self.assertEqual((self.dest / "docs/host/README.md").read_text(), "Host guide\n")
        self.assertEqual((self.dest / "docs/host/CAPABILITY.md").read_text(), "Host probe\n")
        self.assertEqual((self.dest / "src/evm.c").read_text(),
                         'static const char LANG_NAME[] = "example-lang";\n')
        self.assertIn(": make check\n", result.stdout)

    def test_tcc_wasm_tcc_evm_tcc_evm_dao_and_tcc_evm_anchor_hosts(self):
        for host, target in (("tcc-wasm", "src/wasm.c"), ("tcc-evm", "src/evm.c"),
                             ("tcc-evm-dao", "src/evm.c"), ("tcc-evm-anchor", "src/evm.c")):
            with self.subTest(host=host):
                kit = self.template / "hosts" / host
                (kit / "src").mkdir(parents=True)
                (kit / "docs").mkdir()
                (kit / "README.md").write_text("Host guide\n")
                (kit / "FORMERS.md").write_text("Host formers\n")
                (kit / "docs/CAPABILITY.md").write_text("Host probe\n")
                (kit / target).write_text('static const char LANG_NAME[] = "{{LANG}}";\n')
                dest = self.base / host
                result = run("bash", str(self.template / "bin/new-lang.sh"),
                             "example-lang", host, str(dest))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((dest / f"formers/{host}.md").read_text(), "Host formers\n")
                self.assertEqual((dest / "docs/host/README.md").read_text(), "Host guide\n")
                self.assertEqual((dest / "docs/host/CAPABILITY.md").read_text(), "Host probe\n")
                self.assertEqual((dest / target).read_text(),
                                 'static const char LANG_NAME[] = "example-lang";\n')
                self.assertIn(": make check\n", result.stdout)

    def test_unknown_host_is_rejected(self):
        for host in ("nohost", "mech assay", "", "tcc", "tcc-evm ", "../mech"):
            with self.subTest(host=host):
                # A kit directory exists for each name, so only the HOST check can refuse it.
                (self.template / "hosts" / host).mkdir(parents=True, exist_ok=True)
                result = run("bash", str(self.template / "bin/new-lang.sh"),
                             "example-lang", host, str(self.dest))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"new-lang: unknown HOST '{host}': use ", result.stderr)
                self.assertFalse(self.dest.exists())

    def test_host_without_gate_arm_is_refused(self):
        script = self.template / "bin/new-lang.sh"
        text = script.read_text()
        self.assertIn("\nhosts=(mech ", text)
        script.write_text(text.replace("\nhosts=(mech ", "\nhosts=(tcc-new mech ", 1))
        kit = self.template / "hosts" / "tcc-new"
        kit.mkdir()
        (kit / "README.md").write_text("Host guide\n")
        (kit / "FORMERS.md").write_text("Host formers\n")
        result = run("bash", str(script), "example-lang", "tcc-new", str(self.dest))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("new-lang: HOST 'tcc-new' has no gate command", result.stderr)
        self.assertFalse(self.dest.exists())

    def test_tcc_json_document_key_is_reserved(self):
        result = run("bash", str(self.template / "bin/new-lang.sh"),
                     "instances", "tcc-json", str(self.dest))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("reserved by the tcc-json document format", result.stderr)
        self.assertFalse(self.dest.exists())


class HostListTests(unittest.TestCase):
    """bin/new-lang.sh keeps its hosts in one array."""

    def hosts(self):
        lines = re.findall(r"^hosts=\(([^)]*)\)$",
                           (ROOT / "bin/new-lang.sh").read_text(), re.MULTILINE)
        self.assertEqual(len(lines), 1, lines)
        return lines[0].split()

    def test_each_host_has_a_kit(self):
        # A kit can exist in hosts/ before it is in the array, so the test
        # does not compare the array with the directories.
        hosts = self.hosts()
        self.assertEqual(len(hosts), len(set(hosts)), hosts)
        for host in hosts:
            with self.subTest(host=host):
                self.assertTrue((ROOT / "hosts" / host).is_dir())

    def test_usage_lists_exactly_the_hosts(self):
        result = run("bash", str(ROOT / "bin/new-lang.sh"))
        self.assertEqual(result.returncode, 2, result.stderr)
        lines = [line for line in result.stderr.splitlines() if line.startswith("  HOST  ")]
        self.assertEqual(len(lines), 1, result.stderr)
        head, last = lines[0][len("  HOST  "):].rsplit(" or ", 1)
        self.assertEqual(head.split(", ") + [last], self.hosts())

    def test_docs_list_exactly_the_hosts(self):
        # README.md and the formers matrix name the hosts by hand, in the
        # order of the hosts array.
        hosts = self.hosts()
        readme = (ROOT / "README.md").read_text()
        table = re.findall(r"^\| ([a-z][a-z0-9-]*) \| .* \| \[`hosts/\1/README\.md`\]",
                           readme, re.MULTILINE)
        self.assertEqual(table, hosts)
        lines = re.findall(r"^- `HOST` is (.*)\.$", readme, re.MULTILINE)
        self.assertEqual(len(lines), 1, lines)
        self.assertEqual([host.strip("`") for host in re.split(r", | or ", lines[0])], hosts)
        kits = re.findall(r"^\| (.*) \| The host kits \|$", readme, re.MULTILINE)
        self.assertEqual(len(kits), 1, kits)
        self.assertEqual(re.findall(r"`hosts/([a-z0-9-]+)/`", kits[0]), hosts)
        header = re.findall(r"^\| ID \| Former \| (.*) \|$",
                            (ROOT / "formers/FORMERS.md").read_text(), re.MULTILINE)
        self.assertEqual(len(header), 1, header)
        columns = [cell.split(" (")[0] for cell in header[0].split(" | ")]
        self.assertEqual(columns, hosts)

    def test_formers_prose_lists_exactly_the_hosts(self):
        # The formers host-file list and the section 5 provenance prose also
        # name the hosts by hand, in the order of the hosts array.
        hosts = self.hosts()
        formers = (ROOT / "formers/FORMERS.md").read_text()
        files = re.findall(r"Each host file\s+\((.*?)\)\s+gives the", formers, re.DOTALL)
        self.assertEqual(len(files), 1, files)
        self.assertEqual(re.findall(r"`hosts/([a-z0-9-]+)/FORMERS\.md`", files[0]), hosts)
        sections = re.findall(r"^## 5\. Realization matrix$(.*?)^\| ID \| Former \|",
                              formers, re.MULTILINE | re.DOTALL)
        self.assertEqual(len(sections), 1, sections)
        facts = re.findall(r"([a-z][a-z0-9-]*)(?: and ([a-z][a-z0-9-]*))? facts come from",
                           " ".join(sections[0].split()))
        self.assertEqual([host for pair in facts for host in pair if host], hosts)


class TccKitTests(unittest.TestCase):
    """Make a language from each real TinyCC kit and run its own gate."""

    def generate_and_check(self, host, *kit_files):
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp) / "example-lang"
            result = run("bash", str(ROOT / "bin/new-lang.sh"), "example-lang", host, str(dest))
            self.assertEqual(result.returncode, 0, result.stderr)
            for path in (f"formers/{host}.md", "docs/host/README.md", "docs/host/CAPABILITY.md",
                         "domain/domain.lang", "Makefile") + kit_files:
                self.assertTrue((dest / path).is_file(), path)
            self.assertFalse((dest / "build").exists())
            gate = subprocess.run(("make", "-C", str(dest), "check"), text=True,
                                  capture_output=True, timeout=900)
            self.assertEqual(gate.returncode, 0, gate.stdout[-4000:] + gate.stderr[-4000:])

    def test_tcc_wasm_language_passes_its_gate(self):
        self.generate_and_check("tcc-wasm", "src/wasm.c", "PIN")

    def test_tcc_evm_language_passes_its_gate(self):
        self.generate_and_check("tcc-evm", "src/evm.c", "PIN")

    def test_tcc_evm_dao_language_passes_its_gate(self):
        self.generate_and_check("tcc-evm-dao", "src/evm.c")

    def test_tcc_evm_anchor_language_passes_its_gate(self):
        self.generate_and_check("tcc-evm-anchor", "src/evm.c")


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
