# Builtin IME data

`tgh2013.tsv` and `traditional-readings.tsv` are reproducible extracts of Unicode Unihan 17.0.0:
https://www.unicode.org/Public/17.0.0/ucd/Unihan.zip
It retains `kTGH` (the complete 2013 standard table and its original indices),
`kTGHZ2013` (Mandarin readings), `kTraditionalVariant` (character variants),
`kBigFive` (common traditional characters) and the characters’ own `kMandarin`/
`kXHC1983` readings. `traditional-readings.tsv` retains these actual readings;
we do not copy every simplified reading onto every traditional variant.
SHA256 of the source archive is recorded in `upstream.json`.
The data is licensed under Unicode License V3; see `UNICODE-LICENSE.txt`.
Tone marks are removed, ü uses v, and lüe/nüe accept both lue/nue and lve/nve.

`frequency.tsv` comes from pxa-apps/pinyin-ime-lite and the Apache-2.0 licensed
lvgl_pinyin_ime dictionary at commit 037169998150a768ff43863fc44d563222d4b801:
https://github.com/VanillaNahida/lvgl_pinyin_ime
The original dictionary SHA256 and source are retained in the file header.
Only the order of existing single-character candidates is used; no word or
sentence model is included. See `APACHE-LICENSE.txt`.

Simplified candidates are exactly the 8,105 characters in the standard table.
Traditional candidates include their Unicode character counterparts and
Big5 level-one characters, including one-to-many alternatives. This preserves
independent traditional characters such as 后 (queen) and 干 (shield). This is a character IME, not a context-sensitive
sentence conversion engine. Shared characters are valid in either script.
Generated tables are selected at compile time and stored as immutable data.
Normal compilation needs neither a network connection nor upstream archives.
