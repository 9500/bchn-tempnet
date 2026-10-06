#!/usr/bin/env bash
#
# Copyright (c) 2026 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# An alternative spelling linter allowing for spelling errors to be suppressed per-line.
#
# To use:
# 1) Add a "NOLINT(SPELLING)" comment at the end of lines that have spelling errors to be ignored.
# 2) Add a new `check_spelling` instruction at the end of this files with the incorrect spelling and
#    its correction so it can be caught elsewhere.
# 3) Remove the corresponding spelling correction entry from `test/lint/dictionary/english.json`.

export LC_ALL=C

file="$1"

[ -z "$file" ] && exit 0
[ ! -f "$file" ] && exit 0

check_spelling() {
    bad="$1"
    correction="$2"
    awk -v file="$file" -v bad="$bad" -v correction="$correction" '
        /NOLINT\(SPELLING\)/ {
            next
        }
        index($0, bad) {
            printf "%s:%d:SPELLING:Misspelling found: use \"%s\", not \"%s\"\n", file, FNR, correction, bad
            exit
        }
    ' "$file"
}

check_spelling "reenabled" "re-enabled"  # NOLINT(SPELLING)

exit 0
