# Mozilla/curl CA bundle provenance

Source URL: https://curl.se/ca/cacert.pem
Extraction information: https://curl.se/docs/caextract.html
Mozilla source: https://github.com/mozilla-firefox/firefox/blob/release/security/nss/lib/ckfw/builtins/certdata.txt
Snapshot source date: 2026-02-11 18:26:30 GMT (bundle header).
Converter: `mk-ca-bundle.pl` 1.32. Exact source commit: **not recorded**.
TrussC import: `f59a10fd`; no subsequent payload edits.

| File | SHA-256 (complete working-tree bytes) |
| --- | --- |
| `cacert.pem` | `b6e66569cc3d438dd5abe514d0df50005d570bfc96c14dca8f768d020cb96171` |

The header's producer SHA256 (`3b98d4e3ff57a326d9587c33633039c8c3a9cf0b55f7ca581d7598ff329eb1f3`)
is separate metadata, not the hash of the full PEM containing that comment.
Mozilla's source certificate database is under MPL-2.0; curl's extraction
page documents the bundle's source/license. Exact standalone certificate-data
redistribution terms were not preserved at import and were not independently
authenticated offline. Retain the bundle header and certificate notices.

`../scripts/update_ca_bundle.sh` is the existing refresh mechanism. It downloads
from the source URL and checks for a PEM certificate marker; no refresh was run
for this record-only change. On an authorized future refresh, review source
notices and certificate changes and update this hash/date. This is an inventory
record, not a freshness, revocation or trust-policy review.
