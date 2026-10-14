#!/usr/bin/env python3
"""Fail closed on unreviewed dependencies, vendored code, and media assets.

Uses the Git inventory, including nonignored new files, never downloaded build trees.
This is an inventory/policy gate, not a license inference or legal entitlement check.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ALLOWED = frozenset({'MIT', 'BSD-2-Clause', 'BSD-3-Clause', 'BSL-1.0',
                     'Apache-2.0', 'Zlib', 'OFL-1.1', 'Unlicense', 'JUCE-commercial'})
MEDIA = frozenset('ttf otf woff woff2 ttc eot dfont fnt png jpg jpeg gif svg webp bmp ico icns tif tiff avif heic heif jxl psd xcf dds tga exr hdr pdf wav aif aiff aifc mp3 mp2 flac ogg opus m4a aac caf wma snd au bwf weba mid midi mp4 mov'.split())
CODE = frozenset('c cc cpp cxx h hh hpp hxx m mm inc py js ts swift go rs java cs lua sh'.split())
FIRST_PARTY = frozenset({'app/Source', 'app/Source/console', 'core/src',
                         'core/include/fohcore', 'tests', 'tests/roomsim', 'scripts', 'tools'})
FIRST_PARTY_DIRS = FIRST_PARTY | frozenset({'.', '.github/workflows', 'app',
    'app/Resources/Fonts', 'core', 'core/include', 'resources', 'resources/targets',
    'tests/data', 'docs', 'compliance'})
VENDOR_NAMES = frozenset({'vendor', 'vendored', 'third_party', 'third-party', 'thirdparty',
                          'external', 'extern', 'deps', 'dependencies', 'libs', 'submodules'})


def inventory(root):
    result = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
                            cwd=root, check=True, capture_output=True)
    return sorted(set(result.stdout.decode().rstrip('\0').split('\0')) - {''})


def cmake_commands(text):
    """Tokenize CMake quotes, bracket arguments/comments, line comments and lists.

    Do not execute CMake (untrusted PR input). Unresolved variables in dependency
    identities/locations/pins fail below, requiring a literal reviewable declaration.
    """
    tokens = []
    i = 0
    while i < len(text):
        if text[i].isspace():
            i += 1
            continue
        bracket = re.match(r'(\#?)\[(=*)\[', text[i:])
        if bracket:
            end = text.find(']' + bracket[2] + ']', i + len(bracket[0]))
            if end < 0:
                raise ValueError('unterminated CMake bracket argument')
            if not bracket[1]:
                tokens.append(text[i + len(bracket[0]):end])
            i = end + len(bracket[2]) + 2
        elif text[i] == '#':
            end = text.find('\n', i)
            i = len(text) if end < 0 else end + 1
        elif text[i] == '"':
            end = i + 1
            value = ''
            while end < len(text) and text[end] != '"':
                if text[end] == '\\' and end + 1 < len(text):
                    end += 1
                value += text[end]
                end += 1
            if end == len(text):
                raise ValueError('unterminated CMake string')
            tokens.append(value)
            i = end + 1
        elif text[i] in '()':
            tokens.append(text[i])
            i += 1
        else:
            end = i
            while end < len(text) and not text[end].isspace() and text[end] not in '()#"':
                end += 1
            tokens.extend(text[i:end].split(';'))
            i = end
    i = 0
    while i + 1 < len(tokens):
        if tokens[i + 1] != '(':
            i += 1
            continue
        name = tokens[i].lower()
        args = []
        depth = 1
        i += 2
        while i < len(tokens) and depth:
            token = tokens[i]
            depth += (token == '(') - (token == ')')
            if depth:
                args.append(token)
            i += 1
        if depth:
            raise ValueError('unbalanced CMake command')
        yield name, args


def dependencies(root, files):
    found = []
    available = []
    for path in files:
        if PurePosixPath(path).name != 'CMakeLists.txt' and not path.endswith('.cmake'):
            continue
        for command, args in cmake_commands((root / path).read_text()):
            if command == 'fetchcontent_makeavailable':
                available.extend(args)
            if command not in {'fetchcontent_declare', 'fetchcontent_populate', 'externalproject_add'}:
                continue
            if command == 'fetchcontent_populate' and len(args) == 1:
                available.extend(args)
                continue
            if not args:
                raise ValueError(f'{path}: empty dependency declaration')
            options = {a.upper(): args[i + 1] for i, a in enumerate(args[:-1])}
            source = options.get('GIT_REPOSITORY', options.get('URL', options.get('SOURCE_DIR')))
            version = options.get('GIT_TAG', options.get('URL_HASH'))
            if not source or not version or any('$' in x for x in (args[0], source, version)):
                raise ValueError(f'{path}: {args[0]} must have a literal source and pin')
            found.append((args[0], source, version))
    declared = {name.lower() for name, _, _ in found}
    for name in available:
        if name.lower() not in declared:
            raise ValueError(f'undeclared FetchContent dependency: {name}')
    return found


def check(root):
    files = inventory(root)
    entries = json.loads((root / 'compliance/third_party.json').read_text())['components']
    if not isinstance(entries, list) or any(not isinstance(e, dict) for e in entries):
        raise ValueError('components must be a list of objects')
    errors = []
    covered = set()
    vendor_roots = set()
    declared = {}
    names = set()
    for entry in entries:
        name = entry.get('name', '')
        if not name or name.lower() in names:
            errors.append(f'duplicate or empty component name: {name}')
        names.add(name.lower())
        if not all(isinstance(entry.get(key), str) and entry[key] for key in ('version', 'license', 'source')) or type(entry.get('ships')) is not bool:
            errors.append(f'{name}: requires version, license, source and boolean ships')
        own = entry.get('ownership') == 'Will Adams'
        # Own work retains the proprietary LICENSE; it must never be mislabeled
        # as an open-source license simply to fit the third-party policy.
        if own:
            if entry.get('license') != 'LicenseRef-FOH-proprietary' or entry.get('source') != 'LICENSE' or entry.get('dependency') or entry.get('vendored'):
                errors.append(f'{name}: invalid original-work provenance')
        elif entry.get('license') not in ALLOWED:
            errors.append(f'{name}: disallowed license {entry.get("license")}')
        if entry.get('dependency'):
            dep = entry['dependency'].lower()
            if dep in declared:
                errors.append(f'duplicate dependency: {dep}')
            declared[dep] = entry
        for path, digest in entry.get('files', {}).items():
            if path not in files:
                errors.append(f'{name}: missing inventory file {path}')
                continue
            if path in covered:
                errors.append(f'duplicate asset coverage: {path}')
            covered.add(path)
            if hashlib.sha256((root / path).read_bytes()).hexdigest() != digest:
                errors.append(f'{name}: changed file requires review: {path}')
        for path in entry.get('vendored', []):
            if path.startswith('/') or '..' in PurePosixPath(path).parts or not any(f == path.rstrip('/') or f.startswith(path.rstrip('/') + '/') for f in files):
                errors.append(f'{name}: invalid/missing vendored directory {path}')
            vendor_roots.add(path.rstrip('/'))
    def is_vendor(path):
        return any(path == v or path.startswith(v + '/') for v in vendor_roots)
    for path in files:
        p = PurePosixPath(path)
        suffix = p.suffix.lower().lstrip('.')
        if (suffix in MEDIA or path.startswith('resources/') or path.startswith('app/Resources/')) and path not in covered and not is_vendor(path):
            errors.append(f'unlisted asset: {path}')
        vendor_marker = any(part.lower() in VENDOR_NAMES for part in p.parts[:-1])
        license_marker = p.suffix.lower() in {'', '.txt', '.md', '.rst'} and bool(re.match(r'^(licen[cs]e|copying|notice)([.-]|$)', p.name, re.I)) and path != 'LICENSE' and path not in covered
        new_code_dir = suffix in CODE and str(p.parent) not in FIRST_PARTY
        new_directory = str(p.parent) not in FIRST_PARTY_DIRS and path not in covered
        if (vendor_marker or license_marker or new_code_dir or new_directory) and not is_vendor(path):
            errors.append(f'unlisted vendored code/directory: {path}')
        if p.name == '.gitmodules':
            for submodule in re.findall(r'^\s*path\s*=\s*(.+)$', (root / path).read_text(), re.M):
                if submodule.strip() not in vendor_roots:
                    errors.append(f'unlisted submodule: {submodule.strip()}')
    for name, source, version in dependencies(root, files):
        entry = declared.get(name.lower())
        if entry is None:
            errors.append(f'unlisted CMake dependency: {name}')
        elif (entry['source'], entry['version']) != (source, version):
            errors.append(f'{name}: source/version differs from allowlist')
    juce = declared.get('juce', {})
    reviewed_modules = set(juce.get('modules', []))
    for path in files:
        if PurePosixPath(path).name == 'CMakeLists.txt' or path.endswith('.cmake'):
            for _, args in cmake_commands((root / path).read_text()):
                for arg in args:
                    for module in re.findall(r'juce::(juce_[a-z_]+)', arg):
                        if module.startswith('juce_recommended_'):
                            continue
                        if module not in reviewed_modules:
                            errors.append(f'{path}: unreviewed JUCE module {module}')
    # The golden-signal tool's Python dependency is test-only, but still reviewed.
    for path in files:
        if re.match(r'(^|.*/)requirements[^/]*\.txt$', path):
            for line in (root / path).read_text().splitlines():
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                match = re.fullmatch(r'([\w.-]+)==([\w.+-]+)', line)
                entry = next((e for e in entries if match and e.get('python_package', '').lower() == match[1].lower()), None)
                if not entry or entry['version'] != match[2]:
                    errors.append(f'{path}: unlisted/unpinned Python requirement {line}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        errors = check(args.root.resolve())
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as exc:
        errors = [f'invalid compliance inventory: {exc}']
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('License guard passed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
