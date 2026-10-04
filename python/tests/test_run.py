"""Bounded, opt-in desktop checks: POMELO_TEST_GUI=1 python -m unittest discover -s python/tests."""
import os
import subprocess
import sys
import unittest


@unittest.skipUnless(os.environ.get('POMELO_TEST_GUI') == '1', 'requires a Vulkan desktop')
class RunTests(unittest.TestCase):
    def test_interrupt_snapshot_gil_and_sequential_runs(self):
        code = '''
import os
import signal
import threading
import time
import pomelo

for iteration in range(2):
    settings = pomelo.Settings(sources=[])
    progressed = []
    done = threading.Event()
    def worker():
        for tick in range(30):
            if done.wait(0.1):
                return
            progressed.append(tick)
            # Invalid edits must not affect the owned run snapshot.
            settings.domain_box_opacity = -1
            settings.sources = [(2, 2, 2)]
        os.kill(os.getpid(), signal.SIGINT)
    thread = threading.Thread(target=worker)
    thread.start()
    try:
        pomelo.run(settings)
    except KeyboardInterrupt:
        pass
    else:
        raise AssertionError('run returned before interruption')
    finally:
        done.set()
        thread.join()
    assert len(progressed) == 30, progressed
'''
        result = subprocess.run([sys.executable, '-c', code], capture_output=True,
                                text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr)
