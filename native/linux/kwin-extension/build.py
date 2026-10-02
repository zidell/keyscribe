#!/usr/bin/env python3
"""Build against headers matching the installed KWin 6 private ABI."""
import os
import sys
import json
from pathlib import Path
import shlex
import subprocess
root = Path(__file__).resolve().parent
headers = Path(os.environ.get('KEYSCRIBE_KWIN_INCLUDE_DIR', '/usr/include'))
output = Path(os.environ.get('KEYSCRIBE_BUILD_DIR', root.parents[2] / 'dist-native/linux'))
output.mkdir(parents=True, exist_ok=True)
include = ['-I' + str(root), '-I' + str(output), '-I' + str(headers / 'kwin'),
           '-I' + str(headers / 'KF6/KConfig')]
for name in ['KCoreAddons', 'KConfigCore', 'KConfigGui', 'KWindowSystem']:
    include.append('-I' + str(headers / 'KF6' / name))
source = root / 'escape.cpp'
name = 'escape'
binary = 'keyscribe-escape'
if '--test-driver' in sys.argv:
    source = root.parent / 'tests/kwin_escape_driver.cpp'
    name = 'kwin_escape_driver'
    binary = 'keyscribe-escape-driver'
include.append('-I' + str(source.parent))
qt = shlex.split(subprocess.check_output(
    ['pkg-config', '--cflags', '--libs', 'Qt6Core', 'Qt6Gui', 'Qt6Widgets', 'Qt6DBus'], text=True))
subprocess.run(['/usr/lib/qt6/libexec/moc', *include,
                *[v for v in qt if v.startswith(('-I', '-D'))],
                str(source), '-o', str(output / (name + '.moc'))], check=True)
subprocess.run(['g++', '-std=c++20', '-fPIC', '-shared', '-O2', '-Wall', '-Wextra',
                '-Wl,-z,defs', *include, str(source), '-o',
                str(output / (binary + '.so')), *qt,
                '-l:libkwin.so.6', '-l:libKF6CoreAddons.so.6'], check=True)

if '--test-driver' not in sys.argv:
    if subprocess.run(['pkg-config', '--exists', 'Qt6Qml']).returncode == 0:
        qml = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'Qt6Qml', 'Qt6DBus'], text=True))
    else:
        # The exact-version headers may be extracted without installing dev packages.
        qml_headers = headers / 'x86_64-linux-gnu/qt6'
        if not (qml_headers / 'QtQml/QQmlExtensionPlugin').exists():
            raise SystemExit('Install qt6-declarative-dev or provide its extracted headers.')
        qml = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'Qt6Core', 'Qt6DBus'], text=True))
        qml += ['-I' + str(qml_headers), '-I' + str(qml_headers / 'QtQml'), '-l:libQt6Qml.so.6']
    source = root / 'bootstrap.cpp'
    plugin_dir = Path(os.environ.get('KEYSCRIBE_INSTALL_PREFIX', Path.home() / '.local')) / 'lib/qt6/plugins'
    define = '-DKEYSCRIBE_KWIN_PLUGIN_DIR=' + json.dumps(str(plugin_dir))
    subprocess.run(['/usr/lib/qt6/libexec/moc',
        *[v for v in qml if v.startswith(('-I', '-D'))], str(source),
        '-o', str(output / 'bootstrap.moc')], check=True)
    subprocess.run(['g++', '-std=c++20', '-fPIC', '-shared', '-O2', '-Wall', '-Wextra',
        '-Wl,-z,defs', '-I' + str(output), define, str(source),
        '-o', str(output / 'libkeyscribebootstrap.so'), *qml], check=True)
