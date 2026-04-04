/*
 * build_lt_map.c
 * --------------
 * Jointure linktarget_ns0.output x pages_ns0.output sur le titre.
 * Produit lt_map.output : une ligne par entrée → (lt_id,page_id)
 *
 * Compilation :
 *   gcc -O2 -o build_lt_map build_lt_map.c
 *
 * Usage :
 *   ./build_lt_map pages_ns0.output linktarget_ns0.output lt_map.output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Paramètres                                                           */
/* ------------------------------------------------------------------ */
#define MAX_PAGES       5000000
#define NAME_BUF_SIZE   512000000   /* pool de chars pour les titres ~512 Mo */
#define HASH_SIZE       (1 << 23)   /* ~8M buckets, puissance de 2           */
#define HASH_MASK       (HASH_SIZE - 1)
#define IO_BUF_SIZE     (8 * 1024 * 1024)
#define OUT_BUF_SIZE    (4 * 1024 * 1024)
#define LINE_CAP        4096

/* ------------------------------------------------------------------ */
/* Hash table title → page_id                                          */
/* ------------------------------------------------------------------ */
typedef struct Entry {
    const char   *title;
    int           page_id;
    struct Entry *next;
} Entry;

static Entry  *hash_table[HASH_SIZE];
static Entry   entry_pool[MAX_PAGES];
static int     entry_pool_pos = 0;

static char    name_buf[NAME_BUF_SIZE];
static int     name_buf_pos = 0;

/* ------------------------------------------------------------------ */
/* Utilitaires                                                          */
/* ------------------------------------------------------------------ */
static unsigned int hash_str(const char *s)
{
    unsigned int h = 5381;
    while (*s) h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h & HASH_MASK;
}

static char *pool_strdup(const char *s, int len)
{
    if (name_buf_pos + len + 1 > NAME_BUF_SIZE) {
        fprintf(stderr, "Erreur : pool de noms saturé\n");
        exit(1);
    }
    char *p = name_buf + name_buf_pos;
    memcpy(p, s, len);
    p[len] = '\0';
    name_buf_pos += len + 1;
    return p;
}

static void ht_insert(const char *title, int page_id)
{
    if (entry_pool_pos >= MAX_PAGES) {
        fprintf(stderr, "Erreur : pool d'entrées saturé\n");
        exit(1);
    }
    unsigned int h = hash_str(title);
    Entry *e = &entry_pool[entry_pool_pos++];
    e->title   = title;
    e->page_id = page_id;
    e->next    = hash_table[h];
    hash_table[h] = e;
}

static int ht_find(const char *title)
{
    unsigned int h = hash_str(title);
    for (Entry *e = hash_table[h]; e; e = e->next)
        if (strcmp(e->title, title) == 0)
            return e->page_id;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Parsing d'une ligne format (id,title)                               */
/* title peut contenir des virgules → on prend la première virgule     */
/* pour l'id, et on cherche le ')' final pour clore le titre           */
/* ------------------------------------------------------------------ */
static int parse_line(char *line, long *out_id, char **out_title)
{
    if (line[0] != '(') return 0;
    char *p = line + 1;

    /* id : digits jusqu'à la première virgule */
    char *comma = strchr(p, ',');
    if (!comma) return 0;
    *comma = '\0';
    *out_id = atol(p);

    /* title : du char après la virgule jusqu'au dernier ')' */
    char *title = comma + 1;
    char *rp = strrchr(title, ')');
    if (!rp) return 0;
    *rp = '\0';

    *out_title = title;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr,
            "Usage: %s pages_ns0.output linktarget_ns0.output lt_map.output\n",
            argv[0]);
        return 1;
    }

    char *io_buf  = malloc(IO_BUF_SIZE);
    char *out_buf = malloc(OUT_BUF_SIZE);
    char *line    = malloc(LINE_CAP);
    if (!io_buf || !out_buf || !line) { perror("malloc"); return 1; }

    /* ── 1. Charge pages_ns0.output → hash table title → page_id ── */
    printf("[1/3] Chargement de '%s'...\n", argv[1]);
    fflush(stdout);

    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    setvbuf(f, io_buf, _IOFBF, IO_BUF_SIZE);

    long  page_count = 0;
    long  out_id;
    char *title;

    while (fgets(line, LINE_CAP, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_line(line, &out_id, &title)) continue;
        int   tlen = strlen(title);
        char *stored = pool_strdup(title, tlen);
        ht_insert(stored, (int)out_id);
        page_count++;
    }
    fclose(f);
    printf("  %ld pages chargées\n", page_count);

    /* ── 2. Lit linktarget_ns0.output, résout, écrit lt_map.output ── */
    printf("[2/3] Traitement de '%s'...\n", argv[2]);
    fflush(stdout);

    FILE *lt_f = fopen(argv[2], "r");
    if (!lt_f) { perror(argv[2]); return 1; }
    setvbuf(lt_f, io_buf, _IOFBF, IO_BUF_SIZE);

    FILE *out_f = fopen(argv[3], "w");
    if (!out_f) { perror(argv[3]); return 1; }

    size_t out_pos  = 0;
    long   resolved = 0;
    long   missed   = 0;

    while (fgets(line, LINE_CAP, lt_f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_line(line, &out_id, &title)) continue;

        int page_id = ht_find(title);
        if (page_id == -1) { missed++; continue; }

        /* Écrit (lt_id,page_id)\n dans le buffer de sortie */
        char tmp[64];
        int  len = snprintf(tmp, sizeof(tmp), "(%ld,%d)\n", out_id, page_id);

        if (out_pos + (size_t)len > OUT_BUF_SIZE) {
            fwrite(out_buf, 1, out_pos, out_f);
            out_pos = 0;
        }
        memcpy(out_buf + out_pos, tmp, len);
        out_pos += len;
        resolved++;

        if (resolved % 5000000 == 0) {
            printf("  %ld résolus...\n", resolved);
            fflush(stdout);
        }
    }

    /* Flush final */
    if (out_pos > 0) fwrite(out_buf, 1, out_pos, out_f);

    fclose(lt_f);
    fclose(out_f);

    printf("[3/3] Terminé\n");
    printf("  ✓ %ld lt_id résolus\n", resolved);
    printf("  ✗ %ld lt_id sans page correspondante (liens rouges)\n", missed);

    free(io_buf); free(out_buf); free(line);
    return 0;
}



/*
gcc -O2 -o build_lt_map build_lt_map.c
./build_lt_map pages_ns0.output linktarget_ns0.output lt_map.output
*/