"""Offline fixtures for the allCoreTests nm check."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "check_core_test_symbols", Path(__file__).resolve().parents[1] / "check_core_test_symbols.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class CoreTestSymbolTests(unittest.TestCase):
    def check_listings(self, listings, demangled=None):
        objects = {Path("first.o"): "firstTest", Path("second.o"): "secondTest"}
        demangled = demangled or {}
        output = io.StringIO()
        nm_outputs = iter(listings)

        def run(command, **kwargs):
            if command == ["c++filt"]:
                names = kwargs["input"].splitlines()
                listing = "".join(demangled.get(name, name) + "\n" for name in names)
            else:
                listing = next(nm_outputs)
            return subprocess.CompletedProcess(command, 0, listing, "")

        with patch.object(check.subprocess, "run", side_effect=run) as commands, \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = check.check_objects(objects)
        calls = commands.call_args_list
        self.assertEqual([call.args[0] for call in calls[:2]], [
            ["nm", "-g", "--defined-only", str(obj)] for obj in objects])
        self.assertLessEqual(len(calls), 3)
        if len(calls) == 3:
            self.assertEqual(calls[2].args[0], ["c++filt"])
        return status, output.getvalue()

    def assert_collision_message(self, status, message, name):
        self.assertEqual(status, 1)
        for text in (name, "firstTest: first.o", "secondTest: second.o",
                     "put test helpers in an anonymous namespace",
                     "helpers shared through core/tests/common/ headers go in namespace tcCoreTest"):
            self.assertIn(text, message)
        self.assertEqual(message.splitlines()[-1],
                         "If this is a library namespace, add one line to the list in "
                         "tools/check_core_test_symbols.py")

    def test_library_weak_duplicates_pass(self):
        listing = ("00000000 W _ZNSt6vectorIiSaIiEE5clearEv\n"
                   "00000000 u _ZN6trussc6colors3redE\n")
        status, message = self.check_listings([listing, listing], {
            "_ZNSt6vectorIiSaIiEE5clearEv": "std::vector<int, std::allocator<int> >::clear()",
            "_ZN6trussc6colors3redE": "trussc::colors::red"})
        self.assertEqual(status, 0)
        self.assertIn("2 objects; no duplicates", message)

    def test_shared_inline_definition_fails_with_names_objects_and_fix(self):
        for kind in ("W", "V", "u"):
            with self.subTest(kind=kind):
                status, message = self.check_listings([
                    f"00000000 {kind} _Z12sharedHelperv\n00000000 T firstOnly\n",
                    f"00000010 {kind} _Z12sharedHelperv\n00000000 T secondOnly\n"],
                    {"_Z12sharedHelperv": "sharedHelper()"})
                self.assert_collision_message(status, message, "sharedHelper()")
                self.assertNotIn("firstOnly", message)

    def test_strong_duplicates_fail_even_in_exempt_namespaces(self):
        for kind in ("T", "D", "B", "R"):
            for name in ("sharedHelper()", "std::helper()", "trussc::helper()",
                         "tcCoreTest::helper()", "DW.ref.helper"):
                with self.subTest(kind=kind, name=name):
                    listing = f"00000000 {kind} symbol\n"
                    status, message = self.check_listings([listing, listing], {"symbol": name})
                    self.assert_collision_message(status, message, name)
        status, message = self.check_listings(
            ["00000000 W symbol\n", "00000000 T symbol\n"], {"symbol": "std::helper()"})
        self.assert_collision_message(status, message, "std::helper()")

    def test_clean_listings(self):
        status, message = self.check_listings([
            "00000000 T firstOnly\n00000000 W firstInline\n",
            "00000000 V secondVariable\n00000000 u secondUnique\n"])
        self.assertEqual(status, 0)
        self.assertIn("2 objects; no duplicates", message)

    def test_registration_and_exception_references_pass(self):
        listing = ("00000000 W _ZN10tcCoreTest6invokeEPFivEiPPc\n"
                   "00000000 V _ZTIN10tcCoreTest8RegistryE\n"
                   "00000000 V DW.ref.__gxx_personality_v0\n")
        status, _ = self.check_listings([listing, listing], {
            "_ZN10tcCoreTest6invokeEPFivEiPPc": "tcCoreTest::invoke(int (*)(), int, char**)",
            "_ZTIN10tcCoreTest8RegistryE": "typeinfo for tcCoreTest::Registry"})
        self.assertEqual(status, 0)

    def test_prefixed_library_duplicates_pass(self):
        # Real RTTI/vtable manglings, plus every required demangled prefix.
        status, _ = self.check_listings([
            "00000000 V _ZTVN6trussc1XE\n00000000 V _ZTIN6trussc1XE\n"] * 2, {
                "_ZTVN6trussc1XE": "vtable for trussc::X",
                "_ZTIN6trussc1XE": "typeinfo for trussc::X"})
        self.assertEqual(status, 0)
        for prefix in check.DEMANGLED_PREFIXES:
            with self.subTest(prefix=prefix):
                status, _ = self.check_listings(["00000000 V symbol\n"] * 2,
                                                {"symbol": prefix + "trussc::X"})
                self.assertEqual(status, 0)

    def test_function_type_rtti_weak_duplicates_pass(self):
        for prefix in ("typeinfo for ", "typeinfo name for "):
            for type_name in ("void (int)", "void (*)(int)",
                              "long (httplib::Stream&, std::unordered_multimap<int, int> const&)",
                              "long (*)(httplib::Stream&, std::unordered_multimap<int, int> const&)",
                              "Helper<void (*)(int)> (*)(int)"):
                for kind in ("W", "V", "u", "T", "D", "B", "R"):
                    with self.subTest(prefix=prefix, type_name=type_name, kind=kind):
                        name = prefix + type_name
                        status, message = self.check_listings(
                            [f"00000000 {kind} symbol\n"] * 2, {"symbol": name})
                        if kind in check.STRONG_KINDS:
                            self.assert_collision_message(status, message, name)
                        else:
                            self.assertEqual(status, 0)

    def test_function_rtti_exclusion_does_not_hide_helpers(self):
        for name in ("typeinfo for Helper<void (int)>",
                     "typeinfo name for Helper<void (*)(int)>",
                     "typeinfo for Helper<Nested<void (*)(int)> >",
                     "typeinfo for Helper", "typeinfo name for Helper",
                     "vtable for Helper<void (*)(int)>",
                     "helper(void (*)(int))", "guard variable for helper(int)::value"):
            with self.subTest(name=name):
                status, message = self.check_listings(["00000000 V symbol\n"] * 2,
                                                    {"symbol": name})
                self.assert_collision_message(status, message, name)

    def test_entity_scope_ignores_return_types_arguments_and_abi_tags(self):
        cases = {
            "void std::foo<trussc::X>(trussc::X)": "std",
            "trussc::X* trussc::Y::f()": "trussc",
            "std::vector<trussc::X> const& helpers::make<int>()": "helpers",
            "std::string helper<trussc::X>(std::string)": None,
            "helper[abi:cxx11](std::string)": None,
            "Helper<std::string>::run()": "Helper",
            "helpers::std::run()": "helpers",
            "stdExtra::run()": "stdExtra",
            "bool std::operator< <int>(std::X<int>, std::X<int>)": "std",
            "trussc::X::operator std::string() const": "trussc",
            "std::X::operator()() const": "std",
            "std::X::operator[](int)": "std",
            "void helper<void (*)(std::string)>(int)": None,
            "void std::foo<void (*)(helpers::X)>(int)": "std",
            "decltype (helpers::f()) std::foo<int>()": "std",
            "std::X::f() const::{lambda()#1}::operator()() const": "std",
            "helper(std::X)::{lambda()#1}::operator()() const": None,
            "construction vtable for trussc::Base-in-helpers::Derived": "trussc",
            "vtable for Helper<std::string>": None,
        }
        for name, scope in cases.items():
            with self.subTest(name=name):
                self.assertEqual(check.outermost_scope(name), scope)
                status, _ = self.check_listings(["00000000 W symbol\n"] * 2, {"symbol": name})
                self.assertEqual(status, 0 if scope in check.EXCLUDED_WEAK_NAMESPACES else 1)

    def test_all_listed_namespaces_exempt_only_weak_definitions(self):
        for scope in check.EXCLUDED_WEAK_NAMESPACES:
            for kind in ("W", "V", "u"):
                with self.subTest(scope=scope, kind=kind):
                    status, _ = self.check_listings([f"00000000 {kind} symbol\n"] * 2,
                                                    {"symbol": f"void {scope}::f<int>()"})
                    self.assertEqual(status, 0)

    def test_collisions_between_helper_objects_of_same_test(self):
        with patch.object(check, "demangle_symbols", side_effect=lambda names: dict.fromkeys(names, "helper")):
            duplicates, _ = check.find_duplicates({
                Path("main.o"): ("firstTest", "00000000 W helper\n00000000 W helper\n"),
                Path("helper.o"): ("firstTest", "00000000 W helper\n")})
            self.assertEqual(len(duplicates["helper"]), 2)
            duplicates, _ = check.find_duplicates({
                Path("main.o"): ("firstTest", "00000000 W helper\n00000000 W helper\n")})
            self.assertEqual(duplicates, {})

    def test_raw_identities_are_not_merged_by_demangling(self):
        status, _ = self.check_listings(
            ["00000000 W constructor1\n", "00000000 W constructor2\n"],
            {"constructor1": "Helper::Helper()", "constructor2": "Helper::Helper()"})
        self.assertEqual(status, 0)

    def test_demangler_errors_fail_closed(self):
        for error in (FileNotFoundError("c++filt"),
                      subprocess.CalledProcessError(1, "c++filt", stderr="failed")):
            with patch.object(check.subprocess, "run", side_effect=error), \
                    self.assertRaises(type(error)):
                check.demangle_symbols(["helper"])
        with patch.object(check.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")), \
                self.assertRaisesRegex(ValueError, r"Unexpected c\+\+filt output"):
            check.demangle_symbols(["helper"])

    def test_database_selects_all_helper_sources_but_not_common_or_own_binary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tests = root / "core/tests"
            build = root / "build with spaces"
            build.mkdir()
            entries = []
            for name in ("combined", "own", "allCoreTests", "common"):
                directory = tests / name / "src"
                directory.mkdir(parents=True)
                (directory / "main.cpp").touch()
                if name == "own":
                    (directory.parent / "own-binary").touch()
                for source in ("main.cpp", "helper.c"):
                    entries.append(dict(directory=str(build), file=str(directory / source),
                                        command=f"c++ -c '{directory / source}' -o '{name}/{source}.o'"))
            entries.append(dict(directory=str(build), file=str(tests / "common/tcHeadlessSokol.cpp"),
                                arguments=["c++", "-o", "common.o"]))
            # Exercise the alternate compilation-database output representation.
            entries[1]["output"] = "combined/helper.c.o"
            del entries[1]["command"]
            entries.append(dict(directory=str(build), file=str(tests / "combined/src/nested/extra.cpp"),
                                arguments=["c++", "-o", "combined/extra.o"]))
            # A stale object absent from the database must not be inspected.
            (build / "stale.o").touch()
            (build / "compile_commands.json").write_text(json.dumps(entries))
            self.assertEqual(check.combined_objects(build, tests), {
                build / "combined/main.cpp.o": "combined",
                build / "combined/helper.c.o": "combined",
                build / "combined/extra.o": "combined"})

    def test_unusable_database_and_nm_errors_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(check.main([str(build)]), 1)
                (build / "compile_commands.json").write_text("[]")
                self.assertEqual(check.main([str(build)]), 1)
                for error in (FileNotFoundError("nm"),
                              subprocess.CalledProcessError(1, "nm", stderr="object missing")):
                    with patch.object(check, "combined_objects", return_value={Path("missing.o"): "test"}), \
                            patch.object(check.subprocess, "run", side_effect=error):
                        self.assertEqual(check.main([str(build)]), 1)
        with self.assertRaisesRegex(ValueError, "Unexpected nm output"):
            check.defined_symbols("not a valid nm listing")


if __name__ == "__main__":
    unittest.main()
