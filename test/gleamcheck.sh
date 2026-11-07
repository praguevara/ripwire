#!/usr/bin/env bash
# Gleam: the grammar, definitions and their kinds, module-qualified and unqualified-import calls resolved through the
# import's own Step-A, the locals that shadow (and do not shadow) a module, `@external` spans, dependency edges, test
# modules, and the deterministic cache round trip. Fixture: test/gleamfix/, one package with a decoy module
# (src/other/board.gleam) that defines the same names as src/app/board.gleam and that no call may reach.
#
# What the grammar and the extractor cannot see, pinned here so a change to it is a decision:
#   * a local named like an imported module (`world` the record, `world` the module) reads `world.f` as the module's f
#     when the module defines f, and as the local's field otherwise: Gleam tries the field first, so a record whose
#     field and its module's function share a name binds the function (a floor, not tested: the fixture has none);
#   * a constructor in a case PATTERN and a type in an annotation are not references; only construction is.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-${RIPWIRE_BIN:-$ROOT/build/ripwire}}"
[ "${BIN#/}" = "$BIN" ] && BIN="$ROOT/$BIN"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir "$TMP/cache"
export XDG_CACHE_HOME="$TMP/cache"
cp -R "$ROOT/test/gleamfix" "$TMP/fix"

"$BIN" "$TMP/fix" --no-cache >"$TMP/cold.xml"
xmllint --noout "$TMP/cold.xml"
python3 - "$TMP/cold.xml" <<'PY'
import re, sys, xml.etree.ElementTree as ET
raw = open(sys.argv[1]).read()
t = ET.parse(sys.argv[1])
head = re.search(r'<!-- files=[^>]*-->', raw).group(0)
# Every call names its module lexically: no ambiguity, and nothing in the tree is unresolved.
assert ' ambiguous=0 ' in head and ' unresolved=0 ' in head, head
assert re.search(r' external=[1-9]', head), ('gleam/list and gleam/result calls are external', head)
by = {}
for f in t.iter('f'):
    for s in f.iter('s'):
        by[(f.get('p'), s.get('n'), s.get('t'))] = s
kinds = {('src/app/board.gleam', 'place'): 'fn', ('src/app/board.gleam', 'now'): 'fn', ('src/app/board.gleam', 'size'): 'var',
         ('src/app/board.gleam', 'Cell'): 'struct', ('src/app/board.gleam', 'Empty'): 'var', ('src/app/board.gleam', 'Taken'): 'var',
         ('src/app/board.gleam', 'Grid'): 'alias', ('src/app/world.gleam', 'apply'): 'fn', ('test/app/turn_test.gleam', 'update_test'): 'fn'}
for (p, n), k in kinds.items():
    assert (p, n, k) in by, ('definition and kind', p, n, k, sorted(by))
# An opaque type and its constructor share a name: one row each, a struct and a var.
assert ('src/app/board.gleam', 'Board', 'struct') in by and ('src/app/board.gleam', 'Board', 'var') in by
print('PASS definitions: functions, an @external function, constants, custom and opaque types, constructors, an alias')
def calls(p, n):
    return {c.get('n') for c in by[(p, n, 'fn')].iter('c')}
update = calls('src/app/turn.gleam', 'update')
# `board.new()`, `put(who)` (= `place as put`), `Taken(who)` unqualified, `board.place(_, who)` as a capture, `w.apply`
# through an alias, `check` in the same module, and a `use` callback's call `result.try(check(state))`.
assert {'new', 'place', 'Taken', 'apply', 'check'} <= update, update
assert calls('src/app/turn.gleam', 'check') == {'allows'}, calls('src/app/turn.gleam', 'check')
assert calls('test/app/turn_test.gleam', 'update_test') == {'update', 'World'}, calls('test/app/turn_test.gleam', 'update_test')
assert calls('src/app/board.gleam', 'hidden') == {'apply', 'size'}, calls('src/app/board.gleam', 'hidden')
# board.place's parameter is named `board`: `board.cells` is its field, in the module that does not import itself.
assert calls('src/app/board.gleam', 'place') == {'Board', 'Taken'}, calls('src/app/board.gleam', 'place')
# step.then: `world` is a parameter and the module. `world.turn` is the field; `world.allows` the module's function.
assert calls('src/app/step.gleam', 'then') == {'allows'}, calls('src/app/step.gleam', 'then')
# step.chain: `let apply = f; apply(1)` calls the closure, never step.apply or world.apply.
assert calls('src/app/step.gleam', 'chain') == set(), calls('src/app/step.gleam', 'chain')
print('PASS calls: qualified, aliased, unqualified and renamed imports, captures, use callbacks, locals named like a module')
PY

# The decoy: src/other/board.gleam defines place and new too, and no call reaches it.
"$BIN" "$TMP/fix" --callers=src/other/board.gleam:place --no-cache >"$TMP/decoy.xml"
rg -q '<callers [^>]*count="0"' "$TMP/decoy.xml"
"$BIN" "$TMP/fix" --callers=src/app/board.gleam:place --no-cache >"$TMP/place.xml"
rg -q '<callers [^>]*count="1"' "$TMP/place.xml"
rg -q 'n="update" p="src/app/turn.gleam:6"' "$TMP/place.xml"
echo 'PASS the decoy module defining the same names gets no caller'

# A dotted module selector names the module's file stem, as Python's `json.dump` does.
"$BIN" "$TMP/fix" --expand=world.apply --no-cache >"$TMP/expand.xml"
rg -q 'sym="apply:6"' "$TMP/expand.xml"
rg -q '<src p="src/app/world.gleam"' "$TMP/expand.xml"
echo 'PASS --expand=world.apply'

# An @external function is bodyless and still a definition; its span starts at the attribute run, so the signature
# carries the FFI target and the /// doc above the attributes is read.
"$BIN" "$TMP/fix" --for='the host clock' --no-cache >"$TMP/for.xml"
python3 - "$TMP/for.xml" <<'PY'
import sys, xml.etree.ElementTree as ET
rows = [d for d in ET.parse(sys.argv[1]).iter('d') if d.get('n') == 'now']
assert len(rows) == 1, rows
text = ''.join(rows[0].itertext())
assert text.startswith('The host clock') and '@external(erlang, "os", "system_time")' in text and 'pub fn now() -> Int' in text, text
print('PASS @external: doc, attribute run and signature')
PY

# Imports are dependency edges, resolved under the package's src/ and test/, unique-or-degrade; gleam/* is external.
"$BIN" "$TMP/fix" --deps --no-cache >"$TMP/deps.xml"
python3 - "$TMP/deps.xml" <<'PY'
import re, sys
raw = open(sys.argv[1]).read()
assert re.search(r'dep_langs="[^"]*gleam', raw), 'Gleam is dependency-capable'
rows = dict(re.findall(r'<f p="([^"]+)" includes="(\d+)"', raw))
assert rows.get('src/app/turn.gleam') == '4' and rows.get('test/app/turn_test.gleam') == '3', rows
afferent = dict(re.findall(r'<f p="([^"]+)" afferent="(\d+)"/>', raw))
assert afferent.get('src/app/board.gleam') == '1' and afferent.get('src/app/world.gleam') == '4', afferent
assert 'src/other/board.gleam" afferent' not in raw, 'no import names the decoy'
print('PASS --deps: module paths resolve to their files, the decoy to none')
PY
"$BIN" "$TMP/fix" --affected=src/app/board.gleam --no-cache >"$TMP/affected.xml"
rg -q '<test p="test/app/turn_test.gleam"' "$TMP/affected.xml"
echo 'PASS a _test.gleam module under test/ is a test: --affected reaches it'

"$BIN" "$TMP/fix" >"$TMP/warm-a.xml"
"$BIN" "$TMP/fix" >"$TMP/warm-b.xml"
cmp "$TMP/cold.xml" "$TMP/warm-a.xml"
cmp "$TMP/warm-a.xml" "$TMP/warm-b.xml"
( cd "$TMP" && "$BIN" fix --no-cache >"$TMP/relative.xml" )
python3 - "$TMP/cold.xml" "$TMP/relative.xml" <<'PY'
import sys, xml.etree.ElementTree as ET
def facts(p):
    root = ET.parse(p).getroot()
    root.attrib.pop('root', None)
    root.attrib.pop('est_tokens', None)
    return ET.tostring(root)
assert facts(sys.argv[1]) == facts(sys.argv[2])
PY
echo 'PASS deterministic, warm == cold, a relative and an absolute root answer alike'
"$BIN" "$TMP/fix" --skipped --no-cache >"$TMP/skipped.xml"
rg -q '<lang n="gleam" files="6"' "$TMP/skipped.xml"
if rg -q 'degraded-parse' "$TMP/skipped.xml"; then
    echo 'Unexpected degraded Gleam parse' >&2
    exit 1
fi
PATH="$(cd "$(dirname "$BIN")" && pwd):$PATH" "$BIN" "$TMP/fix" --doctor >"$TMP/doctor.xml"
rg -q '<c n="grammars" ok="1" loaded="27" expected="27"' "$TMP/doctor.xml"
echo 'gleamcheck: ALL PASS'
