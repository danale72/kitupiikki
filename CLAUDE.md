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

## Tests

Each unit test is its own qmake project under `unittest/`; there's no single "run all tests" command — build and run each `.pro` individually (mirrors `.github/workflows/ci.yml`):

```
mkdir -p build-tests/<name> && cd build-tests/<name>
qmake ../../unittest/<name>/<name>.pro -spec linux-g++ "CONFIG+=release"
make -j
./<name>
```

Test projects: `eurotest`, `viitetesti`, `ValidatorTest`, `KieliTesti`, `tositerivitesti`, `dbparity`, `AlvLaskelmaTesti`. Most link `QT += testlib` only; `dbparity` additionally pulls in `kitsas/sources.pri` (the whole app's sources) via `unittest/apptest.pri`, since it exercises real `SQLiteModel`/`PostgresModel` instances.

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

## Architecture

### Backend abstraction: `YhteysModel` → `SqlModel` → `SQLiteModel` / `PostgresModel`

All data access — whether the book lives in a local `.kitsas` SQLite file, a local/self-hosted Postgres client database, or (historically) Kitsas Oy's cloud service — goes through a single abstract interface, `YhteysModel` (`kitsas/db/yhteysmodel.h`). The rest of the app (`kirjaus/`, `raportti/`, `laskutus/`, etc.) talks only to this interface via `KpKysely` and never touches SQL or a specific backend directly.

- `SqlModel` (`kitsas/sql/sqlmodel.h`) is the shared base for both local backends (SQLite and Postgres) — owns the `QSqlDatabase` connection and dispatches `KpKysely` requests to the same `SQLiteRoute`-derived classes. SQLite-only concerns (PRAGMAs, file operations) stay in `SQLiteModel`.
- `SQLiteRoute` subclasses live in `kitsas/sqlite/routes/` — each implements one REST-like resource (`tositeroute.cpp`, `tilitroute.cpp`, `saldotroute.cpp`, etc.), addressed by a URL-style path (`polku`). These routes are shared verbatim between the SQLite and Postgres backends — only the underlying `QSqlDatabase` driver differs, so a route bug affects both, and query SQL must stay portable between SQLite and PostgreSQL syntax/semantics.
- `KpKysely` (`kitsas/db/kpkysely.h`) is the request object callers build (method GET/POST/PATCH/PUT/DELETE + path + attributes), matching the same shape historically used to talk to the cloud REST API — this is why local storage is modeled as routed "queries" rather than direct SQL calls from UI code.
- `Kirjanpito` (`kitsas/db/kirjanpito.h`) is the app-wide singleton owning the current book: holds the active `YhteysModel` plus all the higher-level models (accounts, periods, allocations, invoicing...). `KitsasInterface` (`kitsas/db/kitsasinterface.h`) is the interface `Kirjanpito` implements, used so tests can substitute a partial fake without pulling in the whole dependency graph.

### SQLite ↔ PostgreSQL parity is an active, ongoing effort

Postgres support (`kitsas/postgres/`) is newer than the SQLite backend and mirrors its schema (`postgres/luo.sql` vs `sqlite/luo.sql`) and route logic, but the two databases have real semantic differences that have caused silent data corruption before — see [MIGRATION_NOTES.md](kitsas/postgres/MIGRATION_NOTES.md) for the specifics (bytea vs text binding for JSON columns, transaction-abort-on-error differences, identity/autoincrement sequence resync after bulk import, `jsonb` strictness). When touching shared route code (`kitsas/sqlite/routes/`) or either `luo.sql`, assume the change must behave identically on both backends, and check `dbparity` still passes.

`kitsas/postgres/sqlitetuoja.{h,cpp}` (`SqliteTuoja`) imports an existing `.kitsas` SQLite file into a freshly-created, schema-initialized Postgres database — the in-app equivalent of `kitsas/postgres/migrate_sqlite_to_pg.py`, which is the standalone Python/psycopg2 version of the same migration (kept in sync with the C++ importer and with MIGRATION_NOTES.md). `kitsas/postgres/repair_bytea_json.py` repairs already-corrupted JSON columns in an existing Postgres database (see notes file, fix #1) — a one-off remediation script, not part of the normal import path.

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
