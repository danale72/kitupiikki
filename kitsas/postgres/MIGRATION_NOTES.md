# SQLite → PostgreSQL migration notes

Living log of schema/behavior discrepancies found while testing the Postgres
backend against a real SQLite-derived data model. Intent: once enough of
these are catalogued, build a proper `.kitsas` (SQLite) → PostgreSQL import
tool that applies all the necessary conversions up front, instead of hitting
them one at a time in production.

Branch: `kitsas-pg-local-setup`. Update this file whenever a new
SQLite-vs-Postgres discrepancy is found, whether or not it's fixed yet.

## Fixed

### 1. JSON columns silently corrupted / hard failures on write (FIXED)

**Where:** `SQLiteRoute::mapToJson()` in `kitsas/sqlite/sqliteroute.cpp`
(shared by both backends), plus one inline duplicate in
`TositeRoute::lisaaTaiPaivita()` (`kitsas/sqlite/routes/tositeroute.cpp`).

**Cause:** Both built a `QByteArray` from `QJsonDocument::toJson()` and bound
it directly via `QSqlQuery::addBindValue()`. Qt's QPSQL driver serializes a
bound `QByteArray` using PostgreSQL's binary `bytea` hex-escape format
(`\x7b22...`), not as plain text. SQLite's driver has no such distinction —
it stores the raw bytes as given — so this was completely invisible under
SQLite and only surfaces against real Postgres.

**Effect at the schema level:**
- Columns declared plain `text` (`Tosite.json`, `Vienti.json`,
  `Kumppani.json`, `Tuote.json`, `Kohdennus.json`, etc. — see below) accept
  anything, so they silently stored the garbled bytea-hex string instead of
  the real JSON, with **no error at all**. Confirmed with a real corrupted
  row: `Kumppani` "EK-Pro Rakennus Oy" (created through the app) has
  `json = \x7b226b617570756e6b69223a...` instead of readable JSON.
- The one column declared strict `jsonb` (`Tositeloki.data`) **rejects** the
  bytea-hex string outright: `ERROR: invalid input syntax for type json`.
  Since that insert is the last statement in the voucher-save transaction,
  Postgres aborts the *entire* transaction — so a "voucher saved" success
  message could still result in zero rows actually persisted (the earlier
  `Tosite`/`Vienti` inserts in the same transaction get rolled back too).
  This is a general SQLite-vs-Postgres semantic gap worth remembering:
  SQLite does not abort a whole transaction the way Postgres does on the
  first failing statement inside one.

**Fix applied:** `mapToJson()` now returns `QString`
(`QString::fromUtf8(...)`) instead of `QByteArray`; same change applied to
the inline duplicate in `tositeroute.cpp`. A bound `QString` is always sent
as plain text regardless of driver. Verified directly against
`im_kirjanpito`: the same `jsonb` insert that previously errored now stores
clean, valid JSON.

**Migration tool implication:** existing `.kitsas` SQLite files are **not**
expected to have this corruption themselves — SQLite doesn't reinterpret
`QByteArray` binds, so their `json`/`text` columns should already hold
correct plain-text JSON. The risk is entirely on the *write* side: an import
tool must bind these values as `QString`/text when inserting into Postgres,
not reproduce the original bug.

**Repaired:** `kitsas/postgres/repair_bytea_json.py` — scans every
`json`-bearing table for the `\x`-prefixed hex-corruption pattern, decodes
it back to the original UTF-8 JSON (fully recoverable, not data loss), and
updates the row. Run against `im_kirjanpito`: found and fixed exactly one
row (`Kumppani` id=2, "EK-Pro Rakennus Oy" — real customer data from an
invoice test, not junk to discard), confirmed the decoded JSON was sane
(real address/city/postcode fields), verified with a follow-up `--dry-run`
that zero corrupted rows remain anywhere in the database. Reusable for any
other Postgres client database created with a pre-fix build.

One gotcha hit while writing it: don't use `psycopg2.sql.Identifier()` for
these table/column names — it double-quotes them, and since the schema was
created with unquoted `CREATE TABLE Kumppani` (Postgres folds unquoted
identifiers to lowercase), a quoted `"Kumppani"` reference doesn't match the
actual stored `kumppani` table. Plain unquoted string interpolation (safe
here — names come from the fixed TABLES list, not user input) is what
actually matches how the app itself queries these tables.

### 2. Balance/saldo calculations regressed by a wrong "fix" (FIXED, was broken twice)

**Where:** `kitsas/sqlite/routes/saldotroute.cpp`, 6 occurrences.

**History:** the original code used `CAST(tili as text) >= '3'` / `< '3'` to
split balance-sheet accounts (1xxx-2xxx) from income-statement accounts
(3xxx+). This got misdiagnosed as a lexicographic-string-comparison bug (it
genuinely is a string comparison) and "fixed" to `tili >= 3` / `< 3` — a
real regression. The mistake: verifying *that* it does string comparison is
not the same as verifying *what the correct numeric equivalent is*. For
real 4-digit account numbers, `CAST(... as text) >= '3'` is exactly
equivalent to `tili >= 3000` (comparing the leading digit works out to a
leading-digit-times-1000 threshold only because every real account number
is uniformly 4 digits) — not `tili >= 3`, which wrongly includes accounts
like 1910 (a balance-sheet bank account) in what should be an
income-statement-only filter. Caught by a second Copilot review pass on
PR #2, confirmed by direct comparison against real account numbers before
re-fixing. Corrected (at the time) to `tili >= 3000` / `< 3000`.

**Second regression (found via a real customer import, "Buplace Oy"):** the
"4-digit assumption" flagged in the lesson below as a caveat turned out to
be a real bug, not just a theoretical one. Real books have user-created
*sub-accounts* that extend a standard 4-digit account with extra trailing
digits, e.g. account `29412` (a custom sub-account of `2941`, "Siirtovelat",
tyyppi `BS`/Velat — an ordinary balance-sheet liability). Numerically
`29412 >= 3000`, so `tili >= 3000` routed it into the income-statement
bucket and `tili < 3000` excluded it from the balance-sheet bucket
entirely — it vanished from "Muut velat" on the tase report *and* its
balance polluted the tulos (income statement) calculation as if it were
revenue/expense, in both `kitsas/tilikartat/*/raportit.json` line matching
(unaffected — that part already does correct string-prefix matching on the
account number) and the underlying `/saldot` data feeding it. Reproduced
identically on SQLite and Postgres — this route is shared verbatim between
backends, so it was never a backend-parity issue, just a latent bug that
only became visible once a book with a real 5-digit sub-account was
inspected. Confirmed via `unittest/dbparity`
(`route_saldotSisaltaaViisinumeroisenVelkatilin`) and against a live
customer database (`Vienti`/`Tili` row counts and sums for the affected
account, plus the whole-ledger debit=credit total, matched the source
`.kitsas` file exactly — ruling out `SqliteTuoja` import loss as the cause).

**Fix:** reverted all 6 occurrences to the original `CAST(tili as text)
>= '3'` / `< '3'` form, matching the sibling routes
(`tilikaudetroute.cpp`, `eraroute.cpp`, `budjettiroute.cpp`) that were never
"fixed" and so never had this problem. This correctly treats any account
number by its leading digit regardless of how many trailing digits a
sub-account adds.

**Lesson for this file specifically:** don't "fix" a SQLite→Postgres
portability concern here without checking what numeric threshold the
existing string comparison actually produces against real chart-of-accounts
data first — the string comparison may be intentional (if unusual) rather
than accidental. In this case it was: the "it only works because every
account is 4 digits" caveat from the first fix wasn't a footnote, it was
the actual bug waiting to happen, and it happened as soon as a customer
book with a real sub-account was imported.

### 3. Dangling `Tosite`/`Vienti.kumppani` references not enforced by SQLite (FIXED)

**Where:** `kopioiTaulu()` in `kitsas/postgres/sqlitetuoja.cpp`
(`SqliteTuoja`), mirrored in `migrate_table()` in
`kitsas/postgres/migrate_sqlite_to_pg.py`.

**Cause:** Both `Tosite.kumppani` and `Vienti.kumppani` are nullable foreign
keys to `Kumppani(id)`. SQLite doesn't enforce FK constraints by default, so
a `.kitsas` file can accumulate rows whose `kumppani` points at a
`Kumppani` row that was deleted later — invisible under SQLite, but
Postgres enforces the constraint on every insert. Hit in practice on a real
customer file (`Edlen.kitsas`): `INSERT INTO vienti ... violates foreign key
constraint "vienti_kumppani_fkey" ... Key (kumppani)=(34) is not present in
table "kumppani"`, which aborted the whole import.

**Fix applied:** same treatment as the existing `Liite.tosite=0` case just
below — a dangling `kumppani` reference is a legitimate "no counterparty"
value, not corrupt data worth failing the import over. Both importers now
check each `Tosite`/`Vienti` row's `kumppani` against the target's actual
`Kumppani.id` set (already imported earlier in table order) and null out
any value that doesn't match, instead of erroring. The C++ importer
collects a summary ("N rows had a kumppani reference that no longer
existed") and shows it in a message box at the end of a successful import
instead of failing silently or aborting; the Python script prints an
equivalent warning per table.

### 4. Dangling `Vienti.tili` references not enforced by SQLite (FIXED)

**Where:** `kopioiTaulu()` in `kitsas/postgres/sqlitetuoja.cpp` (`SqliteTuoja`),
mirrored in `migrate_table()` in `kitsas/postgres/migrate_sqlite_to_pg.py`.

**Cause:** `Vienti.tili` is a nullable FK to `Tili(numero)`. SQLite doesn't
enforce it, so a `.kitsas` file can contain `Vienti` rows whose `tili` is `0`
or otherwise not present in `Tili` — invisible under SQLite, but Postgres
enforces the constraint on every insert. Hit in practice on a real customer
file (`OLEKSENO.kitsas`): `INSERT INTO vienti ... violates foreign key
constraint "vienti_tili_fkey" ... Key (tili)=(0) is not present in table
"tili"`, which aborted the whole import.

`tili=0` specifically is not corrupt data — it's the legitimate "no account
chosen yet" state for a draft (`Tosite::LUONNOS`) voucher line, e.g. one leg
of a bank-statement import (`TositeTyyppi::TUONTI`) the bookkeeper hasn't
categorized yet. `model/tosite.cpp`'s `paivitaVirheet()` already checks for
exactly this (`!kp()->tilit()->tili(vienti.tili())`) and raises
`Tosite::TILIPUUTTUU`, which `kirjauswg.cpp` treats as an acceptable
draft-only state rather than a hard error.

**Fix applied:** same treatment as the `Tosite`/`Vienti.kumppani` case above
— a `Vienti.tili` value not present in the target's `Tili.numero` set (which
includes `0`, since no `Tili` row is ever seeded with that number) is nulled
out instead of failing the import, and counted into the same
huomiot/warning summary shown at the end. Verified this doesn't change
validation behavior: `TositeVienti::tili()` reads the column via
`data(TILI).toInt()`, and `QVariant().toInt()` is `0`, so a `NULL` tili is
read back as `0` and still trips `TILIPUUTTUU` exactly as the original
`tili=0` value did. Covered by `sqliteTuoja_kopioiKaikkiTaulut` in
`unittest/dbparity` (same draft voucher used for the kumppani-dangling
case, with one leg's `tili` forced to `0` via raw SQL before import).

Only `Vienti.tili` is handled this way — `Budjetti.tili` is part of that
table's primary key (`PRIMARY KEY (tilikausi, kohdennus, tili)`, so it can't
be nulled without dropping the row) and `Vakioviite.tili` has no equivalent
"not yet chosen" semantics in the app; neither has been observed to hit
this in practice, so they're left as hard failures for now if it ever comes
up.

### 5. Embedded NUL bytes in old, already-corrupted text data (FIXED)

**Where:** `siivoaJsonb()`/`poistaNulit()` and the generic per-cell check in
`kopioiTaulu()` in `kitsas/postgres/sqlitetuoja.cpp` (`SqliteTuoja`), mirrored
in `sanitize_jsonb()`/`strip_json_nuls()`/`decode_and_strip_nul()` in
`kitsas/postgres/migrate_sqlite_to_pg.py`.

**Cause:** PostgreSQL's text-backed types (`text`, `varchar`, `json`,
`jsonb`) can **never** store byte `0x00` under any circumstances — its
internal representation is a C string. SQLite has no such restriction at
all. Hit in practice on a real customer file (`SannaHirvonenTmi.kitsas`):
`INSERT INTO tositeloki ... ERROR: unsupported Unicode escape sequence ...
DETAIL: \u0000 cannot be converted to text`, which aborted the import.

Root cause was a single genuinely corrupted `Vienti.arkistotunnus` value
(a tiliote-import bank reference code), evidently mangled years ago by an
encoding bug elsewhere (`pdftiliote/`, unrelated to `SqliteTuoja`): 8
characters of a reference like `202404085936192V6420A` had become 3 garbage
multi-byte characters plus 5 raw `0x00` bytes. SQLite stored it unquestioned.
This single corruption surfaced in two different forms, both fatal to
Postgres:
- **`Vienti.arkistotunnus` itself** — the column had been written via a
  `QByteArray` bind into a text-affinity column (the same class of legacy
  bug as bug #1 above), so it came back from SQLite as raw bytes containing
  literal `0x00`.
- **Two `Tositeloki.data` audit-log snapshots** captured at save time — there
  a compliant JSON writer had correctly escaped the embedded NULs as the
  6-character sequence `\u0000`, which is syntactically **valid** JSON per
  spec. `QJsonDocument::fromJson()` parses it fine, so the existing
  malformed-JSON-to-NULL check in `siivoaJsonb()` didn't catch it — the
  failure only happens deep in Postgres's own jsonb parser, when it tries to
  materialize the escape into an actual (impossible) NUL-containing text
  value.

**Fix applied:** `siivoaJsonb()` now keeps the parsed `QJsonDocument` (rather
than discarding it after just checking for a parse error) and recursively
walks it via `poistaNulit()`, stripping any embedded NUL character found in
a string value and re-serializing only if something changed (cheap
no-op/unchanged-text path otherwise). Separately, `kopioiTaulu()` now runs a
generic check on every non-binary cell after all the existing per-column
transformations: any `QString` value containing an embedded NUL has it
stripped. Together these cover both places the same corruption showed up.
Both are additive/lossy-only-on-the-corrupt-byte sanitization, not a
null-out-the-whole-value policy like bugs #3/#4 above, since the surrounding
content (e.g. the rest of the reference code) is still real, useful data.
Counted into the same `huomiot`/warning summary. `Liite.data` (genuine
binary) is explicitly excluded from the generic string check since it never
becomes a `QString` in the first place.

The Python script's version additionally had to gain the "any column can
come back as raw `bytes` if written via a legacy `QByteArray` bind"
decode-to-text step generically (`decode_and_strip_nul()`) — the C++
importer already had this for the QByteArray case (bug #1's follow-up), but
the Python script previously only applied it to the `json` column
specifically, not every column, so it would have hit a `bytes`-into-`text`
type mismatch on `arkistotunnus` independent of the NUL-byte issue.

Covered by `sqliteTuoja_kopioiKaikkiTaulut` in `unittest/dbparity`: forces
one `Vienti.arkistotunnus` to a `QByteArray` with embedded NUL bytes (same
`QByteArray`-bind mechanism as the `sha` case above), and inserts a
`Tositeloki` row with a hand-built JSON string containing valid `\u0000`
escapes, then asserts both come back post-import as `"REFTAIL"` — the
garbage-but-real surrounding text preserved, only the un-storable NUL bytes
removed.

## Schema differences to account for in a migration tool

### Auto-increment: `AUTOINCREMENT` (SQLite) vs `GENERATED ... AS IDENTITY` (Postgres)

`kitsas/sqlite/luo.sql` uses `INTEGER PRIMARY KEY AUTOINCREMENT` for
`Kohdennus`, `Kumppani`, `Ryhma`, `Tosite`, `Tositeloki`, `Vienti`, `Liite`,
`Tuote`. `kitsas/postgres/luo.sql` uses
`INTEGER GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY` for the same tables.

Verified `QSqlQuery::lastInsertId()` **does** work correctly against these
Postgres identity columns (tested directly, not just assumed) — that was an
initial suspicion that turned out to be a dead end, ruled out empirically.

What a migration tool *does* need to handle: when bulk-inserting rows with
their original explicit SQLite `id` values (to preserve cross-table
references), Postgres's identity sequence does not know about those IDs and
will keep counting from wherever it left off (starting at 1). After
importing existing rows with explicit IDs, the sequence must be resynced,
e.g.:
```sql
SELECT setval(pg_get_serial_sequence('tosite','id'), (SELECT MAX(id) FROM Tosite));
```
for every identity-column table, or the next natively-generated insert will
collide with an imported ID.

### `Tositeloki.data`: loosely-typed in SQLite, strict `jsonb` in Postgres

SQLite's `luo.sql` declares this column `jsonb` too, but SQLite has no such
native type — by SQLite's type-affinity rules that declaration doesn't match
any recognized affinity keyword, so it falls back to NUMERIC affinity and in
practice just stores whatever text/bytes are given, no validation. Postgres
enforces real `jsonb` validation on every write. A migration tool must
verify/sanitize this column's content before import — any legacy row with
empty string or malformed JSON that SQLite tolerated will hard-fail against
Postgres. Empty/absent values should map to SQL `NULL`, not `''`.

### `Liite.data`: `bytea` in both — no conversion needed

Unlike the JSON columns, `Liite.data` (attachment file contents) is meant to
be genuine binary data in both schemas, and Postgres's `bytea` type is the
correct target for a real `QByteArray` bind. This is the one place binding a
`QByteArray` is *correct* — don't "fix" this one the same way as the JSON
columns.

## Migration script

`kitsas/postgres/migrate_sqlite_to_pg.py` — Python/psycopg2 script that
reads a `.kitsas` SQLite file and inserts its rows into an already-created
Postgres client database (one created via the app's normal "New Postgres
client" flow, so seed rows like `Kohdennus id=0` and `Kumppani
"Verohallinto"` already exist and are skipped from the source). Living
script, expected to change as more discrepancies are found — keep it in
sync with this notes file.

**Fixed via Copilot review on PR #2:** two bugs in the actual write/resync
logic, not just the read side —
1. `migrate_table()` reported `imported {len(values)} row(s)` regardless of
   whether `ON CONFLICT DO NOTHING` silently dropped some rows on a unique/PK
   conflict, which could mask a real migration problem behind a "successful"
   count. Now counts rows before/after the batch insert and explicitly
   prints how many were skipped.
2. `resync_sequences()` used `COALESCE(MAX(id), 1)` as the fallback for an
   empty table, then called `setval(seq, 1)`. Since `setval`'s 2-arg form
   defaults `is_called=true`, that makes the *next* generated id 2, not 1 —
   an empty table's first real insert would skip id=1. Fallback is now `0`.

**Fixed via a third round of Copilot review on PR #2:**
3. `migrate_table()` built its `INSERT` column list directly from the
   source SQLite file's own column names (`rows[0].keys()`), unsanitized.
   Since this tool exists specifically to import client-provided `.kitsas`
   files — a real trust boundary for an accounting-firm workflow, not just
   your own file — a malformed or tampered file could inject SQL via a
   crafted column name. Added `COLUMN_ALLOWLIST`, one set of expected
   columns per table taken from `postgres/luo.sql`; `migrate_table()` now
   raises before building any SQL if the source has a column outside that
   allowlist. Cross-checked the allowlist against the real Desktop-copy
   file's actual SQLite schema — all 18 tables match cleanly, no
   false-positive rejections.

   The `LIKE '\\x%'` pattern in `repair_bytea_json.py` flagged in the same
   review round was a false positive — checked and confirmed empirically
   (inserted a controlled test row, the pattern matched it correctly).
   Postgres applies two separate escape-processing steps here: string
   literal parsing (backslash is literal, not an escape, since
   `standard_conforming_strings` defaults to on) and then `LIKE`'s own
   escape-character handling (backslash by default) applied to the
   resulting pattern — the two cancel out to match a single literal
   backslash correctly. No change needed.

Validated (read-only `--dry-run`) against a copy of a real production file
(`CorosarOy-211206.kitsas`, 3391 vouchers / 22220 line items / 10555 log
entries / 500 partners / 3282 attachments): table and column assumptions
match the current schema version correctly, all 18 tables read without
error. Working against a copy at `~/Desktop/CorosarOy-211206-COPY.kitsas`
(checksum-verified identical to the original), not the live iCloud file,
from this point on.

Also checked that file's `Tositeloki.data` specifically (the column at risk
per the jsonb strictness note above): **all 10555 rows contain clean, valid
JSON, none empty or malformed.** The script's `sanitize_jsonb()` defensive
handling is a safety net for files that might not be this clean (e.g. older
schema versions, files that hit past bugs), not something this particular
file needed — but worth keeping since we can't assume every `.kitsas` file
in the wild is this well-formed.

## Open questions / not yet investigated

- Full diff of the two `luo.sql` files beyond what's noted above (column
  types, defaults, constraints) has not been done exhaustively — only the
  differences discovered through actual testing so far.
- `Vienti`, `Rivi`, and other tables' numeric columns (`real` in SQLite vs
  Postgres `real`/`numeric`) not yet stress-tested for precision differences.
- Script has not yet been run for a real (non-dry-run) import against a live
  Postgres target — only validated the SQLite read side so far.
- Older `.kitsas` files created under earlier `TIETOKANTAVERSIO` schema
  migrations may have different columns than current `luo.sql` assumes —
  not yet tested against an old file.
