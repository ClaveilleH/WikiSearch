#!/usr/bin/env python3
"""
filter_pagelinks.py  –  version haute performance
--------------------------------------------------
Filtre un dump SQL MariaDB de la table `pagelinks` (Wikipedia)
pour ne garder que les liens entre pages du namespace 0.

Usage:
    python filter_pagelinks.py --ids ids.txt --sql pagelinks.sql --out output.txt

Sortie : une paire par ligne →  (id_origine,id_dest)

Optimisations pour 300 M+ de tuples :
  - Parser manuel des VALUES (pas de regex)
  - Écriture bufférisée par batch de 100 000 lignes
  - Lecture SQL en gros blocs (buffering=8 Mo)
  - Set d'IDs en int natif Python (O(1) lookup)
"""

import argparse
import sys
import time

BATCH_SIZE    = 100_000    # lignes accumulées avant écriture disque
REPORT_EVERY  = 5_000_000  # tuples parsés entre chaque affichage

# --------------------------------------------------------------------------- #
# Arguments                                                                     #
# --------------------------------------------------------------------------- #

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--ids",  required=True, help="Fichier des IDs (un par ligne)")
    p.add_argument("--sql",  required=True, help="Dump SQL brut (.sql)")
    p.add_argument("--out",  required=True, help="Fichier de sortie")
    return p.parse_args()

# --------------------------------------------------------------------------- #
# Chargement des IDs                                                            #
# --------------------------------------------------------------------------- #

def load_ids(path: str) -> set:
    print(f"[1/3] Chargement des IDs depuis '{path}' …", flush=True)
    ids = set()
    with open(path, "r") as fh:
        for line in fh:
            s = line.strip()
            if s:
                ids.add(int(s))
    print(f"      {len(ids):,} IDs chargés en mémoire.", flush=True)
    return ids

# --------------------------------------------------------------------------- #
# Parser manuel ultra-rapide des tuples VALUES                                  #
#                                                                               #
# Format : INSERT INTO `pagelinks` VALUES (a,b,c),(a,b,c), … ;                #
# Parcours caractère par caractère — pas de regex, pas de split global.         #
# --------------------------------------------------------------------------- #

def iter_tuples(line: str):
    """
    Itère sur les tuples (pl_from, pl_from_namespace, pl_target_id)
    d'une ligne INSERT, sans regex.
    """
    pos = line.find("VALUES ")
    if pos == -1:
        return
    pos += 7  # saute "VALUES "

    n = len(line)
    while pos < n:
        # cherche '('
        while pos < n and line[pos] != '(':
            pos += 1
        if pos >= n:
            break
        pos += 1  # saute '('

        # champ 1 : pl_from
        start = pos
        while pos < n and line[pos] != ',':
            pos += 1
        pl_from = int(line[start:pos])
        pos += 1  # saute ','

        # champ 2 : pl_from_namespace
        start = pos
        while pos < n and line[pos] != ',':
            pos += 1
        pl_from_ns = int(line[start:pos])
        pos += 1  # saute ','

        # champ 3 : pl_target_id
        start = pos
        while pos < n and line[pos] not in (',', ')'):
            pos += 1
        pl_target_id = int(line[start:pos])

        yield pl_from, pl_from_ns, pl_target_id

        # avance jusqu'après ')'
        while pos < n and line[pos] != ')':
            pos += 1
        pos += 1  # saute ')'

# --------------------------------------------------------------------------- #
# Filtrage principal                                                            #
# --------------------------------------------------------------------------- #

def filter_links(sql_path: str, valid_ids: set, out_path: str):
    print(f"[2/3] Lecture de '{sql_path}' …", flush=True)

    written = 0
    total   = 0
    t0      = time.time()
    batch   = []

    # Buffers larges : 8 Mo en lecture, 8 Mo en écriture
    with open(sql_path, "r", encoding="utf-8", errors="replace",
              buffering=1 << 23) as src, \
         open(out_path, "w", encoding="utf-8",
              buffering=1 << 23) as dst:

        for raw_line in src:
            if not raw_line.startswith("INSERT INTO"):
                continue

            for pl_from, pl_from_ns, pl_target_id in iter_tuples(raw_line):
                total += 1

                if (pl_from_ns == 0
                        and pl_from      in valid_ids
                        and pl_target_id in valid_ids):
                    batch.append(f"({pl_from},{pl_target_id})\n")
                    written += 1

                    if len(batch) >= BATCH_SIZE:
                        dst.writelines(batch)
                        batch.clear()

                if total % REPORT_EVERY == 0:
                    elapsed = time.time() - t0
                    rate    = total / elapsed / 1_000_000
                    print(f"      {total/1_000_000:.0f}M tuples | "
                          f"{written:,} conservés | "
                          f"{rate:.2f} M/s | "
                          f"{elapsed:.0f}s",
                          flush=True)

        # vide le dernier batch
        if batch:
            dst.writelines(batch)

    elapsed = time.time() - t0
    print(f"      Terminé en {elapsed:.1f}s  —  "
          f"{total/elapsed/1_000_000:.2f} M tuples/s en moyenne.", flush=True)
    return written, total

# --------------------------------------------------------------------------- #
# Point d'entrée                                                                #
# --------------------------------------------------------------------------- #

def main():
    args = parse_args()
    ids  = load_ids(args.ids)
    written, total = filter_links(args.sql, ids, args.out)

    print(f"[3/3] Résultats → '{args.out}'")
    print(f"      ✓ {written:,} paires conservées  ({100*written/max(total,1):.1f} %)")
    print(f"      ✗ {total - written:,} tuples filtrés")

if __name__ == "__main__":
    main()