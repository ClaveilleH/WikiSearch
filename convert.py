import gzip, re

def parse_page(filepath):
    id_to_title = {}
    pattern = re.compile(r"\((\d+),0,'((?:[^'\\]|\\.)*)','")
    
    with gzip.open(filepath, "rt", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if not line.startswith("INSERT INTO"):
                continue
            for m in pattern.finditer(line):
                page_id = int(m.group(1))
                title = m.group(2).replace("\\'", "'")
                id_to_title[page_id] = title
    return id_to_title

id_to_title = parse_page("frwiki-latest-page.sql.gz")
title_to_id = {v: k for k, v in id_to_title.items()}
print(f"{len(id_to_title)} pages chargées")


def parse_pagelinks(filepath, title_to_id):
    edges = []
    # format : (source_id, target_namespace, target_title, source_namespace)
    pattern = re.compile(r"\((\d+),0,'((?:[^'\\]|\\.)*)',0\)")
    
    with gzip.open(filepath, "rt", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if not line.startswith("INSERT INTO"):
                continue
            for m in pattern.finditer(line):
                source_id = int(m.group(1))
                target_title = m.group(2).replace("\\'", "'")
                # on garde seulement si les deux existent
                if source_id in id_to_title and target_title in title_to_id:
                    edges.append((source_id, title_to_id[target_title]))
    return edges

edges = parse_pagelinks("frwiki-latest-pagelinks.sql.gz", title_to_id)
print(f"{len(edges)} liens chargés")

import igraph as ig

# Créer le graphe dirigé
g = ig.Graph(directed=True)

# Ajouter les nœuds avec leur titre
all_ids = sorted(id_to_title.keys())
g.add_vertices(len(all_ids))
g.vs["name"] = [id_to_title[i] for i in all_ids]

# Index local pour les arêtes
id_to_vertex = {page_id: idx for idx, page_id in enumerate(all_ids)}
edge_list = [
    (id_to_vertex[s], id_to_vertex[t])
    for s, t in edges
    if s in id_to_vertex and t in id_to_vertex
]
g.add_edges(edge_list)

print(g.summary())

# Format binaire rapide à recharger
g.save("frwiki_graph.graphml")

# Rechargement ultérieur
g = ig.load("frwiki_graph.graphml")