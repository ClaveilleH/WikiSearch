#!/usr/bin/env python3
"""
filter_pagelinks.py
-------------------
Filtre un dump SQL MariaDB de la table `pagelinks` (Wikipedia)
pour ne garder que les liens entre pages du namespace 0.

Usage:
    python filter_pagelinks.py --ids ids.txt --sql pagelinks.sql(.gz) --out output.txt

Le fichier de sortie contient une paire par ligne :
    (id_origine,id_dest)
"""

import argparse
import gzip
import re
import sys
import time
from pathlib import Path

# --------------------------------------------------------------------------- #
# Parsing des arguments                                                         #
# --------------------------------------------------------------------------- #

def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ids",  required=True, help="Fichier des IDs namespace-0 (un par ligne)")
    p.add_argument("--sql",  required=True, help="Dump SQL (.sql ou .sql.gz)")
    p.add_argument("--out",  required=True, help="Fichier de sortie")
    p.add_argument("--sep",  default=",",   help="Séparateur de sortie (défaut: virgule)")
    return p.parse_args()

# --------------------------------------------------------------------------- #
# Chargement des IDs valides                                                    #
# --------------------------------------------------------------------------- #

def load_ids(path: str) -> set[int]:
    print(f"[1/3] Chargement des IDs depuis {path} …", flush=True)
    ids = set()
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line:
                try:
                    ids.add(int(line))
                except ValueError:
                    pass  # ignore les lignes non-numériques
    print(f"      {len(ids):,} IDs chargés.", flush=True)
    return ids

# --------------------------------------------------------------------------- #
# Ouverture du fichier SQL (brut ou gzip)                                       #
# --------------------------------------------------------------------------- #

def open_sql(path: str):
    p = Path(path)
    if p.suffix == ".gz" or path.endswith(".sql.gz"):
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return open(path, "r", encoding="utf-8", errors="replace")

# --------------------------------------------------------------------------- #
# Parsing d'une ligne INSERT                                                    #
#                                                                               #
# Format attendu :                                                              #
#   INSERT INTO `pagelinks` VALUES (pl_from, pl_from_namespace, pl_target_id), #
#                                  (…), … ;                                    #
# --------------------------------------------------------------------------- #

# Regex pour extraire les tuples entiers d'une ligne INSERT
RE_TUPLE = re.compile(r"\((\d+),(\d+),(\d+)\)")

def parse_insert_line(line: str):
    """Retourne un itérateur de (pl_from, pl_from_namespace, pl_target_id)."""
    return (
        (int(m.group(1)), int(m.group(2)), int(m.group(3)))
        for m in RE_TUPLE.finditer(line)
    )

# --------------------------------------------------------------------------- #
# Filtrage principal                                                            #
# --------------------------------------------------------------------------- #

def filter_links(sql_path: str, valid_ids: set[int], out_path: str, sep: str):
    print(f"[2/3] Lecture du dump SQL {sql_path} …", flush=True)

    written   = 0
    skipped   = 0
    lines_read = 0
    t0        = time.time()

    with open_sql(sql_path) as src, open(out_path, "w", encoding="utf-8") as dst:
        for raw_line in src:
            lines_read += 1

            # Affichage de progression toutes les 100 000 lignes
            if lines_read % 100_000 == 0:
                elapsed = time.time() - t0
                print(f"      {lines_read:,} lignes lues | {written:,} paires écrites "
                      f"| {elapsed:.1f}s", flush=True)

            # On ne traite que les lignes INSERT
            if not raw_line.startswith("INSERT INTO"):
                continue

            for pl_from, pl_from_ns, pl_target_id in parse_insert_line(raw_line):
                # Filtre namespace : pl_from_namespace doit être 0
                # ET la page source ET la page cible doivent être dans valid_ids
                if (pl_from_ns == 0
                        and pl_from      in valid_ids
                        and pl_target_id in valid_ids):
                    dst.write(f"({pl_from}{sep}{pl_target_id})\n")
                    written += 1
                else:
                    skipped += 1

    elapsed = time.time() - t0
    print(f"      Terminé en {elapsed:.1f}s.", flush=True)
    return written, skipped

# --------------------------------------------------------------------------- #
# Point d'entrée                                                                #
# --------------------------------------------------------------------------- #

def main():
    args = parse_args()

    valid_ids = load_ids(args.ids)
    written, skipped = filter_links(args.sql, valid_ids, args.out, args.sep)

    print(f"[3/3] Résultats écrits dans {args.out}")
    print(f"      ✓ {written:,} paires conservées")
    print(f"      ✗ {skipped:,} lignes filtrées (namespace ≠ 0 ou hors liste)")

if __name__ == "__main__":
    main()



# python3 filter_pagelinks.py --ids frwiki-page-namespace0-ids.txt --sql frwiki-latest-pagelinks.sql --out liens_ns0.txt