#!/usr/bin/env python3
"""Check that validation requires actual instance and device layer insertion."""
import unittest

from validate import (validation_environment, validation_environment_log,
                      validation_layer_issues, log_verdict)


INSTANCE = 'INFO | LAYER: Insert instance layer "VK_LAYER_KHRONOS_validation" (libVkLayer_khronos_validation.so)\n'
DEVICE = 'INFO | LAYER: Inserted device layer "VK_LAYER_KHRONOS_validation" (libVkLayer_khronos_validation.so)\n'
MISSING = ('ERROR | LAYER: Layer "VK_LAYER_KHRONOS_validation" was not found '
           'but was requested by env var VK_INSTANCE_LAYERS!\n')


class ValidationLayerTest(unittest.TestCase):
    def test_controlled_environment(self):
        inherited = {"VK_LAYER_DISABLES": "VK_VALIDATION_FEATURE_DISABLE_ALL_EXT",
                     "VK_LAYER_ENABLES": "other", "VK_LAYER_SETTINGS_PATH": "inherited",
                     "VK_KHRONOS_VALIDATION_VALIDATE_CORE": "false",
                     "VK_KHRONOS_VALIDATION_REPORT_FLAGS": "none",
                     "VK_INSTANCE_LAYERS": "other", "DISPLAY": ":93163"}
        env = validation_environment(inherited, "/empty/vk_layer_settings.txt")
        self.assertEqual(env, {"DISPLAY": ":93163",
                              "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
                              "VK_LOADER_DEBUG": "layer",
                              "VK_LAYER_SETTINGS_PATH": "/empty/vk_layer_settings.txt"})
        self.assertIn("VK_LAYER_SETTINGS_PATH=/empty/vk_layer_settings.txt\n",
                      validation_environment_log(env))
        self.assertNotIn("DISPLAY", validation_environment_log(env))

    def test_both_insertions(self):
        self.assertEqual(validation_layer_issues(INSTANCE + DEVICE), [])

    def test_missing_layer(self):
        log = MISSING + 'VK_PROBE close swapchains=0 images=0 views=0 memory=0 semaphores=0\n'
        self.assertIn("Khronos validation layer unavailable (see log)", validation_layer_issues(log))
        self.assertTrue(validation_layer_issues(MISSING + INSTANCE + DEVICE))

    def test_no_or_partial_insertion(self):
        for log in ("", INSTANCE, DEVICE,
                    'WARNING | LAYER: env var VK_INSTANCE_LAYERS adding VK_LAYER_KHRONOS_validation\n'):
            with self.subTest(log=log):
                self.assertTrue(validation_layer_issues(log))


class WindowsLogTest(unittest.TestCase):
    def setUp(self):
        self.log = INSTANCE + DEVICE + (
            "Vulkan host: device=Fixture GPU\n" +
            "Vulkan host: swapchain created\n" * 25 +
            "Vulkan host: teardown complete\n" +
            "vulkanSingleWindow: app checks complete\n" +
            "vulkanSingleWindow: WM_CLOSE posted: PASS\n" +
            "vulkanSingleWindow: 24 resizes completed; ready for close\n" +
            "".join(f"vulkanSingleWindow: live {name} before=2 after=2\n"
                    for name in ("buffers", "images", "views", "samplers", "shaders", "pipelines")) +
            "".join(f"vulkanSingleWindow: stencil draw ref={ref}\n" for ref in (1, 73, 255)))

    def verdict(self, log, code=0):
        return log_verdict(log, ["--resize", "--stencil"], code, windows=True, wm_close=True)[0]

    def test_complete_app_evidence(self):
        self.assertEqual(self.verdict(self.log), [])

    def test_disabled_layer_log(self):
        # Review reproduction: both insertion messages remain even with all checks disabled.
        disabled = ("VK_LAYER_DISABLES=VK_VALIDATION_FEATURE_DISABLE_ALL_EXT\n" +
                    INSTANCE + DEVICE)
        self.assertTrue(self.verdict(self.log.replace(INSTANCE + DEVICE, disabled)))
        for setting in ("VK_LAYER_DISABLES = core_checks",
                        "VK_KHRONOS_VALIDATION_VALIDATE_CORE=false",
                        "VK_VALIDATION_FEATURE_DISABLE_ALL_EXT"):
            with self.subTest(setting=setting):
                self.assertTrue(self.verdict(self.log + setting + "\n"))

    def test_incomplete_or_failed_evidence(self):
        for marker in (INSTANCE, DEVICE, "Vulkan host: device=Fixture GPU\n",
                       "Vulkan host: teardown complete\n", "vulkanSingleWindow: app checks complete\n",
                       "vulkanSingleWindow: WM_CLOSE posted: PASS\n",
                       "vulkanSingleWindow: stencil draw ref=73\n",
                       "vulkanSingleWindow: live views before=2 after=2\n"):
            with self.subTest(marker=marker):
                self.assertTrue(self.verdict(self.log.replace(marker, "")))
        self.assertTrue(self.verdict(self.log, 1))
        self.assertTrue(self.verdict(self.log + "Validation Error: VUID-fixture\n"))
        self.assertTrue(self.verdict(self.log + "Validation Performance Warning: fixture\n"))
        self.assertTrue(self.verdict(self.log.replace("views before=2 after=2", "views before=2 after=3")))
        self.assertTrue(self.verdict(self.log.replace("swapchain created", "no creation", 1)))
        self.assertTrue(self.verdict(self.log + "vulkanSingleWindow: fixture: FAIL\n"))


if __name__ == "__main__":
    unittest.main()
