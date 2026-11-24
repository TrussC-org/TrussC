"""Offline external-addon versions; all Git mutations use temporary fixtures."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import unittest

from test_trusscli_versions import VersionTests, CLI, NATIVE, fixture_rmtree


class AddonVersionTests(VersionTests):
    # Reuse the stage 1 fixture and acquisition tests without rerunning inherited tests.
    def setUp(self):
        super().setUp()
        root = Path(__file__).resolve().parents[2]
        shutil.copyfile(root / "core/cmake/use_addon.cmake", self.source / "core/cmake/use_addon.cmake")
        (self.source / "core/cmake/trussc_app.cmake").write_text(
            'include(${CMAKE_CURRENT_LIST_DIR}/use_addon.cmake)\nadd_library(TrussC INTERFACE)\n'
            'macro(trussc_app)\nadd_executable(fixture src/main.cpp)\napply_addons(fixture)\n'
            'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/fixture-links.txt" '
            'CONTENT "$<TARGET_PROPERTY:fixture,LINK_LIBRARIES>")\nendmacro()\n', encoding="utf-8")
        bundled = self.source / "addons/tcxBundled"
        bundled.mkdir(parents=True)
        (bundled / "addon.json").write_text('{}', encoding="utf-8")
        self.git("-C", self.source, "add", ".")
        self.git("-C", self.source, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-m", "addon hook")
        self.git("-C", self.source, "tag", "v1.0.0")
        for owner in ("TrussC-org", "tettou771"):
            self.git("-C", self.base / "remotes" / owner / "TrussC.git", "fetch", str(self.source), "+refs/heads/*:refs/heads/*", "+refs/tags/*:refs/tags/*")
        self.addon = self.base / "addon-source"
        (self.addon / "src").mkdir(parents=True)
        self.metadata = {"trussc_version": ">=2.0.0 <3.0.0"}
        (self.addon / "addon.json").write_text(json.dumps(self.metadata), encoding="utf-8")
        (self.addon / "src/value.h").write_text('#define ADDON_VALUE 1\n', encoding="utf-8")
        self.git("init", "-b", "main", self.addon)
        self.commit_addon()
        self.addon_initial = self.git("-C", self.addon, "rev-parse", "HEAD").strip()
        self.git("-C", self.addon, "tag", "v1")
        (self.addon / "src/value.h").write_text('#define ADDON_VALUE 2\n', encoding="utf-8")
        self.commit_addon()
        self.addon_latest = self.git("-C", self.addon, "rev-parse", "HEAD").strip()
        self.git("-C", self.addon, "branch", "feature/next")
        for owner in ("alice", "bob"):
            target = self.base / "remotes" / owner / "tcxFixture.git"
            target.parent.mkdir(parents=True)
            self.git("clone", "--bare", self.addon, target)
        registry = self.registry_path()
        registry.parent.mkdir(parents=True)
        registry.write_text(json.dumps({"addons": [{"name": "tcxFixture", "owner": "alice", "url": "https://github.com/alice/tcxFixture.git"}]}), encoding="utf-8")
        self.cli("use", "v1.0.0")

    def commit_addon(self):
        self.git("-C", self.addon, "add", ".")
        self.git("-C", self.addon, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-m", "addon revision")

    def test_addon_unavailable_ref_names_repository(self):
        self.cli("addon", "add", "alice/tcxFixture@missing", success=False)
        self.assertEqual(self.last_result.returncode, 1)
        self.assertEqual(next(line for line in self.last_result.stderr.splitlines() if line.startswith("Error:")),
                         "Error: TrussC ref 'missing' was not found in alice/tcxFixture.")

    def paths(self):
        data = json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))
        return data["vendor"]["trussc"]["addonPaths"]

    def local_addons(self, dependencies):
        paths = {}
        for name, needs in dependencies.items():
            path = self.base / name
            (path / "src").mkdir(parents=True)
            (path / "addon.json").write_text(json.dumps({"dependencies": needs}), encoding="utf-8")
            paths[name] = str(path)
        file = self.project / "CMakePresets.json"
        data = json.loads(file.read_text(encoding="utf-8"))
        data["vendor"]["trussc"]["addonLocalPaths"] = paths
        file.write_text(json.dumps(data), encoding="utf-8")
        return {name: Path(path) for name, path in paths.items()}

    def test_addon_legacy_build_uses_preset_core(self):
        preset_root = self.root()
        for root, value in ((preset_root, 1), (self.source, 9)):
            path = root / "addons/tcxLegacy/src"
            path.mkdir(parents=True)
            (path / "value.h").write_text(f"#define ADDON_VALUE {value}\n", encoding="utf-8")
        (self.project / ".trussc-version").unlink()
        file = self.project / "CMakePresets.json"
        data = json.loads(file.read_text(encoding="utf-8"))
        for preset in data["configurePresets"]:
            for key in ("TRUSSC_ADDON_PATHS", "TRUSSC_ADDON_ORDER"):
                preset["cacheVariables"].pop(key, None)
        data["vendor"]["trussc"].pop("addonPaths", None)
        file.write_text(json.dumps(data), encoding="utf-8")
        (self.project / "addons.make").write_text("tcxLegacy\n", encoding="utf-8")
        (self.project / "src/main.cpp").write_text(
            '#include "value.h"\nint main() { return ADDON_VALUE == 1 ? 0 : 1; }\n', encoding="utf-8")
        self.assertNotIn("-DTRUSSC_ADDON_PATHS=", self.cli("build", "--dry-run"))
        self.cli("build")
        self.assertEqual(subprocess.run([str(self.project / f"build-{NATIVE}/fixture")], check=False).returncode, 0)

    def test_addon_empty_selection_clears_preset_map(self):
        self.cli("addon", "add", "tcxFixture@v1")
        (self.project / "addons.make").write_text("", encoding="utf-8")
        self.assertFalse((self.project / f"build-{NATIVE}/CMakeCache.txt").exists())
        output = self.cli("build")
        self.assertNotIn("[tcxFixture]", output)
        cache = (self.project / f"build-{NATIVE}/CMakeCache.txt").read_text(encoding="utf-8")
        self.assertRegex(cache, r"TRUSSC_ADDON_PATHS:[^=]+=\{\}")
        self.assertNotIn("tcxFixture", (self.project / f"build-{NATIVE}/fixture-links.txt").read_text(encoding="utf-8"))

    def test_addon_static_dependencies_link_parent_before_middle_before_leaf(self):
        paths = self.local_addons({"tcxParent": ["tcxZMiddle"], "tcxZMiddle": ["tcxALeaf"], "tcxALeaf": []})
        for name, code in {
            "tcxParent": "int middle(); int parent() { return middle(); }",
            "tcxZMiddle": "int leaf(); int middle() { return leaf(); }",
            "tcxALeaf": "int leaf() { return 42; }",
        }.items():
            (paths[name] / "src/value.cpp").write_text(code + "\n", encoding="utf-8")
        (self.project / "addons.make").write_text("tcxParent\n", encoding="utf-8")
        (self.project / "src/main.cpp").write_text("int parent(); int main() { return parent() == 42 ? 0 : 1; }\n", encoding="utf-8")
        self.cli("update")
        self.cli("build")
        self.assertEqual(subprocess.run([str(self.project / f"build-{NATIVE}/fixture")], check=False).returncode, 0)
        # Even an explicitly selected leaf must follow its parents on the link line.
        (self.project / "addons.make").write_text("tcxALeaf\ntcxParent\n", encoding="utf-8")
        self.cli("build")

    @unittest.skipUnless(os.name == "posix", "uses a POSIX offline curl sentinel")
    def test_addon_qualified_parent_then_registry_child(self):
        (self.addon / "addon.json").write_text(json.dumps({"dependencies": ["tcxFixture@v1"]}), encoding="utf-8")
        self.commit_addon()
        self.git("clone", "--bare", self.addon, self.base / "remotes/alice/tcxParent.git")
        registry = self.registry_path()
        body = registry.read_text(encoding="utf-8")
        commands = self.base / "commands"
        commands.mkdir()
        marker = self.base / "registry-requested"
        curl = commands / "curl"
        curl.write_text("#!/usr/bin/env python3\nfrom pathlib import Path\nPath(" + repr(str(marker)) +
                        ").touch()\nprint(" + repr(body) + ")\n", encoding="utf-8")
        curl.chmod(0o755)
        self.env["PATH"] = str(commands) + os.pathsep + self.env["PATH"]
        for command in (("addon", "add", "alice/tcxParent"), ("update",)):
            registry.unlink()
            marker.unlink(missing_ok=True)
            self.cli(*command)
            self.assertTrue(marker.exists())
            self.assertTrue(Path(self.paths()["tcxFixture"]).as_posix().endswith("/alice/tcxFixture/v1"))

    def test_addon_dependency_identity_resolves_owner_and_default_ref(self):
        parent = self.local_addons({"tcxParent": []})["tcxParent"]
        (self.project / "addons.make").write_text("tcxParent\n", encoding="utf-8")
        for refs in (("alice/tcxFixture@v1", "tcxFixture@v1"), ("alice/tcxFixture", "tcxFixture@HEAD")):
            (parent / "addon.json").write_text(json.dumps({"dependencies": refs}), encoding="utf-8")
            self.cli("update")
            self.cli("build", "--dry-run")
        installed = self.root() / "addons/tcxFixture"
        (installed / ".git").mkdir(parents=True)
        (installed / "addon.json").write_text(json.dumps({"owner": "alice"}), encoding="utf-8")
        (parent / "addon.json").write_text(json.dumps({"dependencies": ["tcxFixture", "alice/tcxFixture@HEAD"]}), encoding="utf-8")
        self.cli("update")
        self.assertEqual(Path(self.paths()["tcxFixture"]), installed)
        (self.project / "addons.make").write_text("tcxParent\ntcxFixture\n", encoding="utf-8")
        self.assertNotIn("conflicts with project", self.cli("update"))
        (self.project / "addons.make").write_text("tcxParent\n", encoding="utf-8")
        (parent / "addon.json").write_text(json.dumps({"dependencies": ["alice/tcxFixture@v1", "bob/tcxFixture@v1"]}), encoding="utf-8")
        self.assertIn("conflicting addon dependencies", self.cli("update", success=False))

    def test_addon_update_checks_project_before_any_writes(self):
        (self.project / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.20)\nproject(Foreign)\n", encoding="utf-8")
        presets = self.project / "CMakePresets.json"
        for body in (b'{"version":6,"vendor":{"foreign":{"keep":true}}}', b"{ malformed foreign presets"):
            presets.write_bytes(body)
            before = {p.relative_to(self.project): p.read_bytes() for p in self.project.rglob("*") if p.is_file()}
            self.assertIn("Refusing to overwrite", self.cli("update", success=False))
            after = {p.relative_to(self.project): p.read_bytes() for p in self.project.rglob("*") if p.is_file()}
            self.assertEqual(before, after)
            self.assertIn("Refusing to overwrite", self.cli("update", "--tc-root", self.source, success=False))
            self.assertEqual(presets.read_bytes(), body)

    def test_addon_build_rejects_nested_unacquired_ref(self):
        for archive in (False, True):
            if archive:
                fixture_rmtree(self.cache / "addons")
                self.env["TRUSSC_FETCH"] = "tarball"
                file = self.base / "remotes/alice/tcxFixture/archive/v1.tar.gz"
                file.parent.mkdir(parents=True)
                with tarfile.open(file, "w:gz") as output:
                    output.add(self.addon / "src", arcname="fixture/src")
                    output.add(self.addon / "addon.json", arcname="fixture/addon.json")
            self.cli("addon", "add", "alice/tcxFixture@v1")
            (Path(self.paths()["tcxFixture"]) / "sub/src").mkdir(parents=True, exist_ok=True)
            (self.project / "addons.make").write_text("alice/tcxFixture@v1/sub\n", encoding="utf-8")
            self.assertIn("run `trusscli update`", self.cli("build", "--dry-run", success=False))

    def test_addon_dependency_graph_has_one_aggregate_work_limit(self):
        parent = self.local_addons({"tcxParent": [], "tcxLeaf": []})["tcxParent"]
        (self.project / "addons.make").write_text("tcxParent\n", encoding="utf-8")
        for count, success in ((100, True), (4096, False)):
            (parent / "addon.json").write_text(json.dumps({"dependencies": ["tcxLeaf"] * count}), encoding="utf-8")
            output = self.cli("build", "--dry-run", success=success)
            if not success:
                self.assertIn("4096 node/edge limit", output)

    def test_addon_tags_branches_commits_forks_and_default(self):
        for text, owner, ref, commit in (("tcxFixture@v1", "alice", "v1", self.addon_initial),
                ("alice/tcxFixture@feature/next", "alice", "feature/next", self.addon_latest),
                ("alice/tcxFixture@" + self.addon_initial, "alice", self.addon_initial, self.addon_initial),
                ("bob/tcxFixture@v1", "bob", "v1", self.addon_initial),
                ("tcxFixture", "alice", "HEAD", self.addon_latest)):
            output = self.cli("addon", "add", text)
            self.assertIn("Warning: tcxFixture requires TrussC", output)
            expected = self.cache / "addons" / owner / "tcxFixture" / ref
            self.assertEqual(Path(self.paths()["tcxFixture"]), expected)
            self.assertEqual(self.git("-C", expected, "rev-parse", "HEAD").strip(), commit)
            self.assertIn(text, (self.project / "addons.make").read_text(encoding="utf-8"))
            self.assertEqual(len([l for l in (self.project / "addons.make").read_text(encoding="utf-8").splitlines() if l and not l.startswith('#')]), 1)

    @unittest.skipUnless(os.name == "posix", "uses a POSIX offline curl sentinel")
    def test_addon_owner_from_installed_metadata_without_registry(self):
        installed = self.root() / "addons/tcxFixture"
        (installed / ".git").mkdir(parents=True)
        (installed / "addon.json").write_text(json.dumps({"url": "https://github.com/alice/tcxFixture.git"}), encoding="utf-8")
        self.registry_path().unlink()
        commands = self.base / "commands"
        commands.mkdir()
        marker = self.base / "curl-called"
        curl = commands / "curl"
        curl.write_text("#!/usr/bin/env python3\nfrom pathlib import Path\nPath(" + repr(str(marker)) + ").touch()\nraise SystemExit(1)\n", encoding="utf-8")
        curl.chmod(0o755)
        self.env["PATH"] = str(commands) + os.pathsep + self.env["PATH"]
        self.cli("addon", "add", "tcxFixture@v1")
        self.assertFalse(marker.exists(), "installed metadata must avoid a registry request")
        self.assertTrue(Path(self.paths()["tcxFixture"]).as_posix().endswith('/alice/tcxFixture/v1'))
        self.cli("build", "--dry-run")
        self.assertFalse(marker.exists(), "build must never request the registry")

    def test_addon_local_precedence_clear_and_vendor_preservation(self):
        self.cli("addon", "add", "tcxFixture@v1")
        before = (self.project / "addons.make").read_bytes()
        self.cli("addon", "add-local", "tcxFixture", self.addon)
        self.assertEqual(Path(self.paths()["tcxFixture"]), self.addon)
        self.assertEqual((self.project / "addons.make").read_bytes(), before)
        self.cli("addon", "add-local", "tcxFixture", "--clear")
        self.assertEqual(Path(self.paths()["tcxFixture"]), self.cache / "addons/alice/tcxFixture/v1")
        self.assertTrue(json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))["vendor"]["fixture"]["keep"])

    def test_addon_local_selects_new_name_without_registry_or_fetch(self):
        output = self.cli("addon", "add-local", "tcxLocal", self.addon)
        self.assertEqual(Path(self.paths()["tcxLocal"]), self.addon)
        self.assertIn("tcxLocal\n", (self.project / "addons.make").read_text(encoding="utf-8"))
        self.assertNotIn(self.addon.as_posix(), (self.project / "addons.make").read_text(encoding="utf-8"))
        self.assertNotIn("Fetching addon registry", output)
        self.cli("build")

    def test_addon_build_never_fetches_and_missing_version(self):
        self.cli("addon", "add", "tcxFixture@v1")
        fixture_rmtree(self.base / "remotes")
        (self.project / "src/main.cpp").write_text('#include "value.h"\nint main() { return ADDON_VALUE == 1 ? 0 : 1; }\n', encoding="utf-8")
        self.cli("build")
        executable = self.project / f"build-{NATIVE}/fixture"
        self.assertEqual(subprocess.run([str(executable)], check=False).returncode, 0)
        for _ in range(2):
            self.assertIn("requires TrussC", self.cli("build", "--dry-run"))
        (self.project / "addons.make").write_text("alice/tcxFixture@missing\n", encoding="utf-8")
        self.assertIn("run `trusscli update`", self.cli("build", "--dry-run", success=False))

    def test_addon_rejects_bundled_and_malformed_without_editing_selection(self):
        original = (self.project / "addons.make").read_bytes()
        for spec in ("tcxBundled@v1", "alice/tcxFixture@../escape", "/tcxFixture@v1", "alice/tcxFixture@", "alice//tcxFixture@v1", "tcxFixture@@v1"):
            self.cli("addon", "add", spec, success=False)
            self.assertEqual((self.project / "addons.make").read_bytes(), original)

    def test_addon_dependency_conflict_project_wins_every_build(self):
        self.cli("addon", "add", "tcxFixture@v1")
        parent = self.base / "parent"
        parent.mkdir()
        (parent / "addon.json").write_text(json.dumps({"dependencies": [{"name": "tcxFixture", "tag": "different"}]}), encoding="utf-8")
        with (self.project / "addons.make").open('a', encoding="utf-8") as file:
            file.write("tcxParent\n")
        output = self.cli("addon", "add-local", "tcxParent", parent)
        self.assertIn("tcxFixture@different conflicts with project tcxFixture@v1", output)
        for _ in range(2):
            self.assertIn("project wins", self.cli("build", "--dry-run"))
        self.assertTrue(Path(self.paths()["tcxFixture"]).as_posix().endswith('/v1'))

    def test_addon_transitive_pin_is_fetched_and_loaded(self):
        parent = self.base / "parent"
        parent.mkdir()
        (parent / "addon.json").write_text(json.dumps({"dependencies": [{"name": "tcxFixture", "tag": "v1"}]}), encoding="utf-8")
        (self.project / "addons.make").write_text("tcxParent\n", encoding="utf-8")
        presets = self.project / "CMakePresets.json"
        data = json.loads(presets.read_text(encoding="utf-8"))
        data["vendor"]["trussc"]["addonLocalPaths"] = {"tcxParent": str(parent)}
        presets.write_text(json.dumps(data), encoding="utf-8")
        self.cli("update")
        self.assertTrue(Path(self.paths()["tcxFixture"]).as_posix().endswith('/v1'))
        self.assertNotIn("tcxFixture", (self.project / "addons.make").read_text(encoding="utf-8"))
        self.cli("build")

    def test_addon_version_ranges(self):
        (self.project / "addons.make").write_text("tcxFixture\n", encoding="utf-8")
        for text in (">=0.9.0 <2.0.0", "^1.0.0", "~1.0", "1.x", "*", ">=2.0.0 || ^1.0.0"):
            (self.addon / "addon.json").write_text(json.dumps({"trussc_version": text}), encoding="utf-8")
            output = self.cli("addon", "add-local", "tcxFixture", self.addon)
            self.assertNotIn("requires TrussC", output)
        for text in ("^0.9.0", "~1.1", "2.*", "<1.0.0"):
            (self.addon / "addon.json").write_text(json.dumps({"trussc_version": text}), encoding="utf-8")
            self.assertIn("requires TrussC", self.cli("build", "--dry-run"))

    def test_addon_default_branch_refreshes_only_on_update(self):
        self.cli("addon", "add", "tcxFixture")
        destination = Path(self.paths()["tcxFixture"])
        (self.addon / "src/value.h").write_text('#define ADDON_VALUE 3\n', encoding="utf-8")
        self.commit_addon()
        current = self.git("-C", self.addon, "rev-parse", "HEAD").strip()
        self.git("-C", self.base / "remotes/alice/tcxFixture.git", "fetch", self.addon, "+refs/heads/*:refs/heads/*")
        self.cli("build", "--dry-run")
        self.assertEqual(self.git("-C", destination, "rev-parse", "HEAD").strip(), self.addon_latest)
        self.cli("update")
        self.assertEqual(self.git("-C", destination, "rev-parse", "HEAD").strip(), current)

    def test_addon_missing_pin_can_be_overridden_locally(self):
        (self.project / "addons.make").write_text("alice/tcxFixture@unavailable\n", encoding="utf-8")
        self.cli("addon", "add-local", "tcxFixture", self.addon)
        self.assertEqual(Path(self.paths()["tcxFixture"]), self.addon)
        self.cli("build")
        self.assertIn("run `trusscli update`", self.cli("addon", "add-local", "tcxFixture", "--clear", success=False))

    def test_addon_old_trussc_stops(self):
        self.cli("addon", "add", "tcxFixture@v1")
        self.cli("use", "v1", success=False)
        output = self.last_result.stdout + self.last_result.stderr
        self.assertIn("TrussC v1 cannot load external addon paths", output)

    def test_addon_tarball_and_containment(self):
        self.env["TRUSSC_FETCH"] = "tarball"
        archive = self.base / "remotes/alice/tcxFixture/archive/v1.tar.gz"
        archive.parent.mkdir(parents=True)
        with tarfile.open(archive, "w:gz") as output:
            for item in (self.addon / "addon.json", self.addon / "src"):
                output.add(item, arcname="fixture/" + item.name)
        self.cli("addon", "add", "tcxFixture@v1")
        self.assertTrue(Path(self.paths()["tcxFixture"], '.git_archival.txt').is_file())
        self.cli("build")
        self.env.pop("TRUSSC_FETCH")
        outside = self.base / "outside"
        outside.mkdir()
        self.fixture_symlink(self.cache / "addons/bob", outside, target_is_directory=True)
        self.assertIn("cache path must stay", self.cli("addon", "add", "bob/tcxFixture@v1", success=False))


def load_tests(loader, tests, pattern):
    # Reuse stage 1 fixture methods; run its regression cases in their own suite.
    return unittest.TestSuite(AddonVersionTests(name) for name in loader.getTestCaseNames(AddonVersionTests)
                              if name.startswith("test_addon_"))


if __name__ == '__main__':
    unittest.main()
