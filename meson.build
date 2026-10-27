project(
  'kitsas',
  'cpp',
  version: '6.0.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.3.0',
  default_options: ['cpp_std=c++17'],
)

# Qt's Meson dependency has no CMake finder (through Meson 1.12). qmake is
# what the official SDK, install-qt-action, and distro qt6-base-dev all ship.
# Qt's bin directory must be on PATH so qmake6 / qmake can be found.
qt_method = 'config-tool'
qt6 = import('qt6')
fs = import('fs')
py = find_program('python3', 'python', version: '>=3')

qt_modules = [
  'Core',
  'Gui',
  'Widgets',
  'Sql',
  'PrintSupport',
  'Network',
  'Svg',
  'SvgWidgets',
  'Xml',
  'Pdf',
  'PdfWidgets',
  'WebEngineCore',
  'WebEngineWidgets',
  'Quick',
  'QuickWidgets',
  'Qml',
  'QmlModels',
  'OpenGL',
  'WebChannel',
  'Positioning',
  'Core5Compat',
]

qt_dep = dependency('qt6', modules: qt_modules, method: qt_method)

# Newer Qt (6.10+) publishes QmlMeta as a public dependency of Quick.
# Qt 6.7 does not ship that library; skip it when it is absent.
qt_extra_deps = []
foreach mod : ['QmlMeta']
  extra = dependency('qt6', modules: [mod], method: qt_method, required: false)
  if extra.found()
    qt_extra_deps += extra
  endif
endforeach

# rcc lives in libexec on the Qt 6.7 Linux/macOS SDK and in bin on Windows.
# install-qt-action only puts bin on PATH, so tool lookups need both.
qmake = find_program('qmake6', 'qmake-qt6', 'qmake')
qt_host_bins = run_command(qmake, '-query', 'QT_HOST_BINS', check: true).stdout().strip()
qt_host_libexecs = run_command(qmake, '-query', 'QT_HOST_LIBEXECS', check: true).stdout().strip()
qt_tool_dirs = []
foreach d : [qt_host_libexecs, qt_host_bins]
  if d != ''
    qt_tool_dirs += d
  endif
endforeach

qt_translations_dir = get_option('qt_translations_dir')
if qt_translations_dir == ''
  qt_translations_dir = run_command(
    qmake, '-query', 'QT_INSTALL_TRANSLATIONS',
    check: true,
  ).stdout().strip()
endif

# Qt 6.7 ships Swedish base strings in qt_sv.qm. qtbase_sv.qm appears in later SDKs.
# Kielet loads :/tr/qtbase_sv.qm, so the qrc aliases whichever file exists.
qtbase_fi_qm = qt_translations_dir / 'qtbase_fi.qm'
qtbase_sv_qm = qt_translations_dir / 'qtbase_sv.qm'
if not fs.exists(qtbase_fi_qm)
  error('Missing qtbase_fi.qm in ' + qt_translations_dir + '. Pass -Dqt_translations_dir=...')
endif
if not fs.exists(qtbase_sv_qm)
  qtbase_sv_qm = qt_translations_dir / 'qt_sv.qm'
  if not fs.exists(qtbase_sv_qm)
    error('Missing qtbase_sv.qm and qt_sv.qm in ' + qt_translations_dir + '. Pass -Dqt_translations_dir=...')
  endif
endif

subdir('kitsas')

# Defined here, not under kitsas/, so the binary lands in the build root
# (build/kiswaspg) rather than build/kitsas/.
kiswaspg = executable(
  'kiswaspg',
  files('kitsas/main.cpp'),
  app_sources,
  include_directories: kitsas_inc,
  dependencies: [kitsas_dep] + app_deps,
  win_subsystem: 'windows',
  install: true,
)

# macdeployqt needs an app bundle. Meson does not create one for a normal executable.
if host_machine.system() == 'darwin'
  custom_target(
    'kiswaspg-bundle',
    input: kiswaspg,
    output: 'kiswaspg.app.stamp',
    command: [py, files('meson/make_app_bundle.py'), '@INPUT@', '@OUTPUT@', meson.project_version()],
    build_by_default: true,
  )
endif

subdir('unittest')
