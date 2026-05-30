#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/SAX2.h>

#define HASH_SIZE (1 << 23)  // 8M buckets
#define MAX_TITLE 512

// Hash table : title -> new_id
typedef struct Entry {
    char *title;
    int new_id;
    struct Entry *next;
} Entry;

static Entry *table[HASH_SIZE];
static int total_pages = 0;

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s) h = h * 33 ^ (unsigned char)*s++;
    return h & (HASH_SIZE - 1);
}

static void insert(const char *title, int new_id) {
    unsigned int h = hash_str(title);
    Entry *e = malloc(sizeof(Entry));
    e->title = strdup(title);
    e->new_id = new_id;
    e->next = table[h];
    table[h] = e;
}

static int lookup(const char *title) {
    unsigned int h = hash_str(title);
    Entry *e = table[h];
    while (e) {
        if (strcmp(e->title, title) == 0) return e->new_id;
        e = e->next;
    }
    return -1;
}

// Charger pages_ns0.output -> hash table + id_map.output
static void load_pages(const char *path, FILE *id_map) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }

    char line[MAX_TITLE + 32];
    while (fgets(line, sizeof(line), f)) {
        // format : (page_id,title)
        if (line[0] != '(') continue;
        char *comma = strchr(line, ',');
        if (!comma) continue;
        char *end = strrchr(line, ')');
        if (!end) continue;

        *comma = '\0';
        int page_id = atoi(line + 1);
        *end = '\0';
        char *title = comma + 1;

        int new_id = total_pages++;
        insert(title, new_id);
        fprintf(id_map, "(%d,%d,%s)\n", new_id, page_id, title);
    }
    fclose(f);
    fprintf(stderr, "Pages chargées : %d\n", total_pages);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s pages_ns0.output dump.xml.bz2\n", argv[0]);
        return 1;
    }

    FILE *id_map = fopen("id_map.output", "w");
    if (!id_map) { perror("id_map.output"); return 1; }

    load_pages(argv[1], id_map);
    fclose(id_map);

    return 0;
}

/*

gcc -O2 -Wall -o parse_wikixml parse_wikixml.c $(xml2-config --cflags --libs)
./parse_wikixml output/pages_ns0.output data/frwiki-latest-pages-articles.xml.bz2

*/