#!/usr/bin/env python3
import re

DUMP_FILE = "frwiki-pages-namespace0.sql"
# OUTPUT_FILE = "frwiki-page-ids.txt"
OUTPUT_FILE = "pages_ns0.txt"

# Capture simplement le premier champ du tuple : (id,...
tuple_re = re.compile(r"\((\d+),")
# Capture les deux premiers champs du tuple : (id, title,...
tuple_re = re.compile(r"\((\d+),\s*'([^']*)'")

with open(DUMP_FILE, "r", encoding="utf-8") as fin, \
     open(OUTPUT_FILE, "w", encoding="utf-8") as fout:

    for line in fin:
        if line.startswith("INSERT INTO page_simple"):
            for match in tuple_re.finditer(line):
                page_id = match.group(1)
                # print(match.group(2))
                page_name = match.group(2) if len(match.groups()) > 1 else "onne"
                # fout.write(page_id + "\n")
                fout.write("(" + page_id + "," + page_name + ")\n")

print(f"Fichier créé : {OUTPUT_FILE}")
