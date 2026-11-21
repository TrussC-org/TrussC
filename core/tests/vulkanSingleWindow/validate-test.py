#!/usr/bin/env python3
"""Check that validation requires actual instance and device layer insertion."""
import unittest

from validate import validation_layer_issues


INSTANCE = 'INFO | LAYER: Insert instance layer "VK_LAYER_KHRONOS_validation" (libVkLayer_khronos_validation.so)\n'
DEVICE = 'INFO | LAYER: Inserted device layer "VK_LAYER_KHRONOS_validation" (libVkLayer_khronos_validation.so)\n'
MISSING = ('ERROR | LAYER: Layer "VK_LAYER_KHRONOS_validation" was not found '
           'but was requested by env var VK_INSTANCE_LAYERS!\n')


class ValidationLayerTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
