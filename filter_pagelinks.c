/*
 * filter_pagelinks.c  –  version lt_map
 * ---------------------------------------
 * Filtre un dump SQL MariaDB `pagelinks` (Wikipedia) pour ne garder
 * que les liens entre pages du namespace 0, en résolvant pl_target_id
 * via lt_map.output (lt_id → page_id).
 *
 * Compilation :
 *   gcc -O2 -o filter_pagelinks filter_pagelinks.c
 *
 * Usage :
 *   ./filter_pagelinks pages_ns0.output lt_map.output pagelinks.sql liens_ns0.output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Paramètres                                                           */
/* ------------------------------------------------------------------ */
#define MAX_PAGE_ID     18000000UL
#define MAX_LT_ID       32000000UL

#define BITSET_WORDS    ((MAX_PAGE_ID + 63) / 64)

#define IO_BUF_SIZE     (8 * 1024 * 1024)
#define OUT_BUF_SIZE    (4 * 1024 * 1024)
#define LINE_CAP        (64 * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* Bitset pour pl_from (page_id valides)                               */
/* ------------------------------------------------------------------ */
static uint64_t bitset[BITSET_WORDS];

static inline void bitset_set(unsigned long id)
{
    if (id < MAX_PAGE_ID)
        bitset[id >> 6] |= (uint64_t)1 << (id & 63);
}

static inline int bitset_test(unsigned long id)
{
    if (id >= MAX_PAGE_ID) return 0;
    return (bitset[id >> 6] >> (id & 63)) & 1;
}

/* ------------------------------------------------------------------ */
/* Tableau direct lt_id → page_id (0 = non résolu)                    */
/* ------------------------------------------------------------------ */
static int *lt_map = NULL;

/* ------------------------------------------------------------------ */
/* Lecture rapide d'un entier non-signé                                */
/* ------------------------------------------------------------------ */
static inline const char *parse_uint(const char *p, unsigned long *out)
{
    unsigned long v = 0;
    while ((unsigned char)(*p - '0') <= 9)
        v = v * 10 + (unsigned char)(*p++ - '0');
    *out = v;
    return p;
}

/* ------------------------------------------------------------------ */
/* Chargement pages_ns0.output → bitset                                */
/* ------------------------------------------------------------------ */
static long load_pages(const char *path, char *io_buf)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    setvbuf(f, io_buf, _IOFBF, IO_BUF_SIZE);

    char line[256];
    long count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '(') continue;
        unsigned long id;
        parse_uint(line + 1, &id);
        bitset_set(id);
        count++;
    }
    fclose(f);
    return count;
}

/* ------------------------------------------------------------------ */
/* Chargement lt_map.output → tableau lt_id → page_id                 */
/* ------------------------------------------------------------------ */
static long load_lt_map(const char *path, char *io_buf)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    setvbuf(f, io_buf, _IOFBF, IO_BUF_SIZE);

    char line[64];
    long count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '(') continue;
        const char *p = line + 1;
        unsigned long lt_id, page_id;
        p = parse_uint(p, &lt_id);
        if (*p != ',') continue;
        p++;
        parse_uint(p, &page_id);
        if (lt_id < MAX_LT_ID && page_id > 0) {
            lt_map[lt_id] = (int)page_id;
            count++;
        }
    }
    fclose(f);
    return count;
}

/* ------------------------------------------------------------------ */
/* Traitement d'une ligne INSERT                                        */
/* ------------------------------------------------------------------ */
static size_t g_out_pos = 0;  /* position globale dans out_buf */

static void process_insert(const char *line,
                            char       *out_buf,
                            FILE       *out_fp,
                            long       *written,
                            long       *skipped)
{
    const char *p = strstr(line, "VALUES ");
    if (!p) return;
    p += 7;

    unsigned long pl_from, pl_from_ns, pl_target_id;

    while (*p) {
        while (*p && *p != '(') p++;
        if (!*p) break;
        p++;

        p = parse_uint(p, &pl_from);
        if (*p != ',') { break; } p++;

        p = parse_uint(p, &pl_from_ns);
        if (*p != ',') { break; } p++;

        p = parse_uint(p, &pl_target_id);

        if (pl_from_ns == 0
                && bitset_test(pl_from)
                && pl_target_id < MAX_LT_ID
                && lt_map[pl_target_id] != 0)
        {
            int dst_id = lt_map[pl_target_id];

            char tmp[64];
            char digits[20];
            int  rlen;
            unsigned long v;
            char *t = tmp;

            *t++ = '(';
            v = pl_from; rlen = 0;
            if (v == 0) { digits[rlen++] = '0'; }
            else { while (v) { digits[rlen++] = '0' + (v % 10); v /= 10; } }
            for (int i = rlen - 1; i >= 0; i--) *t++ = digits[i];

            *t++ = ',';
            v = (unsigned long)dst_id; rlen = 0;
            if (v == 0) { digits[rlen++] = '0'; }
            else { while (v) { digits[rlen++] = '0' + (v % 10); v /= 10; } }
            for (int i = rlen - 1; i >= 0; i--) *t++ = digits[i];

            *t++ = ')'; *t++ = '\n';

            size_t len = (size_t)(t - tmp);
            if (g_out_pos + len > OUT_BUF_SIZE) {
                fwrite(out_buf, 1, g_out_pos, out_fp);
                g_out_pos = 0;
            }
            memcpy(out_buf + g_out_pos, tmp, len);
            g_out_pos += len;
            (*written)++;
        } else {
            (*skipped)++;
        }

        while (*p && *p != ')') p++;
        if (*p == ')') p++;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr,
            "Usage: %s pages_ns0.output lt_map.output pagelinks.sql liens_ns0.output\n",
            argv[0]);
        return 1;
    }

    char *io_buf  = malloc(IO_BUF_SIZE);
    char *out_buf = malloc(OUT_BUF_SIZE);
    char *line    = malloc(LINE_CAP);
    lt_map        = calloc(MAX_LT_ID, sizeof(int));
    if (!io_buf || !out_buf || !line || !lt_map) { perror("malloc"); return 1; }

    /* 1. Bitset des page_id valides */
    printf("[1/4] Chargement des pages '%s'...\n", argv[1]); fflush(stdout);
    long page_count = load_pages(argv[1], io_buf);
    printf("      %ld pages chargées (bitset %.1f Mo)\n",
           page_count, BITSET_WORDS * 8.0 / (1024 * 1024));

    /* 2. Tableau lt_id → page_id */
    printf("[2/4] Chargement du lt_map '%s'...\n", argv[2]); fflush(stdout);
    long lt_count = load_lt_map(argv[2], io_buf);
    printf("      %ld entrées lt_map chargées (tableau %.0f Mo)\n",
           lt_count, MAX_LT_ID * sizeof(int) / (1024.0 * 1024));

    /* 3. Ouverture fichiers SQL et sortie */
    FILE *sql_fp = fopen(argv[3], "r");
    if (!sql_fp) { perror(argv[3]); return 1; }
    FILE *out_fp = fopen(argv[4], "w");
    if (!out_fp) { perror(argv[4]); return 1; }
    setvbuf(sql_fp, io_buf, _IOFBF, IO_BUF_SIZE);

    /* 4. Parsing */
    printf("[3/4] Lecture de '%s'...\n", argv[3]); fflush(stdout);

    long    written = 0, skipped = 0, total = 0;
    clock_t t0 = clock(), last = t0;

    while (fgets(line, LINE_CAP, sql_fp)) {
        if (strncmp(line, "INSERT INTO", 11) != 0) continue;

        /* Compte approximatif des tuples */
        const char *s = line; long lt = 0;
        while ((s = strchr(s, '(')) != NULL) { lt++; s++; }
        total += lt;

        process_insert(line, out_buf, out_fp, &written, &skipped);

        clock_t now = clock();
        if ((double)(now - last) / CLOCKS_PER_SEC >= 5.0) {
            double elapsed = (double)(now - t0) / CLOCKS_PER_SEC;
            printf("      %.0fM tuples | %ld conservés | %.1f M/s | %.0fs\n",
                   total / 1e6, written, total / elapsed / 1e6, elapsed);
            fflush(stdout);
            last = now;
        }
    }

    /* Flush final */
    if (g_out_pos > 0) fwrite(out_buf, 1, g_out_pos, out_fp);
    fclose(sql_fp);
    fclose(out_fp);

    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("[4/4] Terminé en %.1fs — %.2f M tuples/s\n",
           elapsed, total / elapsed / 1e6);
    printf("      ✓ %ld paires conservées\n", written);
    printf("      ✗ %ld tuples filtrés\n",    skipped);

    free(io_buf); free(out_buf); free(line); free(lt_map);
    return 0;
}


/*
gcc -O2 -o filter_pagelinks filter_pagelinks.c
./filter_pagelinks pages_ns0.output lt_map.output frwiki-latest-pagelinks.sql liens_ns0.output
*/