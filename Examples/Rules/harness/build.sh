#!/bin/bash
# Build the host-side rules-engine harness.
# Compiles the real firmware rules engine (HeishaMon/src/rules) for the host
# and links it with harness.cpp. Requires g++ and python3.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$DIR/../../../HeishaMon"

# Generate the list of valid @names (topics + commands) from the firmware
# tables so the harness validates rulesets against the real name set.
python3 - "$SRC" "$DIR/valid_names.h" <<'EOF'
import re, sys
src_dir, out_path = sys.argv[1], sys.argv[2]
decode = open(src_dir + '/decode.h').read()
def topics(name):
    m = re.search(r'(?<![A-Za-z_])' + name + r'\[\]\[[^\]]*\]\s*PROGMEM\s*=\s*\{(.*?)\};', decode, re.S)
    return re.findall(r'"([^"]+)"', m.group(1))
cmds_src = open(src_dir + '/commands.h').read()
def cmds(name):
    m = re.search(name + r'\[\]\s*PROGMEM\s*=\s*\{(.*?)\n\};', cmds_src, re.S)
    return re.findall(r'\{\s*"([^"]+)"', m.group(1))
names = topics('topics') + topics('xtopics') + topics('optTopics') \
      + cmds(r'const cmdStruct commands') + cmds(r'const optCmdStruct optionalCommands')
with open(out_path, 'w') as out:
    out.write('static const char *valid_at_names[] = {\n')
    for n in names:
        out.write('  "%s",\n' % n)
    out.write('  0\n};\n')
print('valid_names.h: %d names' % len(names))
EOF

g++ -std=gnu++17 -O1 -g -Wall -I "$DIR/shim" -I "$SRC" -I "$DIR" -include "$DIR/shim/Arduino.h" \
  "$DIR/harness.cpp" \
  "$SRC"/src/rules/rules.cpp "$SRC"/src/rules/function.cpp "$SRC"/src/rules/operator.cpp \
  "$SRC"/src/rules/functions/*.cpp \
  "$SRC"/src/common/mem.cpp "$SRC"/src/common/uint32float.cpp "$SRC"/src/common/strnicmp.cpp \
  -o "$DIR/harness"

echo "built: $DIR/harness"
