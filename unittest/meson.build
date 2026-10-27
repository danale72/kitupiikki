test_env = environment()
test_env.set('QT_QPA_PLATFORM', 'offscreen')

qt_core_test = dependency('qt6', modules: ['Core', 'Test'], method: qt_method)
qt_gui_test = dependency('qt6', modules: ['Core', 'Gui', 'Test'], method: qt_method)
qt_widgets_test = dependency('qt6', modules: ['Core', 'Gui', 'Widgets', 'Test'], method: qt_method)
qt_test_dep = dependency('qt6', modules: ['Test'], method: qt_method)

testidata = qt6.compile_resources(
  sources: files('data/testidata.qrc'),
  method: qt_method,
)

# Headers that include generated ui_*.h are moc'd with the app. Full-app tests
# that moc their own .cpp still need those headers on the include path.
app_moc_deps = [qt_dep, declare_dependency(sources: ui_stamp)] + qt_extra_deps
app_moc_args = ['-I' + kitsas_build_dir]

euro_moc = qt6.compile_moc(
  sources: files('eurotest/tst_eurotest.cpp'),
  include_directories: kitsas_inc,
  dependencies: qt_core_test,
  method: qt_method,
)
eurotest = executable(
  'eurotest',
  files(
    'eurotest/tst_eurotest.cpp',
    '../kitsas/model/euro.cpp',
  ),
  euro_moc,
  include_directories: kitsas_inc,
  dependencies: qt_core_test,
)
test('eurotest', eurotest, suite: 'unit', env: test_env)

viite_moc = qt6.compile_moc(
  sources: files('viitetesti/tst_viitetest.cpp'),
  include_directories: kitsas_inc,
  dependencies: qt_gui_test,
  method: qt_method,
)
viitetesti = executable(
  'viitetesti',
  files(
    'viitetesti/tst_viitetest.cpp',
    '../kitsas/laskutus/viitenumero.cpp',
    '../kitsas/laskutus/iban.cpp',
  ),
  viite_moc,
  include_directories: kitsas_inc,
  dependencies: qt_gui_test,
)
test('viitetesti', viitetesti, suite: 'unit', env: test_env)

validator_moc = qt6.compile_moc(
  headers: files(
    '../kitsas/validator/ibanvalidator.h',
    '../kitsas/validator/maksuviestivalidator.h',
  ),
  sources: files('ValidatorTest/tst_validatortest.cpp'),
  include_directories: kitsas_inc,
  dependencies: qt_gui_test,
  method: qt_method,
)
validator_test = executable(
  'ValidatorTest',
  files(
    'ValidatorTest/tst_validatortest.cpp',
    '../kitsas/validator/ibanvalidator.cpp',
    '../kitsas/validator/maksuviestivalidator.cpp',
    '../kitsas/laskutus/iban.cpp',
  ),
  validator_moc,
  include_directories: kitsas_inc,
  dependencies: qt_gui_test,
)
test('ValidatorTest', validator_test, suite: 'unit', env: test_env)

kieli_moc = qt6.compile_moc(
  headers: files('../kitsas/kieli/kielet.h'),
  sources: files('KieliTesti/tst_monikielinen.cpp'),
  include_directories: kitsas_inc,
  dependencies: qt_widgets_test,
  method: qt_method,
)
kieli_testi = executable(
  'KieliTesti',
  files(
    'KieliTesti/tst_monikielinen.cpp',
    '../kitsas/kieli/monikielinen.cpp',
    '../kitsas/kieli/kieli.cpp',
    '../kitsas/kieli/kielet.cpp',
  ),
  kieli_moc,
  testidata,
  include_directories: kitsas_inc,
  dependencies: qt_widgets_test,
)
test('KieliTesti', kieli_testi, suite: 'unit', env: test_env)

tositerivi_moc = qt6.compile_moc(
  headers: files('../kitsas/model/tositerivi.h'),
  sources: files('tositerivitesti/tst_tositerivitesti.cpp'),
  include_directories: kitsas_inc,
  dependencies: qt_core_test,
  method: qt_method,
)
tositerivitesti = executable(
  'tositerivitesti',
  files(
    'tositerivitesti/tst_tositerivitesti.cpp',
    '../kitsas/model/tositerivi.cpp',
    '../kitsas/model/euro.cpp',
    '../kitsas/db/kantavariantti.cpp',
  ),
  tositerivi_moc,
  include_directories: kitsas_inc,
  dependencies: qt_core_test,
)
test('tositerivitesti', tositerivitesti, suite: 'unit', env: test_env)

liite_moc = qt6.compile_moc(
  headers: files(
    '../kitsas/liite/liitecache.h',
    '../kitsas/liite/cacheliite.h',
    '../kitsas/db/kpkysely.h',
  ),
  sources: files('liitecachetest/tst_liitecachetest.cpp'),
  include_directories: kitsas_inc,
  dependencies: qt_widgets_test,
  method: qt_method,
)
liitecachetest = executable(
  'liitecachetest',
  files(
    'liitecachetest/tst_liitecachetest.cpp',
    '../kitsas/liite/liitecache.cpp',
    '../kitsas/liite/cacheliite.cpp',
  ),
  liite_moc,
  include_directories: kitsas_inc,
  dependencies: qt_widgets_test,
)
test('liitecachetest', liitecachetest, suite: 'unit', env: test_env)

kitsas_src = (meson.project_source_root() / 'kitsas').replace('\\', '/')

dbparity_moc = qt6.compile_moc(
  sources: files('dbparity/tst_dbparity.cpp'),
  include_directories: kitsas_inc,
  dependencies: app_moc_deps,
  extra_args: app_moc_args,
  method: qt_method,
)
dbparity = executable(
  'dbparity',
  files(
    'dbparity/tst_dbparity.cpp',
    'dbparity/testdb.cpp',
  ),
  dbparity_moc,
  include_directories: kitsas_inc,
  dependencies: [kitsas_dep, qt_test_dep],
  cpp_args: '-DKITSAS_SRC_DIR="' + kitsas_src + '"',
)
test('dbparity', dbparity, suite: 'dbparity', env: test_env, timeout: 300)

vakaustesti_moc = qt6.compile_moc(
  sources: files('vakaustesti/tst_vakaustesti.cpp'),
  include_directories: kitsas_inc,
  dependencies: app_moc_deps,
  extra_args: app_moc_args,
  method: qt_method,
)
vakaustesti = executable(
  'vakaustesti',
  files('vakaustesti/tst_vakaustesti.cpp'),
  vakaustesti_moc,
  testidata,
  include_directories: kitsas_inc,
  dependencies: [kitsas_dep, qt_test_dep],
)
test('vakaustesti', vakaustesti, suite: 'vakaus', env: test_env, timeout: 180)

alv_moc = qt6.compile_moc(
  sources: files('AlvLaskelmaTesti/tst_alvlaskelma.cpp'),
  include_directories: kitsas_inc,
  dependencies: app_moc_deps,
  extra_args: app_moc_args,
  method: qt_method,
)
alv_laskelma_testi = executable(
  'AlvLaskelmaTesti',
  files('AlvLaskelmaTesti/tst_alvlaskelma.cpp'),
  alv_moc,
  testidata,
  include_directories: kitsas_inc,
  dependencies: [kitsas_dep, qt_test_dep],
)
test('AlvLaskelmaTesti', alv_laskelma_testi, suite: 'alv', env: test_env, timeout: 180)

test(
  'sbom',
  py,
  args: [files('../tools/sbom/test_meson_to_cyclonedx.py')],
  suite: 'unit',
)
