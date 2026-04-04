#!/usr/bin/env python3
"""
Génère pages_ns0.txt : une ligne par page namespace 0, format (page_id,page_title)
Source : frwiki-latest-page.sql
"""
import re

DUMP_FILE  = "frwiki-latest-page.sql"
OUTPUT_FILE = "pages_ns0.output"

# varbinary exporté par mysqldump : les titres sont entre quotes, apostrophes échappées en \'
tuple_re = re.compile(r"\((\d+),(\d+),'((?:[^'\\]|\\.)*)'")

count = 0
with open(DUMP_FILE, "r", encoding="utf-8", errors="replace") as fin, \
     open(OUTPUT_FILE, "w", encoding="utf-8") as fout:

    for line in fin:
        if not line.startswith("INSERT INTO `page`"):
            continue
        for m in tuple_re.finditer(line):
            page_id   = int(m.group(1))
            namespace = int(m.group(2))
            if namespace != 0:
                continue
            # Restaure les apostrophes : \' -> '
            title = m.group(3).replace("\\'", "'")
            fout.write(f"({page_id},{title})\n")
            count += 1

print(f"Pages écrites : {count}")
