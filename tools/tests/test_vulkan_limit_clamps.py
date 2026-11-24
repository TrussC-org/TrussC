"""Vulkan device limits clamp in unsigned before casting (AMD reports UINT32_MAX)."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOKOL = ROOT / "core/include/sokol/sokol_gfx.h"
FIELDS = ("maxVertexInputAttributes", "maxFragmentOutputAttachments",
          "maxPerStageDescriptorSampledImages", "maxPerStageDescriptorStorageBuffers",
          "maxPerStageDescriptorStorageImages")


def production_lines():
    text = SOKOL.read_text(encoding="utf-8")
    body = text[text.index("_SOKOL_PRIVATE void _sg_vk_init_caps(void) {"):]
    body = body[:body.index("\n}\n")]
    lines = [line for line in body.splitlines()
             if "_sg.limits." in line and any("l->" + f + "," in line for f in FIELDS)]
    minimum = re.search(r"^#define _sg_min\(a,b\).*$", text, re.M).group(0)
    return minimum, lines


class VulkanLimitClampTests(unittest.TestCase):
    def test_unsigned_max_clamps_to_sokol_constants(self):
        minimum, lines = production_lines()
        self.assertEqual(len(lines), len(FIELDS))
        source = "\n".join([
            '#include "sokol/sokol_gfx.h"',
            "#include <cstdint>",
            "#include <cstdio>",
            minimum,
            "struct Limits { uint32_t " + ", ".join(FIELDS) + "; };",
            "struct { sg_limits limits; } _sg;",
            "static void apply(const Limits* l) {",
            *lines,
            "}",
            "int main() {",
            "    const uint32_t big = UINT32_MAX;",
            "    Limits huge{big, big, big, big, big};",
            "    apply(&huge);",
            "    if (_sg.limits.max_vertex_attrs != SG_MAX_VERTEX_ATTRIBUTES) return 1;",
            "    if (_sg.limits.max_color_attachments != SG_MAX_COLOR_ATTACHMENTS) return 2;",
            "    if (_sg.limits.max_texture_bindings_per_stage != SG_MAX_VIEW_BINDSLOTS) return 3;",
            "    if (_sg.limits.max_storage_buffer_bindings_per_stage != SG_MAX_VIEW_BINDSLOTS) return 4;",
            "    if (_sg.limits.max_storage_image_bindings_per_stage != SG_MAX_VIEW_BINDSLOTS) return 5;",
            "    Limits small{4, 2, 3, 1, 0};",
            "    apply(&small);",
            "    if (_sg.limits.max_vertex_attrs != 4 || _sg.limits.max_color_attachments != 2) return 6;",
            "    if (_sg.limits.max_texture_bindings_per_stage != 3) return 7;",
            "    if (_sg.limits.max_storage_buffer_bindings_per_stage != 1) return 8;",
            "    if (_sg.limits.max_storage_image_bindings_per_stage != 0) return 9;",
            "    return 0;",
            "}",
        ])
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "clamp.cpp"
            exe = Path(tmp) / "clamp"
            cpp.write_text(source, encoding="utf-8")
            subprocess.run(["c++", "-std=c++17", "-I", str(ROOT / "core/include"), str(cpp), "-o", str(exe)],
                           check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)


if __name__ == "__main__":
    unittest.main()
