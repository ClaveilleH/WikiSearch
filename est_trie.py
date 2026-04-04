#!/usr/bin/env python3
import re
"""
Verifie que le fichier donné est trié
"""
DUMP_FILE = "frwiki-page-ids.txt"


def is_sorted(filename):
    with open(filename, "r") as f:
        previous = None
        for line in f:
            match = re.match(r"(\d+)\s+(.+)", line)
            if not match:
                continue
            page_id, title = match.groups()
            if previous is not None and title < previous:
                return False
            previous = title
    return True


if __name__ == "__main__":
    if is_sorted(DUMP_FILE):
        print("Le fichier est trié.")
    else:
        print("Le fichier n'est pas trié.")