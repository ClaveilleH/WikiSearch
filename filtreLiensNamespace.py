#!/usr/bin/env python3
import re

# Fichier dump original
DUMP_FILE = "frwiki-latest-pagelinks.sql"
ID_DUMP_FILE = "frwiki-page-namespace0-ids.txt"
# Nouveau fichier filtré
OUTPUT_FILE = "frwiki-pagelinks-namespace0.txt" # Format texte simple : id \n

"""
Structure de la table pagelinks :
  pl_from              int(8)   -- ID de la page source
  pl_from_namespace    int(11)  -- namespace de la page source
  pl_target_id         bigint   -- ID de la page cible (dans linktarget)

On garde uniquement les liens dont pl_from_namespace == 0 (articles principaux).
"""

# Regex pour matcher un tuple : (pl_from, pl_from_namespace, pl_target_id)
tuple_re = re.compile(r"\((\d+),(\d+),(\d+)\)")

with open(ID_DUMP_FILE, "r", encoding="utf-8") as id_file:
    # ids_namespace0 = [int(line.strip()) for line in id_file]
    ids_namespace0 = set()
    for line in id_file:
        ids_namespace0.add(int(line.strip()))

print(f"Nombre d'IDs dans le namespace 0 : {len(ids_namespace0)}")

def is_namespace0(pl_from):
    """
    La liste est triée, on peut faire une recherche dichotomique
    """
    return pl_from in ids_namespace0
    left, right = 0, len(ids_namespace0) - 1
    while left <= right:
        mid = (left + right) // 2
        if ids_namespace0[mid] == pl_from:
            return True
        elif ids_namespace0[mid] < pl_from:
            left = mid + 1
        else:
            right = mid - 1
    return False

with open(DUMP_FILE, "r", encoding="utf-8") as fin, \
     open(OUTPUT_FILE, "w", encoding="utf-8") as fout:


    for line in fin:
        if line.startswith("INSERT INTO `pagelinks`"):
            for match in tuple_re.finditer(line):
                pl_from          = match.group(1)
                pl_from_namespace = int(match.group(2))
                pl_target_id     = match.group(3)

                if pl_from_namespace == 0 and is_namespace0(int(pl_from)):
                    fout.write(f"{pl_from},{pl_target_id}\n")
    fout.flush()

print(f"Fichier filtré créé : {OUTPUT_FILE}")


"""
CREATE TABLE pagelinks_simple (
  pl_from      INT(8) UNSIGNED NOT NULL,
  pl_target_id BIGINT(20) UNSIGNED NOT NULL,
  PRIMARY KEY (pl_from, pl_target_id)
) ENGINE=InnoDB;

"""