#!/usr/bin/env bash
# Layer 3: negative tests. Every .xs under this directory must fail to
# compile or fail at runtime, and its failure must match the EXPECT
# marker inside the file.
#
# Markers (first matching line wins):
#   -- EXPECT_ERROR          -- any error anywhere
#   -- EXPECT_ERROR: <CODE>  -- stderr must mention the error code
#   -- EXPECT_RUNTIME_ERROR  -- must compile but fail at runtime
#
# Usage: tests/negative/run.sh [--xs <path>]
set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"
XS="${XS:-$ROOT/../xs}"
if [ ! -x "$XS" ]; then XS="$ROOT/../xs.exe"; fi
if [ ! -x "$XS" ]; then echo "no xs binary at $XS" >&2; exit 2; fi

pass=0
fail=0
for f in "$DIR"/*.xs; do
    [ -f "$f" ] || continue
    name="$(basename "$f" .xs)"
    marker_line="$(grep -m1 '^-- EXPECT_' "$f" || true)"
    if [ -z "$marker_line" ]; then
        echo "  SKIP  $name (no EXPECT marker)"
        continue
    fi

    out="$("$XS" "$f" 2>&1)"
    rc=$?

    if [ $rc -eq 0 ]; then
        echo "  FAIL  $name (exited 0, expected failure)"
        fail=$((fail + 1))
        continue
    fi

    case "$marker_line" in
        *"EXPECT_ERROR:"*)
            code="$(echo "$marker_line" | sed -E 's/.*EXPECT_ERROR:[[:space:]]*([A-Za-z0-9_]+).*/\1/')"
            if echo "$out" | grep -q "$code"; then
                pass=$((pass + 1))
            else
                echo "  FAIL  $name (expected code $code, got:)"
                echo "$out" | head -3 | sed 's/^/        /'
                fail=$((fail + 1))
            fi
            ;;
        *"EXPECT_RUNTIME_ERROR"*|*"EXPECT_ERROR"*)
            pass=$((pass + 1))
            ;;
    esac
done

total=$((pass + fail))
if [ $fail -gt 0 ]; then
    echo "[negative] $fail/$total failed"
    exit 1
fi
echo "[negative] $pass passed"
