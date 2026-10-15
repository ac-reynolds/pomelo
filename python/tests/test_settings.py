import os
import subprocess
import sys
import threading
import unittest

import pomelo


class SettingsTests(unittest.TestCase):
    def test_defaults_and_ownership(self):
        first = pomelo.default_settings()
        second = pomelo.Settings()
        self.assertAlmostEqual(first.domain_box_opacity, 0.65)
        self.assertEqual(first.sources, second.sources)
        self.assertEqual(len(first.sources), 1)
        self.assertIsInstance(first.sources[0], tuple)
        self.assertEqual(pomelo.Settings(sources=None).sources, first.sources)
        copied = first.sources
        copied.clear()
        self.assertEqual(len(first.sources), 1)
        original = [[0, -0.85, 0]]
        first.sources = original
        original[0][0] = 0.5
        self.assertEqual(first.sources[0][0], 0)
        first.sources = []
        self.assertEqual(len(second.sources), 1)
        self.assertIn('Settings(domain_box_opacity=', repr(second))
        self.assertFalse(hasattr(pomelo, 'validate'))
        self.assertEqual(first.preset, 'smoke')
        self.assertTrue(first.thermal_buoyancy)
        self.assertAlmostEqual(first.density_dissipation, 0.2)
        self.assertEqual(first.camera_yaw, 0)
        self.assertAlmostEqual(first.camera_pitch, -0.35)
        self.assertEqual(first.camera_zoom, 1)
        self.assertEqual(first.pressure_iterations, 64)

    def test_demo_settings(self):
        for preset in ['smoke', 'taylor_green', 'double_shear_layer', 'vortex_ring']:
            settings = pomelo.Settings(preset=preset, thermal_buoyancy=False,
                                      density_dissipation=0, camera_yaw=0.6,
                                      camera_pitch=-0.4, camera_zoom=1.5,
                                      pressure_iterations=512, sources=[])
            self.assertEqual(settings.preset, preset)
            self.assertFalse(settings.thermal_buoyancy)
            self.assertEqual(settings.density_dissipation, 0)
            self.assertAlmostEqual(settings.camera_yaw, 0.6)
            self.assertAlmostEqual(settings.camera_pitch, -0.4)
            self.assertEqual(settings.camera_zoom, 1.5)
            self.assertEqual(settings.pressure_iterations, 512)
            settings.pressure_iterations = 129
            self.assertEqual(settings.pressure_iterations, 129)
            self.assertIn(preset, repr(settings))
            settings.preset = 'taylor_green'
            settings.thermal_buoyancy = True
            settings.density_dissipation = 0.5
            settings.camera_yaw = -0.5
            settings.camera_pitch = 0.2
            self.assertTrue(settings.thermal_buoyancy)
            self.assertEqual(settings.density_dissipation, 0.5)
            self.assertEqual(settings.camera_yaw, -0.5)
        with self.assertRaisesRegex(ValueError, 'preset'):
            pomelo.Settings(preset='unknown')
        for value in [False, 1, None]:
            with self.assertRaisesRegex(TypeError, 'preset'):
                pomelo.Settings(preset=value)
        for value in [0, 1, 'false', None]:
            with self.assertRaisesRegex(TypeError, 'thermal_buoyancy'):
                pomelo.Settings(thermal_buoyancy=value)
        for value in [True, 64.0, 1.5, '64', None]:
            with self.assertRaisesRegex(TypeError, 'pressure_iterations'):
                pomelo.Settings(pressure_iterations=value)
        for value in [0, -1, 4097, 10**1000]:
            with self.assertRaisesRegex(ValueError, 'pressure_iterations'):
                pomelo.Settings(pressure_iterations=value)
        self.assertEqual(pomelo.Settings(pressure_iterations=1).pressure_iterations, 1)
        self.assertEqual(pomelo.Settings(pressure_iterations=4096).pressure_iterations, 4096)
        for field in ['density_dissipation', 'camera_yaw', 'camera_pitch', 'camera_zoom']:
            for value in [False, '0.5', None]:
                with self.assertRaisesRegex(TypeError, field):
                    pomelo.Settings(**{field: value})
            for value in [float('nan'), float('inf'), 1e40]:
                with self.assertRaisesRegex(ValueError, field):
                    pomelo.Settings(**{field: value})

    def test_constructor_and_conversion(self):
        with self.assertRaises(TypeError):
            pomelo.Settings(0.5)
        for value in [True, '0.5', None, object()]:
            with self.subTest(value=value), self.assertRaisesRegex(TypeError, 'domain_box_opacity'):
                pomelo.Settings(domain_box_opacity=value)
        for value in [float('nan'), float('inf'), 1e40, 10**1000]:
            with self.subTest(value=str(value)), self.assertRaisesRegex(ValueError, 'domain_box_opacity'):
                pomelo.Settings(domain_box_opacity=value)
        for value in [True, '0', None]:
            with self.assertRaisesRegex(TypeError, r'sources\[1\].y'):
                pomelo.Settings(sources=[(0, 0, 0), (0, value, 0)])
        for value in [float('nan'), float('-inf'), 1e40]:
            with self.assertRaisesRegex(ValueError, r'sources\[0\].z'):
                pomelo.Settings(sources=[(0, 0, value)])
        for value in [[], [0, 0], [0, 0, 0, 0]]:
            with self.assertRaisesRegex(ValueError, r'sources\[0\]'):
                pomelo.Settings(sources=[value])
        for value in ['abc', 3, True]:
            with self.assertRaisesRegex(TypeError, 'sources'):
                pomelo.Settings(sources=value)

    def test_assignment_is_atomic(self):
        settings = pomelo.Settings()
        before = settings.sources
        with self.assertRaises(ValueError):
            settings.sources = [(0, 0, 0), (0, 0)]
        self.assertEqual(settings.sources, before)
        with self.assertRaises(TypeError):
            settings.domain_box_opacity = False
        self.assertAlmostEqual(settings.domain_box_opacity, 0.65)
        with self.assertRaises(ValueError):
            settings.preset = 'unknown'
        self.assertEqual(settings.preset, 'smoke')
        with self.assertRaises(TypeError):
            settings.thermal_buoyancy = 0
        self.assertTrue(settings.thermal_buoyancy)
        with self.assertRaises(ValueError):
            settings.pressure_iterations = 0
        self.assertEqual(settings.pressure_iterations, 64)

    def test_run_validation_precedes_graphics(self):
        for value in [-0.01, 1.01]:
            with self.assertRaisesRegex(ValueError, 'domain_box_opacity'):
                pomelo.run(pomelo.Settings(domain_box_opacity=value))
        with self.assertRaisesRegex(ValueError, r'sources\[0\].x'):
            pomelo.run(pomelo.Settings(sources=[(1, 0, 0)]))
        with self.assertRaises(TypeError):
            pomelo.run({})
        for field, values in [('density_dissipation', [-0.1, 10.1]), ('camera_pitch', [-1.51, 1.51]), ('camera_zoom', [0.49, 3.01])]:
            for value in values:
                with self.assertRaisesRegex(ValueError, field):
                    pomelo.run(pomelo.Settings(**{field: value}))

    def test_worker_thread_rejected(self):
        errors = []
        def worker():
            try:
                pomelo.run(pomelo.Settings())
            except Exception as error:
                errors.append(error)
        thread = threading.Thread(target=worker)
        thread.start()
        thread.join(timeout=5)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(errors), 1)
        self.assertIsInstance(errors[0], RuntimeError)
        self.assertIn('main thread', str(errors[0]))

    def test_headless_startup_failure_and_retry(self):
        code = '''
import pomelo
for settings in [pomelo.Settings(sources=[]), pomelo.Settings(domain_box_opacity=0, sources=[(0,0,0),(0,0,0)])]:
    try:
        pomelo.run(settings)
    except RuntimeError as error:
        assert 'already running' not in str(error), error
    else:
        raise AssertionError('dummy SDL driver unexpectedly supports Vulkan')
'''
        env = dict(os.environ, SDL_VIDEODRIVER='dummy')
        result = subprocess.run([sys.executable, '-c', code], env=env,
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
