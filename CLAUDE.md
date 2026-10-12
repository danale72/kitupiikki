# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Kitsas (formerly Kitupiikki) — a Finnish bookkeeping/accounting desktop app written in C++17/Qt 6. All code, comments, UI strings, and identifiers are in Finnish. Books are stored either locally in SQLite (`.kitsas` files) or on a PostgreSQL server (Kitsas Oy's paid multi-user service). GPLv3.

## Build

Qt 6.4+ required (6.7/6.8 for full feature set: QtWidgets, QtPdf, QtWebEngine, qt5compat). Uses qmake, not CMake.

```
qmake kitsasproject.pro && make qmake_all   # whole workspace: app + unit tests
make
```

To build just the app:
```
qmake kitsas/kitsas.pro -spec <spec> "CONFIG+=release"
make
```

Linux also needs `libzip-dev`; `libpoppler-qt5-dev` is used for PDF handling on Linux.

Packaging scripts at the repo root (`paketoi-*.sh`, `kaanna-*.sh`) build/translate release artifacts for Linux/Windows/mac cross-builds — read one before using it, they assume specific toolchains (mxe, qt6, etc.) not necessarily present locally.

### Local macOS dev builds (Kitsas PG)

The maintainer's local builds live in git-ignored dirs at the repo root, configured with Qt 6.7.3 at `~/Qt/6.7.3/macos`:

- `build-pg/` — the app (debug, Postgres flavour). qmake line (see the `# Command:` header of its `Makefile`): `qmake -o Makefile ../kitsas/kitsas.pro 'DEFINES+=KITSAS_PG_BUILD USE_ZIPLIB' CONFIG+=debug CONFIG-=release QMAKE_CXXFLAGS+=-Wno-error=implicit-function-declaration 'LIBS+=-lzip -L/opt/homebrew/lib' 'QMAKE_LIBS_OPENGL=-framework OpenGL'`, then `make -j8`. Only rebuild when asked (see Notes).
- `build-dbparity/` — the `dbparity` test: `qmake -o Makefile ../unittest/dbparity/dbparity.pro QMAKE_CXXFLAGS+=-Wno-error=implicit-function-declaration 'QMAKE_LIBS_OPENGL=-framework OpenGL'`.
- `QMAKE_LIBS_OPENGL=-framework OpenGL` is required on the current macOS SDK: Qt 6.7.3 otherwise links the removed `AGL` framework (`ld: framework 'AGL' not found`).
- If qmake/make says "The macosx platform SDK has been changed … wipe the build directory including .qmake.stash", do exactly that (`rm -rf` the build dir, re-run qmake). A stale test Makefile can also reference generated files that no longer exist (e.g. `ui_trkisto.h`) — same remedy.
- After a successful `build-pg` build, install it: quit `/Applications/Kitsas_PG.app` if running, `rm` then `cp` `build-pg/kiswaspg.app/Contents/MacOS/kiswaspg` (the qmake `TARGET` is `kiswaspg` since PR #18; the bundle's `CFBundleExecutable` is still `kitsas`, so keep that destination name) over `/Applications/Kitsas_PG.app/Contents/MacOS/kitsas`, `chmod 755`, then `codesign --force --sign - /Applications/Kitsas_PG.app` (the swapped binary invalidates the signature and arm64 refuses to launch it), and delete the throwaway `build-pg/kiswaspg.app`. The bundle's `PlugIns/sqldrivers` (QPSQL) and `Frameworks/libpq.5.dylib` were patched in by hand and must survive — replace only the executable.

## Tests

Each unit test is its own qmake project under `unittest/`; there's no single "run all tests" command — build and run each `.pro` individually (mirrors `.github/workflows/ci.yml`):

```
mkdir -p build-tests/<name> && cd build-tests/<name>
qmake ../../unittest/<name>/<name>.pro -spec linux-g++ "CONFIG+=release"
make -j
./<name>
```

Test projects: `eurotest`, `viitetesti`, `ValidatorTest`, `KieliTesti`, `tositerivitesti`, `liitecachetest`, `vakaustesti`, `dbparity`, `AlvLaskelmaTesti`. Most link `QT += testlib` only; `dbparity` and `vakaustesti` additionally pull in `kitsas/sources.pri` (the whole app's sources) via `unittest/apptest.pri`, since they exercise real app classes (`dbparity` real `SQLiteModel`/`PostgresModel` instances).

Headless/CI runs need `QT_QPA_PLATFORM=offscreen`.

### dbparity (SQLite vs PostgreSQL backend parity)

`unittest/dbparity` is the authoritative test for backend equivalence — schema (tables/columns/seed rows), and binding semantics (JSON-as-string vs JSON-as-bytea, `Liite` bytea roundtrip, `lastInsertId()`). It needs a live Postgres reachable via env vars:

```
KITSAS_REQUIRE_POSTGRES=1
KITSAS_PG_HOST=localhost  KITSAS_PG_PORT=5432
KITSAS_PG_USER=kitsas     KITSAS_PG_PASSWORD=kitsas
KITSAS_PG_ADMIN_DB=postgres
```

`docker-compose.yml` at the repo root brings up a matching local Postgres. Without `KITSAS_REQUIRE_POSTGRES=1` the Postgres-only cases are expected to skip rather than fail.

Any time a SQLite/Postgres behavioral discrepancy is found or fixed, log it in [kitsas/postgres/MIGRATION_NOTES.md](kitsas/postgres/MIGRATION_NOTES.md) — it's the running log for this ongoing migration effort, keep it current rather than rediscovering the same gotchas.

`sqliteTuoja_kopioiKaikkiTaulut` and `sqliteTuoja_hylkaaVaarinVersioidunTiedoston` in `tst_dbparity.cpp` cover `SqliteTuoja` specifically: a full round-trip through a real source SQLite file into a throwaway, schema-only target Postgres database (`<KITSAS_PG_DB>_tuonti`, created/dropped alongside the main parity database), checking row counts, marks, attachment bytes, the legacy `tosite=0` → `NULL` normalization, the QByteArray-into-text defensive re-stringify, and identity-sequence resync — plus a fast, Postgres-independent check that an old-schema-version source file is rejected before ever touching Postgres. The QPSQL driver plugin in this Qt distribution is linked against a hardcoded `/Applications/Postgres.app/...` path for `libpq` that may not exist on a given machine; if a locally-built `Kitsas_PG.app` bundle exists, set `DYLD_FALLBACK_LIBRARY_PATH` to its `Contents/Frameworks` directory (which has its own bundled `libpq.5.dylib`) before running `dbparity`, or `QPSQL driver not loaded` will make every Postgres-dependent test report Postgres as unavailable instead of actually running. Caveat (2026-09-21): the `initTestCase` failure `PostgreSQL is not available: ` (empty message) is not a driver problem — the admin-DB connection opens fine and `Kitsas_PG.app/Contents/Frameworks` works as `DYLD_FALLBACK_LIBRARY_PATH`. It comes from `TestDb::pudotaJaLuoPostgresTietokanta()` → `PostgresModel::luoTietokanta()` returning false without setting `postgresVirhe_`, which on the maintainer's machine happens because the default `kitsas_parity` database is owned by the `iljamikkonen` superuser, so the `kitsas` role (CREATEDB only) cannot `DROP` it. Either run with a database the test role owns (`KITSAS_PG_DB=kitsas_parity_x`, and `DROP DATABASE` it afterwards) or use a superuser role; ideally make `pudotaJaLuoPostgresTietokanta()` report why it failed. Note that each shell command starts with a fresh environment — export the `KITSAS_*` variables in the same command that runs `dbparity`, or Postgres silently counts as unavailable. `TestDb::avaaSqlite()`/`avaaPostgres()` take an optional init map, for tests that need wizard-shaped `UusiVelho::data()` instead of the minimal `TestDb::initials()`. In test lambdas returning `QVariant` use `QTest::qVerify(...)`-and-`return {}` (as `suoritaMolemmissa` does), not `QVERIFY`/`QVERIFY2`, which `return;` and don't compile there.

## Architecture

### Backend abstraction: `YhteysModel` → `SqlModel` → `SQLiteModel` / `PostgresModel`

All data access — whether the book lives in a local `.kitsas` SQLite file, a local/self-hosted Postgres client database, or (historically) Kitsas Oy's cloud service — goes through a single abstract interface, `YhteysModel` (`kitsas/db/yhteysmodel.h`). The rest of the app (`kirjaus/`, `raportti/`, `laskutus/`, etc.) talks only to this interface via `KpKysely` and never touches SQL or a specific backend directly.

- `SqlModel` (`kitsas/sql/sqlmodel.h`) is the shared base for both local backends (SQLite and Postgres) — owns the `QSqlDatabase` connection and dispatches `KpKysely` requests to the same `SQLiteRoute`-derived classes. SQLite-only concerns (PRAGMAs, file operations) stay in `SQLiteModel`.
- `SQLiteRoute` subclasses live in `kitsas/sqlite/routes/` — each implements one REST-like resource (`tositeroute.cpp`, `tilitroute.cpp`, `saldotroute.cpp`, etc.), addressed by a URL-style path (`polku`). These routes are shared verbatim between the SQLite and Postgres backends — only the underlying `QSqlDatabase` driver differs, so a route bug affects both, and query SQL must stay portable between SQLite and PostgreSQL syntax/semantics. Two rules that have each already caused a silent production bug (see MIGRATION_NOTES.md #6): every non-aggregated column in a `SELECT … GROUP BY` must be listed in the `GROUP BY` (SQLite lets bare columns through, Postgres rejects the statement), and never compare a text expression with an unquoted number (`CAST(tili AS text) < '3'`, not `< 3`). Routes ignore a failed `exec()` and return an empty list/map, so a Postgres-only SQL error surfaces in the UI as "nothing shown", not as an error — when a Postgres book shows empty data that SQLite shows fine, first run the route's exact query with `psql` in a `BEGIN READ ONLY` transaction.
- `KpKysely` (`kitsas/db/kpkysely.h`) is the request object callers build (method GET/POST/PATCH/PUT/DELETE + path + attributes), matching the same shape historically used to talk to the cloud REST API — this is why local storage is modeled as routed "queries" rather than direct SQL calls from UI code.
- `Kirjanpito` (`kitsas/db/kirjanpito.h`) is the app-wide singleton owning the current book: holds the active `YhteysModel` plus all the higher-level models (accounts, periods, allocations, invoicing...). `KitsasInterface` (`kitsas/db/kitsasinterface.h`) is the interface `Kirjanpito` implements, used so tests can substitute a partial fake without pulling in the whole dependency graph.

### SQLite ↔ PostgreSQL parity is an active, ongoing effort

Postgres support (`kitsas/postgres/`) is newer than the SQLite backend and mirrors its schema (`postgres/luo.sql` vs `sqlite/luo.sql`) and route logic, but the two databases have real semantic differences that have caused silent data corruption before — see [MIGRATION_NOTES.md](kitsas/postgres/MIGRATION_NOTES.md) for the specifics (bytea vs text binding for JSON columns, transaction-abort-on-error differences, identity/autoincrement sequence resync after bulk import, `jsonb` strictness). When touching shared route code (`kitsas/sqlite/routes/`) or either `luo.sql`, assume the change must behave identically on both backends, and check `dbparity` still passes.

`kitsas/postgres/sqlitetuoja.{h,cpp}` (`SqliteTuoja`) imports an existing `.kitsas` SQLite file into a freshly-created, schema-initialized Postgres database — the in-app equivalent of `kitsas/postgres/migrate_sqlite_to_pg.py`, which is the standalone Python/psycopg2 version of the same migration (kept in sync with the C++ importer and with MIGRATION_NOTES.md). `kitsas/postgres/repair_bytea_json.py` repairs already-corrupted JSON columns in an existing Postgres database (see notes file, fix #1) — a one-off remediation script, not part of the normal import path.

Kitsas PG's server-login customer list (`PostgresModel::listaaTietokannat`, `kitsas/postgres/postgresmodel.cpp`) probes every database on the server once (`onkoKitsasTietokanta`: does it have an `Asetus` row with `avain='KpVersio'`?) and permanently caches any database that fails the probe as "not Kitsas" per-server, in the `PostgresEiKitsasKannat` QSettings key, so it's never re-probed. There is currently no UI to clear this cache. If a database fails the probe even once — e.g. queried mid-import before `Asetus` was populated, or during a transient QPSQL-driver-not-loaded hiccup (see `dbparity` note above) — it silently and permanently disappears from the customer list even after becoming a valid Kitsas database. The only fix today is editing the cached array out of the settings file by hand (`~/Library/Preferences/io.github.danale72.Kiswas PG.plist` → `PostgresEiKitsasKannat` → `<host>:<port>` → remove the database name) with the app quit first so it doesn't get overwritten on exit. The app identifies itself as "Kiswas PG" (`setOrganizationDomain("danale72.github.io")` / `setOrganizationName`/`setApplicationName` in `kitsas/main.cpp`), which is what determines that settings path; older builds used `fi.kitsas-pg.Kitsas PG.plist`, which is now stale. Worth adding a "forget non-Kitsas databases"/refresh action to the connection dialog to make this self-healing.

### Other major subsystems (each under `kitsas/`)

- `model/` — value/data model classes independent of storage (e.g. `Tosite`/`TositeVienti` = voucher/voucher line, `Euro` = fixed-point currency type, `Lasku` = invoice).
- `kirjaus/` — the voucher entry UI (the app's core day-to-day screen).
- `raportti/` — financial reports (income statement, balance sheet, etc.).
- `laskutus/` — invoicing.
- `alv/` — VAT (arvonlisävero) handling and reporting.
- `tilinpaatoseditori/` — financial statement (tilinpäätös) editor.
- `arkisto/` / `arkistoija/` — document/attachment archive.
- `tuonti/` — importers (CSV, bank statement PDFs via `pdftiliote/`, Tesseract OCR, payroll).
- `pilvi/` — Kitsas Oy cloud-service client integration.
- `maaritys/`, `uusikirjanpito/` — settings and the "new book" creation wizard (`uusivelho`).
- `kieli/` — i18n/translation helpers; UI strings translated via `tr/kitsas_en.ts`, `tr/kitsas_sv.ts` (source language is Finnish).

## Notes

- Do not rebuild/recompile the app after edits unless explicitly asked to — the user runs builds themselves in batches.
- When building a new feature or fixing a bug, add or extend an automated test in the same pass rather than as an afterthought — don't wait to be asked separately. `unittest/dbparity` (see above) is the natural home for anything touching the SQL layer or backend parity; the smaller `unittest/*` projects suit isolated logic (e.g. `eurotest` for `Euro`, `viitetesti` for reference-number validation). If a change genuinely isn't testable this way (pure UI layout, a one-off migration script), say so explicitly rather than silently skipping tests.
