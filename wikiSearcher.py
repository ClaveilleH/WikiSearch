import requests
import json
import os
from collections import deque

"""
Wikipedia Path Finder
Given two Wikipedia article titles, find the shortest path of links between them.
Pour l'instant le cache est stocké dans un fichier JSON local, mais on peut envisager d'utiliser une base de données pour de meilleures performances et une plus grande échelle.
"""
link_seen = 0
paths = {}
DESACTIVATE_DATES = False
DESACTIVATE_DATES = True 

def get_links(title):

    URL = "https://fr.wikipedia.org/w/api.php"
    links = []
    params = {
        "action": "query",
        "prop": "links",
        "titles": title,
        "pllimit": "max",
        "plnamespace": 0,  # limite aux articles
        "format": "json"
    }

    headers = {
        "User-Agent": "MyApp/1.0 (https://example.com)"
    }

    r = requests.get(URL, params=params, headers=headers)

    if r.status_code != 200:
        print(f"Error fetching {title}: {r.status_code}")
        return []

    data = r.json()
    pages = data["query"]["pages"]
    for page in pages.values():
        if "links" in page:
            for link in page["links"]:
                if DESACTIVATE_DATES and link["title"].isdigit():
                    continue
                if DESACTIVATE_DATES and link["title"][:4].isdigit():
                    continue
                links.append(link["title"].replace(" ", "_"))
    # print(f"Fetched {len(links)} links for {title}")

    return links


def search(start, end):
    print(f"Searching path from '{start}' to '{end}'...")
    if start == end:
        return [start]
    visited = set()
    front = {start: None}
    queue = deque([start])

    global paths
    paths = {start: [start]}

    while queue:
        print(f"Exploring {len(queue)} nodes...")
        current = queue.popleft()
        visited.add(current)

        links = get_links(current)
        print(f"Found {len(links)} links for '{current}'")
        if end in links:
            return paths[current] + [end]
        for link in links:
            if link == end:
                return paths[link]
            if link not in visited and link not in front:
                front[link] = current
                paths[link] = paths[current] + [link]
                queue.append(link)
            

def search_bidirectional(start, end):
    """Bidirectional BFS for Wikipedia path finding."""
    if start == end:
        return [start]

    front = {start: None}
    back = {end: None}
    queue_front = deque([start])
    queue_back = deque([end])

    global link_seen
    global paths
    paths = {start: [start], end: [end]}

    while queue_front and queue_back:
        if queue_front:
            current_front = queue_front.popleft()
            links_front = get_links(current_front)
            for link in links_front:
                if link in back:
                    return paths[current_front] + paths[link][::-1]
                if link not in front:
                    front[link] = current_front
                    paths[link] = paths[current_front] + [link]
                    queue_front.append(link)
                    link_seen += 1

        if queue_back:
            current_back = queue_back.popleft()
            links_back = get_links(current_back)
            for link in links_back:
                if link in front:
                    return paths[link] + paths[current_back][::-1]
                if link not in back:
                    back[link] = current_back
                    paths[link] = paths[current_back] + [link]
                    queue_back.append(link)
                    link_seen += 1
    


if __name__ == "__main__":
    page1 = "Pelagia_noctiluca"
    page2 = "Nylon"
    page1 = "Musa_acuminata"
    page2 = "Emmanuel_Macron"
    # path = search("Crevette", "Seconde_Guerre_mondiale")
    # path = search_bidirectional("Crevette", "C_(langage)")
    # path = search_bidirectional("Pelagia_notiluca", "Nylon")
    # path = search_bidirectional("Crevette", "Nylon")
    # path = search_bidirectional("Pelagia_notiluca", "Crevette")
    import sys
    if len(sys.argv) >= 3:
        page1 = sys.argv[1]
        page2 = sys.argv[2]
    path = search_bidirectional(page1, page2)
    # path = search("Python_(langage)", "C_(langage)")
    # print(" -> ".join(path) if path else "No path found")
    # path = search("Python_(langage)", "1989")
    
    if path:
        print("Path found in {} links (searched {} links):".format(len(path)-1, link_seen))
        print(" -> ".join(path))
    else:        
        print("No path found")
        # on affiche les url pour verifier
        print("Searched links:")
        print("http://fr.wikipedia.org/wiki/" + page1)

    # print(paths)