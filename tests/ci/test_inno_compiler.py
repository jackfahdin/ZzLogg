"""Exercise local compiler selection without downloading or installing tools."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
POWERSHELL = shutil.which('pwsh') or shutil.which('powershell')


@unittest.skipUnless(POWERSHELL, 'PowerShell is required')
class InnoCompilerTests(unittest.TestCase):
    def invoke(self, version='7.1.0', missing=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scripts = root / 'packaging/windows'
            scripts.mkdir(parents=True)
            shutil.copy2(ROOT / 'packaging/windows/Build-InnoInstaller.ps1', scripts)
            compiler = root / 'Inno Setup 7/compiler.ps1'
            compiler.parent.mkdir()
            if not missing:
                compiler.write_text('''
$global:LASTEXITCODE = 0
if ($args[0] -eq '--version') { Write-Output '%s'; return }
$output = ($args | Where-Object { $_ -like '/O*' }).Substring(2)
Set-Content -LiteralPath (Join-Path $output 'ZzLogg-26.10.05-x64-Qt6-setup.exe') -Value 'fixture'
''' % version, encoding='utf-8')
            # Network access is forbidden in all three cases, including errors.
            harness = root / 'test.ps1'
            harness.write_text('''
$ErrorActionPreference = 'Stop'
function Invoke-WebRequest { throw 'UNEXPECTED_NETWORK_REQUEST' }
& "$PSScriptRoot/packaging/windows/Build-InnoInstaller.ps1" -Version 26.10.05
''', encoding='utf-8')
            result = subprocess.run(
                [POWERSHELL, '-NoProfile', '-File', str(harness)],
                env=dict(os.environ, ZZLOGG_ISCC=str(compiler), RUNNER_TEMP=str(root)),
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            exists = (root / 'ZzLogg-26.10.05-x64-Qt6-setup.exe').is_file()
            return result, exists

    def test_installed_compiler_with_spaces_works_offline(self):
        result, exists = self.invoke()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertTrue(exists)

    def test_wrong_version_is_rejected_without_download(self):
        result, exists = self.invoke(version='7.0.0')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unexpected Inno Setup compiler version', result.stdout)
        self.assertNotIn('UNEXPECTED_NETWORK_REQUEST', result.stdout)
        self.assertFalse(exists)

    def test_missing_explicit_compiler_is_rejected_without_download(self):
        result, exists = self.invoke(missing=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Inno Setup compiler not found', result.stdout)
        self.assertNotIn('UNEXPECTED_NETWORK_REQUEST', result.stdout)
        self.assertFalse(exists)
