import igraph as ig
import csv

print("Chargement des titres...")
id_to_title = {}
with open("edges_titles.csv", "r") as f:
    reader = csv.reader(f)
    next(reader)  # skip header
    for row in reader:
        id_to_title[int(row[0])] = row[1]

print(f"{len(id_to_title)} pages")

print("Chargement des arêtes...")
edges = []
node_ids = set()
with open("edges.csv", "r") as f:
    reader = csv.reader(f)
    next(reader)
    for row in reader:
        s, t = int(row[0]), int(row[1])
        edges.append((s, t))
        node_ids.add(s)
        node_ids.add(t)

print(f"{len(edges)} liens")

print("Construction du graphe...")
all_ids = sorted(node_ids)
id_to_vertex = {pid: idx for idx, pid in enumerate(all_ids)}

g = ig.Graph(directed=True)
g.add_vertices(len(all_ids))
g.vs["name"] = [id_to_title.get(i, str(i)) for i in all_ids]

edge_list = [(id_to_vertex[s], id_to_vertex[t]) for s, t in edges]
g.add_edges(edge_list)
print(g.summary())

g.save("frwiki_graph.graphml")
print("Sauvegardé → frwiki_graph.graphml")