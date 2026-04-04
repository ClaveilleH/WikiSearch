#!/usr/bin/env python3
"""
Génère linktarget_ns0.txt : mapping lt_id -> page_title pour namespace 0
Source : frwiki-latest-linktarget.sql

Format sortie : (lt_id,page_title)
Ce fichier est intermédiaire — utilisé par extract_links.py
"""
import re

DUMP_FILE   = "frwiki-latest-linktarget.sql"
OUTPUT_FILE = "linktarget_ns0.output"

# (lt_id, lt_namespace, lt_title)
tuple_re = re.compile(r"\((\d+),(-?\d+),'((?:[^'\\]|\\.)*)'\)")

count = 0
with open(DUMP_FILE, "r", encoding="utf-8", errors="replace") as fin, \
     open(OUTPUT_FILE, "w", encoding="utf-8") as fout:

    for line in fin:
        if not line.startswith("INSERT INTO `linktarget`"):
            continue
        for m in tuple_re.finditer(line):
            lt_id     = int(m.group(1))
            namespace = int(m.group(2))
            if namespace != 0:
                continue
            title = m.group(3).replace("\\'", "'")
            fout.write(f"({lt_id},{title})\n")
            count += 1

print(f"Linktargets écrits : {count}")
