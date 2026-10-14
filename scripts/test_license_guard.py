"""Synthetic repositories exercise the guard without downloading dependencies."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from license_guard import ALLOWED, check, cmake_commands


class LicenseGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.entries = [dict(name='Example', version='v1.0', license='MIT',
                             source='https://example.invalid/example.git', ships=True,
                             dependency='Example')]
        self.write('CMakeLists.txt', '''FetchContent_Declare(Example
            GIT_REPOSITORY "https://example.invalid/example.git" GIT_TAG v1.0)
            FetchContent_MakeAvailable(Example)''')
        self.save()

    def write(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def notices(self):
        self.write('THIRD_PARTY_NOTICES.md', '\n'.join('## ' + e['name'] for e in self.entries))

    def save(self):
        self.write('compliance/third_party.json', json.dumps({'schema_version': 1, 'components': self.entries}))
        self.notices()

    def approve_file(self, path, **extra):
        self.entries.append(dict(name=path, version='1', license='MIT',
                                 source='https://example.invalid/assets', ships=True,
                                 files=[path], **extra))
        self.save()

    def test_policy_files_are_not_vendored_license_markers(self):
        self.write('.github/workflows/license-guard.yml', 'name: license-guard')
        self.write('scripts/license_guard.py', '# synthetic guard')
        self.assertEqual(check(self.root), [])

    def test_new_juce_module_requires_review(self):
        self.write('app/CMakeLists.txt', 'target_link_libraries(app PRIVATE juce::juce_opengl)')
        self.assertTrue(any('unreviewed JUCE module juce_opengl' in e for e in check(self.root)))
        self.entries.append(dict(name='JUCE', version='8.0.4', license='JUCE-commercial',
                                 source='https://example.invalid/juce', ships=True,
                                 dependency='JUCE', modules=['juce_opengl']))
        self.save()
        self.assertEqual(check(self.root), [])

    def test_notices_must_exist_and_list_every_dependency(self):
        (self.root / 'THIRD_PARTY_NOTICES.md').unlink()
        self.assertIn('missing THIRD_PARTY_NOTICES.md', check(self.root))
        self.write('THIRD_PARTY_NOTICES.md', '# notices')
        self.assertTrue(any('Example: listed in third_party.json but missing' in e for e in check(self.root)))

    def test_top_level_license_file_rejected(self):
        for name in ('LICENSE', 'LICENSE.md', 'COPYING'):
            with self.subTest(name=name):
                self.write(name, 'x')
                self.assertIn(f'top-level license file is not allowed: {name}', check(self.root))
                (self.root / name).unlink()

    def test_seeded_repo_passes(self):
        self.assertEqual(check(self.root), [])

    def test_each_allowed_license(self):
        for license in ALLOWED:
            with self.subTest(license=license):
                self.entries[0]['license'] = license
                self.save()
                self.assertEqual(check(self.root), [])

    def test_copyleft_unknown_and_compound_licenses_fail(self):
        for license in ('GPL-3.0', 'AGPL-3.0', 'LGPL-2.1', 'unknown', 'MIT OR GPL-3.0', 'ISC', ''):
            with self.subTest(license=license):
                self.entries[0]['license'] = license
                self.save()
                self.assertTrue(any('license' in e for e in check(self.root)))

    def test_unlisted_fetch_and_external_project_fail(self):
        for command in ('FetchContent_Declare', 'FetchContent_Populate', 'ExternalProject_Add'):
            with self.subTest(command=command):
                self.write('nested/new.cmake', f'{command}(New URL https://example.invalid/a.tar.gz URL_HASH SHA256=abc)')
                self.assertIn('unlisted CMake dependency: New', check(self.root))

    def test_version_and_source_changes_fail(self):
        for before, after in (('v1.0', 'v2.0'), ('example.git', 'other.git')):
            with self.subTest(after=after):
                original = (self.root / 'CMakeLists.txt').read_text()
                self.write('CMakeLists.txt', original.replace(before, after))
                self.assertIn('Example: source/version differs from allowlist', check(self.root))
                self.write('CMakeLists.txt', original)

    def test_comment_quote_bracket_and_case_parsing(self):
        self.write('CMakeLists.txt', '''#[=[ FetchContent_Declare(Bad GIT_TAG v1) ]=]
          # ExternalProject_Add(Bad)
          fEtChCoNtEnT_DeClArE("Example" GIT_REPOSITORY
          [=[https://example.invalid/example.git]=] GIT_TAG "v1.0")
          FetchContent_MakeAvailable(Example)''')
        self.assertEqual(check(self.root), [])
        self.assertEqual(list(cmake_commands('set(x "#not a comment")')), [('set', ['x', '#not a comment'])])

    def test_missing_pin_or_variable_fails_closed(self):
        for declaration in ('Example GIT_REPOSITORY https://example.invalid/e.git',
                            '${name} GIT_REPOSITORY https://example.invalid/e.git GIT_TAG v1',
                            'Example URL ${url} URL_HASH SHA256=x',
                            'Example SOURCE_DIR vendor/e'):
            with self.subTest(declaration=declaration):
                self.write('CMakeLists.txt', f'FetchContent_Declare({declaration})')
                with self.assertRaises(ValueError):
                    check(self.root)

    def test_undeclared_make_available_and_populate_fail(self):
        for command in ('FetchContent_MakeAvailable', 'FetchContent_Populate'):
            self.write('new.cmake', f'{command}(New)')
            with self.assertRaisesRegex(ValueError, 'undeclared'):
                check(self.root)

    def test_every_media_type_is_checked_anywhere(self):
        for extension in ('ttf', 'otf', 'woff2', 'PNG', 'svg', 'jpg', 'icns', 'wav', 'mp3', 'flac', 'm4a', 'mp4'):
            with self.subTest(extension=extension):
                path = 'arbitrary/new.' + extension
                self.write(path, 'synthetic asset')
                self.assertIn('unlisted asset: ' + path, check(self.root))
                (self.root / path).unlink()

    def test_text_resource_requires_entry(self):
        self.write('resources/targets/new.txt', 'synthetic target')
        self.assertIn('unlisted asset: resources/targets/new.txt', check(self.root))

    def test_asset_hash_and_missing_file(self):
        path = 'resources/example.svg'
        self.write(path, '<svg/>')
        self.approve_file(path)
        self.assertEqual(check(self.root), [])
        self.write(path, '<svg>changed</svg>')
        self.assertEqual(check(self.root), [])  # content edits no longer need a checksum bump
        (self.root / path).unlink()
        self.assertTrue(any('missing inventory file' in e for e in check(self.root)))

    def test_vendored_markers_new_code_directories_and_licenses(self):
        for path in ('app/Source/vendor/blob.txt', 'third_party/lib/a.cpp',
                     'app/Source/newlib/a.h', 'arbitrary/LICENSE.txt', 'external/lib/data.txt', 'unfamiliar/library/table.dat'):
            with self.subTest(path=path):
                self.write(path, 'synthetic source')
                self.assertIn('unlisted vendored code/directory: ' + path, check(self.root))
                (self.root / path).unlink()

    def test_reviewed_vendor_directory(self):
        self.write('vendor/example/a.cpp', '// synthetic')
        self.entries.append(dict(name='Vendored Example', version='1', license='BSD-2-Clause',
                                 source='https://example.invalid/vendor', ships=True,
                                 vendored=['vendor/example']))
        self.save()
        self.assertEqual(check(self.root), [])

    def test_submodule_needs_entry(self):
        self.write('.gitmodules', '[submodule "example"]\n path = elsewhere/example\n url = https://example.invalid/e')
        self.assertIn('unlisted submodule: elsewhere/example', check(self.root))

    def test_own_work_keeps_proprietary_license(self):
        path = 'resources/own.txt'
        self.write(path, 'synthetic original work')
        self.approve_file(path, ownership='Will Adams')
        self.entries[-1].update(license='LicenseRef-FOH-proprietary', source='THIRD_PARTY_NOTICES.md')
        self.save()
        self.assertEqual(check(self.root), [])
        self.entries[0].update(ownership='Will Adams', license='LicenseRef-FOH-proprietary', source='THIRD_PARTY_NOTICES.md')
        self.save()
        self.assertTrue(any('invalid original-work provenance' in e for e in check(self.root)))

    def test_bad_schema_and_duplicates(self):
        self.entries[0]['ships'] = 'false'
        self.entries.append(self.entries[0].copy())
        self.save()
        errors = check(self.root)
        self.assertTrue(any('requires version' in e for e in errors))
        self.assertTrue(any('duplicate' in e for e in errors))

    def test_tracked_and_new_assets_checked_but_ignored_builds_skipped(self):
        self.write('.gitignore', 'build*/\n')
        self.write('build-new/vendor/dependency/font.ttf', 'ignored synthetic')
        self.assertEqual(check(self.root), [])
        self.write('tracked.wav', 'synthetic')
        subprocess.run(['git', 'add', 'tracked.wav'], cwd=self.root, check=True)
        self.assertIn('unlisted asset: tracked.wav', check(self.root))
        self.write('new.wav', 'synthetic')
        self.assertIn('unlisted asset: new.wav', check(self.root))

    def test_python_requirements_are_pinned_and_reviewed(self):
        self.write('tools/requirements-test.txt', 'example==2.0\n')
        self.assertTrue(any('Python requirement' in e for e in check(self.root)))
        self.entries.append(dict(name='Python Example', version='2.0', license='MIT',
                                 source='https://example.invalid/python', ships=False, python_package='example'))
        self.save()
        self.assertEqual(check(self.root), [])


if __name__ == '__main__':
    unittest.main()
