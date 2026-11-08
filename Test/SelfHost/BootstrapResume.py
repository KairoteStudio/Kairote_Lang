"""Explicit saved-bootstrap acceptance gate; ordinary test discovery omits it.

Run ``python -m unittest Test.SelfHost.BootstrapResume -v`` with network access
for the real HTTP cases. SELFHOST_BOOTSTRAP_WORK selects a nondefault report.
"""
import os
from pathlib import Path
import subprocess
import unittest

from Test.SelfHost.Bootstrap import Bootstrap, ROOT


class BootstrapResumeGate(unittest.TestCase):
    def test_saved_generations_complete_native_acceptance(self):
        work = Path(os.environ.get("SELFHOST_BOOTSTRAP_WORK", ROOT / "build/selfhost"))
        build = Bootstrap.load_resume(work)
        try:
            build.resume_native_driver()
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            build.report["complete"] = False
            build.report["failure"] = str(error)
            build.save()
            raise
        self.assertTrue(build.report["complete"])
