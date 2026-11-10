# /// script
# requires-python = ">=3.11"
# ///
"""Resolve merge conflicts in a .resw where both sides appended <data> blocks: keep both, then
re-close any <data> left open by git aligning on a shared "</data>" line, and check for duplicates."""
import collections
import re
import sys
import xml.dom.minidom

path = sys.argv[1]
s = open(path, encoding="utf-8-sig").read()
s = re.sub(r"<<<<<<< [^\n]*\n(.*?)(?:\|\|\|\|\|\|\| [^\n]*\n.*?)?=======\n(.*?)>>>>>>> [^\n]*\n",
           lambda m: m.group(1) + m.group(2), s, flags=re.S)
# A <value>…</value> line followed directly by a new <data> or comment lost its </data>.
s = re.sub(r"(</value>\n)(\s*(?:<data |<!--|</root>))", r"\1  </data>\n\2", s)
open(path, "w", encoding="utf-8").write(s)
d = xml.dom.minidom.parse(path)
names = [e.getAttribute("name") for e in d.getElementsByTagName("data")]
dupes = [k for k, c in collections.Counter(names).items() if c > 1]
print(f"{len(names)} keys" + (f", DUPLICATES: {dupes}" if dupes else ""))
sys.exit(1 if dupes else 0)
