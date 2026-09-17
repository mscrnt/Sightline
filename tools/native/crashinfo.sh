#!/bin/sh
# crashinfo - turn a crash/hang report into source lines.
#
# The report the game writes contains raw addresses, which name nothing on
# their own. This resolves them against the binary that produced them.
#
#   tools/native/crashinfo.sh                    newest report in /tmp
#   tools/native/crashinfo.sh /tmp/sightline-crash-1234.txt
#
# The binary MUST be the one that crashed - rebuild after a crash and the
# addresses resolve to the wrong lines, silently. If in doubt, reproduce first
# and resolve second.
set -u
cd "$(dirname "$0")/../.."
BIN=build/native/skeleton
REPORT="${1:-$(ls -t /tmp/sightline-crash-*.txt 2>/dev/null | head -1)}"

[ -n "$REPORT" ] && [ -e "$REPORT" ] || { echo "crashinfo: no report found" >&2; exit 1; }
[ -x "$BIN" ] || { echo "crashinfo: no $BIN" >&2; exit 1; }

echo "crashinfo: $REPORT"
if [ "$BIN" -nt "$REPORT" ]; then
    echo "  WARNING: $BIN is NEWER than the report - addresses may be stale."
fi
echo

sed -n 's/.*=== sightline \(.*\) ===.*/kind: \1/p' "$REPORT"
grep -E '^  frame ' "$REPORT"
echo
echo "stack (innermost first):"
grep -oE '0x[0-9a-f]{6,}' "$REPORT" | while read -r a; do
    line=$(addr2line -f -e "$BIN" -C "$a" 2>/dev/null | tr '\n' ' ' | sed 's#/mnt/projects/sightline/##')
    case "$line" in
        "?? ??:0 "|"") ;;
        *) printf "  %-12s %s\n" "$a" "$line" ;;
    esac
done
