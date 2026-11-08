#!/bin/bash
# One step of work on a grammar: stop at unresolved conflicts of its table, otherwise build
# the package, run it over prepared files and group the syntax errors.
#
# usage: AGAS=path/to/agas tools/step.sh GRAMMAR CORPUS [run options]
#
#     AGAS=../borsuk/build/bin/agas tools/step.sh grammars/gnu99.ag /tmp/grammar-stress/c/gnu99
#     AGAS=... tools/step.sh grammars/cGNU.ag /tmp/grammar-stress/c --sample 10 --seed 1
#     AGAS=... OUT=results/retry tools/step.sh grammars/cGNU.ag /tmp/grammar-stress/c \
#         --only-failing results/cGNU/results.tsv
#
# Run it from the repository root. The report goes to OUT, by default results/<grammar>.
set -u

grammar=$1
corpus=$2
shift 2
name=$(basename "$grammar" .ag)
out=${OUT:-results/$name}
package=/tmp/grammar-stress-$name
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# --table of a grammar with a name table shows only numbers, so it goes through a copy
# without the commands of the table, where conflicts show the rules behind them.
sed -E 's/ -> (pushScope|popScope|reopenScope|captureName(, declare\([A-Za-z]+\))?|declare\([A-Za-z]+\))//; s/ -> nameOf\(Identifier\)//' \
    "$grammar" > "$work/plain.ag"
timeout 250 "$AGAS" --table "$work/plain.ag" > "$work/table.txt" 2>&1
grep -E '^grammar=|error:|did not' "$work/table.txt" | head -4 | cut -c1-110
if grep -q 'unresolved conflict' "$work/table.txt"; then
    grep -A3 'unresolved conflict' "$work/table.txt" | grep 'source=' | sed -E 's/ at=.*//; s/^ +//' |
        sort | uniq -c | sort -rn | head -14 | cut -c1-80
    grep -m4 -A3 'unresolved conflict' "$work/table.txt" | cut -c1-110
    exit 1
fi

rm -rf "$package"
"$AGAS" --emit-package "$package" "$grammar" > /dev/null 2> "$work/emit.txt" || {
    tail -2 "$work/emit.txt" | cut -c1-120
    exit 1
}

start=$SECONDS
timeout 400 build/bin/grammar-stress run "$corpus" --preprocessed --agas "$AGAS" --package "$package" \
    --out "$out" "$@" > /dev/null 2>&1
echo "pass took $((SECONDS - start)) s"
timeout 150 python3 -I "$(dirname "$0")/report_errors.py" "$out/results.tsv" "$corpus" 2>&1 | cut -c1-330
