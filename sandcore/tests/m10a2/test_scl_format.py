#!/usr/bin/env python3
"""mio：针对磁盘格式边界、真实 ELF 重定位与 C/Python 合同的一轮宿主检查。"""

from __future__ import annotations

import copy
import ctypes
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mkscl
import scl_format as scl
import scldump


class SclFormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if shutil.which("gcc") is None:
            raise unittest.SkipTest("freestanding GCC is required; run this suite in WSL")
        cls.temporary = tempfile.TemporaryDirectory(prefix="sandcore-scl-format-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        flags = ["-m32", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffreestanding",
                 "-fno-builtin", "-fno-pie", "-fno-pic", "-fno-stack-protector", "-fno-common",
                 "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-msoft-float", "-mno-sse", "-mno-mmx"]
        cls.objects, cls.manifests, cls.blobs = {}, {}, {}
        for name in ("base", "upper"):
            source = ROOT / "tests" / "m10a2" / f"lib{name}.c"
            target = cls.directory / f"lib{name}.o"
            subprocess.run(["gcc", *flags, "-c", str(source), "-o", str(target)], check=True, capture_output=True)
            manifest = mkscl.load_manifest(source.with_suffix(".json"))
            cls.objects[name], cls.manifests[name] = target.read_bytes(), manifest
            cls.blobs[name] = mkscl.pack_elf(cls.objects[name], manifest)
            (cls.directory / manifest["name"]).write_bytes(cls.blobs[name])
        shared = cls.directory / "validator.so"
        subprocess.run(["gcc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror", "-ffreestanding",
                        "-fno-builtin", "-fno-stack-protector", "-fno-tree-loop-distribute-patterns",
                        str(ROOT / "user" / "runtime" / "scl_format.c"), "-o", str(shared)],
                       check=True, capture_output=True)
        cls.validator = ctypes.CDLL(str(shared)).scl_validate
        cls.validator.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        cls.validator.restype = ctypes.c_int

    def verdicts(self, blob: bytes) -> tuple[int, int]:
        try:
            scl.validate(blob)
            python = 0
        except scl.SclError as exc:
            python = exc.code
        memory = ctypes.create_string_buffer(blob)
        return python, self.validator(memory, len(blob))

    def test_real_elf_and_c_validator(self) -> None:
        for blob in self.blobs.values():
            self.assertEqual(self.verdicts(blob), (0, 0))
        upper = scl.validate(self.blobs["upper"])
        self.assertEqual({(r["type"], r["target_kind"]) for r in upper["relocations"]},
                         {(scl.ABS32, scl.INTERNAL), (scl.PC32, scl.INTERNAL),
                          (scl.ABS32, scl.IMPORT), (scl.PC32, scl.IMPORT)})
        self.assertEqual({s["flags"] for s in upper["segments"]}, {scl.SEG_R, scl.SEG_R | scl.SEG_W, scl.SEG_R | scl.SEG_X})
        self.assertTrue(any(s["file_size"] == 0 for s in upper["segments"]))
        self.assertEqual(mkscl.pack_elf(self.objects["upper"], self.manifests["upper"]), self.blobs["upper"])

    def test_loaded_pointer_bytes_at_two_bases(self) -> None:
        base = scl.validate(self.blobs["base"])
        upper = scl.validate(self.blobs["upper"])
        be = {e["name"]: e for e in base["exports"]}
        ue = {e["name"]: e for e in upper["exports"]}
        for bbase, ubase in ((0x10000000, 0x12000000), (0x26000000, 0x32000000)):
            imports = [bbase + be[i["name"]]["rva"] for i in upper["imports"]]
            image = scl.relocated_image(self.blobs["upper"], ubase, imports)
            for pointer, expected in (("upper_base_pointer", bbase + be["base_value"]["rva"]),
                                      ("upper_add_pointer", bbase + be["base_add"]["rva"]),
                                      ("upper_value_pointer", ubase + ue["upper_value"]["rva"]),
                                      ("upper_message_pointer", ubase + ue["upper_message"]["rva"])):
                self.assertEqual(struct.unpack_from("<I", image, ue[pointer]["rva"])[0], expected)
            self.assertEqual(image[ue["upper_message"]["rva"]:ue["upper_message"]["rva"] + 10], b"SCL upper\0")
            self.assertEqual(image[ue["upper_bss"]["rva"]:ue["upper_bss"]["rva"] + 16], bytes(16))
            for r in upper["relocations"]:
                if r["type"] == scl.PC32 and r["target_kind"] == scl.IMPORT:
                    expected = (imports[r["target"]] + r["addend"] - ubase - r["patch_rva"]) & 0xFFFFFFFF
                    self.assertEqual(struct.unpack_from("<I", image, r["patch_rva"])[0], expected)

    def test_format_rejection_agrees_in_both_languages(self) -> None:
        source = self.blobs["upper"]
        words = lambda offset: struct.unpack_from("<I", source, offset)[0]
        export, imports, relocs, strings = (words(n) for n in (56, 64, 72, 88))
        mutations = [
            ("header reserved", 104, 1, scl.E_FORMAT),
            ("image overflow", 28, 0xFFFFF000, scl.E_RANGE),
            ("table offset", 56, export + 4, scl.E_RANGE),
            ("table count overflow", 52, 0xFFFFFFFF, scl.E_RANGE),
            ("writable executable", 128 + 16, 7, scl.E_FORMAT),
            ("duplicate exports", export + 20, words(export), scl.E_SYMBOL),
            ("interior string", export, words(export) + 1, scl.E_SYMBOL),
            ("import dependency", imports + 4, 0xFFFFFFFF, scl.E_RANGE),
            ("relocation type", relocs + 4, 3, scl.E_FORMAT),
            ("overlapping relocation", relocs + 20, words(relocs), scl.E_RANGE),
            ("init is data", 96, next(e["rva"] for e in scl.validate(source)["exports"] if e["name"] == "upper_value"), scl.E_RANGE),
        ]
        for label, offset, value, expected in mutations:
            with self.subTest(label=label):
                blob = bytearray(source)
                struct.pack_into("<I", blob, offset, value)
                self.assertEqual(self.verdicts(bytes(blob)), (expected, expected))
        for label, blob, expected in (("truncated", source[:127], scl.E_RANGE),
                                      ("wrong exact file size", source[:-1], scl.E_RANGE),
                                      ("bad magic", b"X" + source[1:], scl.E_FORMAT),
                                      ("bad library name", source[:strings] + b"b" + source[strings + 1:], scl.E_SYMBOL)):
            with self.subTest(label=label):
                self.assertEqual(self.verdicts(blob), (expected, expected))
        trailing = bytearray(source + b"\x01")
        struct.pack_into("<I", trailing, 24, len(trailing))
        self.assertEqual(self.verdicts(bytes(trailing)), (scl.E_FORMAT, scl.E_FORMAT))

    def test_elf_subset_and_manifest_fail_closed(self) -> None:
        for label, offset, value in (("ELF64", 4, 2), ("big endian", 5, 2), ("ET_DYN", 16, 3)):
            blob = bytearray(self.objects["upper"])
            blob[offset] = value
            with self.subTest(label=label), self.assertRaises(mkscl.PackError):
                mkscl.pack_elf(bytes(blob), self.manifests["upper"])
        elf = mkscl.ElfObject(self.objects["upper"])
        table = next(s for s in elf.sections if s.kind == mkscl.SHT_REL and elf.sections[s.info].flags & mkscl.SHF_ALLOC)
        blob = bytearray(self.objects["upper"])
        info = struct.unpack_from("<I", blob, table.offset + 4)[0]
        struct.pack_into("<I", blob, table.offset + 4, (info & ~255) | 3)
        with self.assertRaises(mkscl.PackError):
            mkscl.pack_elf(bytes(blob), self.manifests["upper"])
        manifest = copy.deepcopy(self.manifests["upper"])
        manifest["imports"].pop()
        with self.assertRaises(mkscl.PackError):
            mkscl.pack_elf(self.objects["upper"], manifest)
        manifest = copy.deepcopy(self.manifests["upper"])
        manifest["init"] = "upper_value"
        with self.assertRaises(mkscl.PackError):
            mkscl.pack_elf(self.objects["upper"], manifest)
        manifest = copy.deepcopy(self.manifests["upper"])
        manifest["abi_major"] = True
        with self.assertRaises(mkscl.PackError):
            mkscl.pack_elf(self.objects["upper"], manifest)
        manifest = copy.deepcopy(self.manifests["upper"])
        manifest["exports"].append(manifest["exports"][0])
        with self.assertRaises(mkscl.PackError):
            mkscl.pack_elf(self.objects["upper"], manifest)

    def test_import_sort_preserves_original_relocation_indices(self) -> None:
        manifest = copy.deepcopy(self.manifests["upper"])
        manifest["imports"].reverse()
        self.assertEqual(mkscl.pack_elf(self.objects["upper"], manifest), self.blobs["upper"])

    def test_recursive_dependency_check_and_diagnostics(self) -> None:
        root = scldump.inspect_file(self.directory / "UPPER.SCL")
        self.assertEqual(scldump.check_dependencies(root, self.directory)["initialization_order"], ["BASE.SCL", "UPPER.SCL"])
        with tempfile.TemporaryDirectory(prefix="sandcore-scl-deps-") as temporary:
            directory = Path(temporary)
            with self.assertRaises(scl.SclError) as failure:
                scldump.check_dependencies(root, directory)
            self.assertEqual(failure.exception.code, -5)
            base = bytearray(self.blobs["base"])
            struct.pack_into("<I", base, 32, 2)
            (directory / "BASE.SCL").write_bytes(base)
            with self.assertRaises(scl.SclError) as failure:
                scldump.check_dependencies(root, directory)
            self.assertEqual(failure.exception.code, scl.E_VERSION)
            manifest = copy.deepcopy(self.manifests["base"])
            manifest["exports"] = [e for e in manifest["exports"] if e["name"] != "base_add"]
            (directory / "BASE.SCL").write_bytes(mkscl.pack_elf(self.objects["base"], manifest))
            with self.assertRaises(scl.SclError) as failure:
                scldump.check_dependencies(root, directory)
            self.assertEqual(failure.exception.code, scl.E_SYMBOL)
            manifest = copy.deepcopy(self.manifests["base"])
            manifest["dependencies"] = [dict(name="UPPER.SCL", abi_major=1, min_minor=0, min_version=1)]
            (directory / "BASE.SCL").write_bytes(mkscl.pack_elf(self.objects["base"], manifest))
            with self.assertRaises(scl.SclError) as failure:
                scldump.check_dependencies(root, directory)
            self.assertEqual(failure.exception.code, -7)

    def test_dump_cli_is_machine_readable(self) -> None:
        result = subprocess.run([sys.executable, str(ROOT / "tools" / "scldump.py"),
                                 str(self.directory / "UPPER.SCL"), "--json", "--deps", str(self.directory)],
                                check=True, capture_output=True, text=True)
        parsed = json.loads(result.stdout)
        self.assertEqual(parsed["name"], "UPPER.SCL")
        self.assertEqual(parsed["dependency_check"]["status"], "PASS")
        self.assertEqual(len(parsed["imports"]), 2)
        self.assertTrue(parsed["strings"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
