#!/usr/bin/env python3
import re

# Fichier dump original
DUMP_FILE = "frwiki-latest-page.sql"

# Nouveau fichier filtré
OUTPUT_FILE = "frwiki-pages-namespace0.sql"

# Nombre de tuples par INSERT
CHUNK_SIZE = 500
"""
On fait des gros chunks pour réduire le nombre de requêtes d'insertion, ce qui accélère l'import dans MySQL.
"""

# Regex pour matcher un tuple : (id,namespace,'title',...)
tuple_re = re.compile(r"\((\d+),(\d+),'((?:[^'\\]|\\.)*)'")

with open(DUMP_FILE, "r", encoding="utf-8") as fin, \
     open(OUTPUT_FILE, "w", encoding="utf-8") as fout:

    buffer = []
    fout.write("ALTER TABLE page_simple DISABLE KEYS;\n") # On désactive les clés pour accélérer les insertions, on les réactive à la fin
    for line in fin:
        if line.startswith("INSERT INTO `page`"):
            for match in tuple_re.finditer(line):
                page_id = match.group(1)
                namespace = int(match.group(2))
                title = match.group(3).replace("\\'", "''")  # corrige les apostrophes

                if namespace == 0:
                    buffer.append(f"({page_id},'{title}')")

                    # Si on atteint CHUNK_SIZE, écrit un INSERT
                    if len(buffer) >= CHUNK_SIZE:
                        fout.write("INSERT INTO page_simple VALUES " + ",".join(buffer) + ";\n")
                        buffer = []

    # Écrire les tuples restants
    if buffer:
        fout.write("INSERT INTO page_simple VALUES " + ",".join(buffer) + ";\n")
    fout.flush()
    fout.write("ALTER TABLE page_simple ENABLE KEYS;\n") # On réactive les clés après l'insertion

print(f"Fichier filtré créé : {OUTPUT_FILE}")