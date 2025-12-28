#!/usr/bin/env bash
# Layer 4: property-based tests. Each file uses tests/property/harness.xs
# to run N randomised iterations of each property. Failures print the
# offending input so you can add it to tests/regression/ as a fixture.
set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"
XS="${XS:-$ROOT/../xs}"
[ -x "$XS" ] || XS="$ROOT/../xs.exe"
if [ ! -x "$XS" ]; then echo "no xs binary at $XS" >&2; exit 2; fi

cd "$ROOT/.."  # load paths in *.xs are relative to project root

fail=0
for f in "$DIR"/*.xs; do
    name="$(basename "$f" .xs)"
    [ "$name" = "harness" ] && continue
    out="$("$XS" --lenient "$f" 2>&1)"
    if echo "$out" | grep -q "^\[property:.*0 failed"; then
        echo "$out" | grep -E "^\[property|^  ok" | tail -5
    else
        echo "  FAIL  $name"
        echo "$out" | grep -E "FAIL|fail" | head -5 | sed 's/^/        /'
        fail=1
    fi
done

if [ $fail -ne 0 ]; then
    echo "[property] FAILED"
    exit 1
fi
echo "[property] all suites passed"
