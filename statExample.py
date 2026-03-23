# PageRank
pr = g.pagerank()
top = sorted(zip(g.vs["name"], pr), key=lambda x: -x[1])[:10]

# Degré sortant (pages qui font le plus de liens)
out_deg = g.outdegree()

# Composantes fortement connexes
components = g.clusters(mode="strong")

# Plus court chemin entre deux articles
path = g.get_shortest_paths("Python_(langage)", to="Intelligence_artificielle")