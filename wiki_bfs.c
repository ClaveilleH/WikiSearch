#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────
   Configuration
   ───────────────────────────────────────────── */
#define MAX_PAGES      5000000
#define MAX_LINKS      250000000
#define NAME_BUF_SIZE  256000000  /* pool de chars pour les noms (~256 Mo) */
#define HASH_SIZE      (1 << 23)  /* ~8M buckets, doit être puissance de 2 */
#define HASH_MASK      (HASH_SIZE - 1)

/* ─────────────────────────────────────────────
   Structures
   ───────────────────────────────────────────── */

/* Liste d'adjacence (liens sortants) */
typedef struct AdjNode {
    int dest;
    struct AdjNode *next;
} AdjNode;

/* Table des pages */
static int         page_count = 0;
static int        *page_ids;          /* page_ids[i]   = id réel wikipedia */
static char      **page_names;        /* page_names[i] = nom de la page     */
static AdjNode   **adj;               /* liste d'adjacence                  */

/* Pool de chaînes */
static char  name_buf[NAME_BUF_SIZE];
static int   name_buf_pos = 0;

/* Table de hachage nom → indice interne */
typedef struct HashEntry {
    const char      *name;
    int              idx;
    struct HashEntry *next;
} HashEntry;
static HashEntry *hash_name[HASH_SIZE];

/* Table de hachage id (int) → indice interne */
typedef struct IdEntry {
    int              id;
    int              idx;
    struct IdEntry  *next;
} IdEntry;
static IdEntry *hash_id[HASH_SIZE];

/* Pool de noeuds d'adjacence (alloué dynamiquement) */
static AdjNode *adj_pool     = NULL;
static int      adj_pool_pos = 0;

/* ─────────────────────────────────────────────
   Utilitaires
   ───────────────────────────────────────────── */

/* ── Hash sur les chaînes ─────────────────── */
static unsigned int hash_str(const char *s)
{
    unsigned int h = 5381;
    while (*s) h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h & HASH_MASK;
}

static char *pool_strdup(const char *s)
{
    int len = strlen(s) + 1;
    if (name_buf_pos + len > NAME_BUF_SIZE) {
        fprintf(stderr, "Erreur : pool de noms saturé\n");
        exit(1);
    }
    char *p = name_buf + name_buf_pos;
    memcpy(p, s, len);
    name_buf_pos += len;
    return p;
}

static void name_insert(const char *name, int idx)
{
    unsigned int h = hash_str(name);
    HashEntry *e = malloc(sizeof(HashEntry));
    e->name = name; e->idx = idx;
    e->next = hash_name[h];
    hash_name[h] = e;
}

static int name_find(const char *name)
{
    unsigned int h = hash_str(name);
    for (HashEntry *e = hash_name[h]; e; e = e->next)
        if (strcmp(e->name, name) == 0) return e->idx;
    return -1;
}

/* ── Hash sur les IDs entiers ─────────────── */
static unsigned int hash_int(int id)
{
    unsigned int u = (unsigned int)id;
    u = ((u >> 16) ^ u) * 0x45d9f3b;
    u = ((u >> 16) ^ u) * 0x45d9f3b;
    u = (u >> 16) ^ u;
    return u & HASH_MASK;
}

static void id_insert(int id, int idx)
{
    unsigned int h = hash_int(id);
    IdEntry *e = malloc(sizeof(IdEntry));
    e->id = id; e->idx = idx;
    e->next = hash_id[h];
    hash_id[h] = e;
}

static int id_find(int id)
{
    unsigned int h = hash_int(id);
    for (IdEntry *e = hash_id[h]; e; e = e->next)
        if (e->id == id) return e->idx;
    return -1;
}

/* Ajoute un arc src_idx → dest_idx */
static void add_edge(int src_idx, int dest_idx)
{
    if (adj_pool_pos >= MAX_LINKS) {
        fprintf(stderr, "Erreur : pool d'arcs saturé\n");
        exit(1);
    }
    AdjNode *node = &adj_pool[adj_pool_pos++];
    node->dest = dest_idx;
    node->next = adj[src_idx];
    adj[src_idx] = node;
}

/* ─────────────────────────────────────────────
   Chargement des fichiers
   Format attendu : (id,nom)  et  (id_src,id_dest)
   ───────────────────────────────────────────── */

static void load_pages(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f) { perror(filename); exit(1); }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* retire le \n */
        line[strcspn(line, "\r\n")] = '\0';

        /* format : (id,nom) */
        if (line[0] != '(') continue;
        char *p = line + 1;               /* saute '(' */
        char *comma = strchr(p, ',');
        if (!comma) continue;
        *comma = '\0';
        int id = atoi(p);
        char *name = comma + 1;
        /* retire ')' final */
        char *rp = strrchr(name, ')');
        if (rp) *rp = '\0';

        if (page_count >= MAX_PAGES) {
            fprintf(stderr, "Erreur : MAX_PAGES atteint\n");
            exit(1);
        }
        int idx = page_count++;
        page_ids[idx]   = id;
        page_names[idx] = pool_strdup(name);
        id_insert(id, idx);
        name_insert(page_names[idx], idx);
    }
    fclose(f);
    printf("Pages chargées : %d\n", page_count);
}

static void load_links(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f) { perror(filename); exit(1); }

    char line[128];
    int total = 0, skipped_src = 0, skipped_dst = 0, bad_fmt = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '(') { bad_fmt++; continue; }

        char *p = line + 1;
        char *comma = strchr(p, ',');
        if (!comma) { bad_fmt++; continue; }
        *comma = '\0';
        int id_src = atoi(p);

        char *q = comma + 1;
        char *rp = strchr(q, ')');
        if (!rp) { bad_fmt++; continue; }
        *rp = '\0';
        int id_dest = atoi(q);

        int si = id_find(id_src);
        int di = id_find(id_dest);
        if (si == -1) { skipped_src++; continue; }
        if (di == -1) { skipped_dst++; continue; }

        /* Le dump wiki stocke (page_liée, page_source) — on inverse */
        add_edge(di, si);
        total++;
    }
    fclose(f);
    printf("Liens chargés  : %d\n", total);
    printf("  ignorés (src inconnue) : %d\n", skipped_src);
    printf("  ignorés (dst inconnue) : %d\n", skipped_dst);
    printf("  format invalide        : %d\n", bad_fmt);
}

/* ─────────────────────────────────────────────
   BFS
   ───────────────────────────────────────────── */

static void bfs_and_print(int src_idx, int dst_idx)
{
    int *prev  = malloc(page_count * sizeof(int));
    char *vis  = calloc(page_count, 1);
    int  *queue = malloc(page_count * sizeof(int));
    if (!prev || !vis || !queue) { perror("malloc BFS"); exit(1); }

    for (int i = 0; i < page_count; i++) prev[i] = -1;

    int head = 0, tail = 0;
    vis[src_idx] = 1;
    queue[tail++] = src_idx;

    long visited = 0;
    while (head < tail) {
        int cur = queue[head++];
        visited++;
        if (cur == dst_idx) break;
        for (AdjNode *e = adj[cur]; e; e = e->next) {
            int nb = e->dest;
            if (nb < 0 || nb >= page_count) {
                fprintf(stderr, "BFS: indice hors bornes nb=%d\n", nb);
                continue;
            }
            if (!vis[nb]) {
                vis[nb]  = 1;
                prev[nb] = cur;
                if (tail >= page_count) {
                    fprintf(stderr, "BFS: queue saturée à tail=%d\n", tail);
                    exit(1);
                }
                queue[tail++] = nb;
            }
        }
    }
    printf("Nœuds visités par le BFS : %ld / %d\n", visited, page_count);

    if (!vis[dst_idx]) {
        printf("Aucun chemin trouvé entre « %s » et « %s ».\n",
               page_names[src_idx], page_names[dst_idx]);
    } else {
        /* Reconstitue le chemin (heap, pas stack) */
        int *path = malloc(page_count * sizeof(int));
        if (!path) { perror("malloc path"); exit(1); }
        int len = 0;
        for (int cur = dst_idx; cur != -1; cur = prev[cur])
            path[len++] = cur;

        printf("\nChemin trouvé (%d saut%s) :\n", len - 1, len - 1 > 1 ? "s" : "");
        for (int i = len - 1; i >= 0; i--)
            printf("  [%d] %s\n", page_ids[path[i]], page_names[path[i]]);
        free(path);
    }

    free(prev); free(vis); free(queue);
}

/* ─────────────────────────────────────────────
   Main
   ───────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr,
            "Usage : %s <pages.csv> <liens.csv> \"Page source\" \"Page destination\"\n",
            argv[0]);
        return 1;
    }

    const char *pages_file = argv[1];
    const char *links_file = argv[2];
    const char *src_name   = argv[3];
    const char *dst_name   = argv[4];

    /* Allocations globales */
    page_ids   = malloc(MAX_PAGES * sizeof(int));
    page_names = malloc(MAX_PAGES * sizeof(char *));
    adj        = calloc(MAX_PAGES, sizeof(AdjNode *));
    adj_pool   = malloc((size_t)MAX_LINKS * sizeof(AdjNode));
    if (!page_ids || !page_names || !adj || !adj_pool) {
        perror("malloc"); return 1;
    }
    memset(hash_name, 0, sizeof(hash_name));
    memset(hash_id,   0, sizeof(hash_id));

    /* Chargement */
    load_pages(pages_file);
    load_links(links_file);

    /* Résolution des noms */
    int src_idx = name_find(src_name);
    int dst_idx = name_find(dst_name);

    if (src_idx == -1) {
        fprintf(stderr, "Page source introuvable : « %s »\n", src_name);
        return 1;
    }
    if (dst_idx == -1) {
        fprintf(stderr, "Page destination introuvable : « %s »\n", dst_name);
        return 1;
    }

    /* Diagnostic : voisins directs de la source */
    int nb_voisins = 0;
    for (AdjNode *e = adj[src_idx]; e; e = e->next) nb_voisins++;
    printf("Voisins directs de « %s » : %d\n", src_name, nb_voisins);

    printf("Source      : [%d] %s\n", page_ids[src_idx], page_names[src_idx]);
    printf("Destination : [%d] %s\n", page_ids[dst_idx], page_names[dst_idx]);
    putchar('\n');

    bfs_and_print(src_idx, dst_idx);

    return 0;
}
// gcc -O2 -o wiki_bfs wiki_bfs.c
// ./wiki_bfs pages_ns0.txt liens_ns0.txt "Philosophie" "Mathématiques"
