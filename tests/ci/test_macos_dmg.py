"""Run the real DMG packaging step with simulated macOS command failures."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name == "posix" and shutil.which("sh"), "requires POSIX sh")
class MacDmgTests(unittest.TestCase):
    def run_packaging(self, scenario, expected_status, expected_attempts):
        action = (ROOT / ".github/actions/agent-package-mac/action.yml").read_text()
        step = action.split("    - name: Mac pack dmg\n", 1)[1]
        script = textwrap.dedent(step.split("      run: |\n", 1)[1].split("\n    #", 1)[0])
        with tempfile.TemporaryDirectory(prefix="zzlogg dmg ") as directory:
            root = Path(directory)
            command = "#!" + sys.executable + "\n" + textwrap.dedent('''\
                import os
                from pathlib import Path
                import plistlib
                import sys

                scenario = os.environ["DMG_TEST_SCENARIO"]
                if Path(sys.argv[0]).name == "cpack":
                    assert sys.argv[1:] == ["--verbose", "-G", "DragNDrop"]
                    count = Path("attempts")
                    attempt = int(count.read_text()) + 1 if count.exists() else 1
                    count.write_text(str(attempt))
                    if scenario == "first_success" or (attempt == 2 and scenario != "retry_fails"):
                        Path("package.dmg").write_text("simulated package")
                        sys.exit(0)
                    if scenario != "unrelated_error":
                        print('CPack Error: Error executing: /usr/bin/hdiutil detach "/Volumes/ZzLogg"')
                        print("CPack Error: Error detaching temporary disk image.")
                    else:
                        print("CPack Error: No space left on device")
                    sys.exit(7)
                if sys.argv[1:] == ["detach", "-force", "/Volumes/ZzLogg"]:
                    sys.exit(0 if scenario == "force_succeeds" else 1)
                assert sys.argv[1:] == ["info", "-plist"]
                if scenario == "info_fails":
                    sys.exit(8)
                if scenario == "malformed_info":
                    print("not a plist")
                    sys.exit(0)
                entities = [{"dev-entry": "/dev/disk4s1", "mount-point": "/Volumes/Other"}]
                if scenario == "still_mounted":
                    entities.append({"dev-entry": "/dev/disk5s1", "mount-point": "/Volumes/ZzLogg"})
                plistlib.dump({"images": [{"system-entities": entities}]}, sys.stdout.buffer)
            ''')
            for name in ("cpack", "hdiutil"):
                path = root / name
                path.write_text(command)
                path.chmod(0o755)
            result = subprocess.run(
                ["sh", "-e", "-c", script],
                env={**os.environ, "PATH": directory + os.pathsep + os.environ["PATH"],
                     "KLOGG_BUILD_ROOT": directory, "DMG_TEST_SCENARIO": scenario},
                capture_output=True, text=True, timeout=20,
            )
            self.assertEqual(result.returncode == 0, expected_status == 0, result.stdout + result.stderr)
            self.assertEqual(int((root / "attempts").read_text()), expected_attempts)
            self.assertEqual((root / "package.dmg").exists(), expected_status == 0)
            return result.returncode

    def test_already_detached_retries(self):
        self.run_packaging("already_detached", 0, 2)

    def test_force_detach_succeeds(self):
        self.run_packaging("force_succeeds", 0, 2)

    def test_first_attempt_succeeds(self):
        self.run_packaging("first_success", 0, 1)

    def test_still_mounted_fails(self):
        self.run_packaging("still_mounted", 1, 1)

    def test_info_failure_is_not_treated_as_detached(self):
        self.run_packaging("info_fails", 1, 1)

    def test_malformed_info_is_not_treated_as_detached(self):
        self.run_packaging("malformed_info", 1, 1)

    def test_unrelated_cpack_error_is_not_retried(self):
        self.run_packaging("unrelated_error", 1, 1)

    def test_second_cpack_failure_is_propagated_without_third_attempt(self):
        self.assertEqual(self.run_packaging("retry_fails", 7, 2), 7)


if __name__ == "__main__":
    unittest.main()
