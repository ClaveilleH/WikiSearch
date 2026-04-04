#!/usr/bin/env python3
import re

# Fichier dump original
DUMP_FILE = "frwiki-latest-pagelinks.sql"
# Nouveau fichier filtré
OUTPUT_FILE = "frwiki-pagelinks-namespace0.sql"
# Nombre de tuples par INSERT
CHUNK_SIZE = 500

"""
Structure de la table pagelinks :
  pl_from              int(8)   -- ID de la page source
  pl_from_namespace    int(11)  -- namespace de la page source
  pl_target_id         bigint   -- ID de la page cible (dans linktarget)

On garde uniquement les liens dont pl_from_namespace == 0 (articles principaux).
"""

# Regex pour matcher un tuple : (pl_from, pl_from_namespace, pl_target_id)
tuple_re = re.compile(r"\((\d+),(\d+),(\d+)\)")

with open(DUMP_FILE, "r", encoding="utf-8") as fin, \
     open(OUTPUT_FILE, "w", encoding="utf-8") as fout:

    buffer = []

    fout.write("ALTER TABLE pagelinks_simple DISABLE KEYS;\n")

    for line in fin:
        if line.startswith("INSERT INTO `pagelinks`"):
            for match in tuple_re.finditer(line):
                pl_from          = match.group(1)
                pl_from_namespace = int(match.group(2))
                pl_target_id     = match.group(3)

                if pl_from_namespace == 0:
                    buffer.append(f"({pl_from},{pl_target_id})")

                if len(buffer) >= CHUNK_SIZE:
                    fout.write("INSERT INTO pagelinks_simple VALUES " + ",".join(buffer) + ";\n")
                    buffer = []

    if buffer:
        fout.write("INSERT INTO pagelinks_simple VALUES " + ",".join(buffer) + ";\n")

    fout.flush()
    fout.write("ALTER TABLE pagelinks_simple ENABLE KEYS;\n")

print(f"Fichier filtré créé : {OUTPUT_FILE}")


"""
CREATE TABLE pagelinks_simple (
  pl_from      INT(8) UNSIGNED NOT NULL,
  pl_target_id BIGINT(20) UNSIGNED NOT NULL,
  PRIMARY KEY (pl_from, pl_target_id)
) ENGINE=InnoDB;

"""