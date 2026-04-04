import re
import igraph as ig

def parse_page(filepath):
    id_to_title = {}
    pattern = re.compile(rb"\((\d+),(\d+),'((?:[^'\\]|\\.)*)'")
    
    with open(filepath, "rb") as f:
        buffer = b""
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            buffer += chunk
            lines = buffer.split(b"\n")
            buffer = lines.pop()
            for line in lines:
                if not line.startswith(b"INSERT INTO"):
                    continue
                for m in pattern.finditer(line):
                    if int(m.group(2)) != 0:
                        continue
                    page_id = int(m.group(1))
                    title = m.group(3).decode("utf-8", errors="ignore").replace("\\'", "'")
                    id_to_title[page_id] = title
        if buffer.startswith(b"INSERT INTO"):
            for m in pattern.finditer(buffer):
                if int(m.group(2)) != 0:
                    continue
                page_id = int(m.group(1))
                title = m.group(3).decode("utf-8", errors="ignore").replace("\\'", "'")
                id_to_title[page_id] = title

    return id_to_title

def parse_pagelinks(filepath, id_to_title):
    edges = []
    # format : (source_id, namespace, target_id)
    pattern = re.compile(rb"\((\d+),0,(\d+)\)")
    
    with open(filepath, "rb") as f:
        buffer = b""
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            buffer += chunk
            lines = buffer.split(b"\n")
            buffer = lines.pop()
            for line in lines:
                if not line.startswith(b"INSERT INTO"):
                    continue
                for m in pattern.finditer(line):
                    source_id = int(m.group(1))
                    target_id = int(m.group(2))
                    if source_id in id_to_title and target_id in id_to_title:
                        edges.append((source_id, target_id))
        if buffer.startswith(b"INSERT INTO"):
            for m in pattern.finditer(buffer):
                source_id = int(m.group(1))
                target_id = int(m.group(2))
                if source_id in id_to_title and target_id in id_to_title:
                    edges.append((source_id, target_id))

    return edges

# --- Main ---
print("Parsing pages...")
id_to_title = parse_page("frwiki-latest-page.sql")
print(f"{len(id_to_title)} pages chargées")

print("Parsing liens...")
edges = parse_pagelinks("frwiki-latest-pagelinks.sql", id_to_title)
print(f"{len(edges)} liens chargés")

print("Construction du graphe...")
g = ig.Graph(directed=True)
all_ids = sorted(id_to_title.keys())
g.add_vertices(len(all_ids))
g.vs["name"] = [id_to_title[i] for i in all_ids]

id_to_vertex = {page_id: idx for idx, page_id in enumerate(all_ids)}
edge_list = [
    (id_to_vertex[s], id_to_vertex[t])
    for s, t in edges
    if s in id_to_vertex and t in id_to_vertex
]
g.add_edges(edge_list)
print(g.summary())

print("Sauvegarde...")
g.save("frwiki_graph.graphml")
print("Terminé → frwiki_graph.graphml")