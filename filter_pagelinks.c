/*
 * filter_pagelinks.c  –  version bitset
 * --------------------------------------
 * Filtre un dump SQL MariaDB `pagelinks` (Wikipedia) pour ne garder
 * que les liens entre pages du namespace 0.
 *
 * Compilation :
 *   gcc -O2 -o filter_pagelinks filter_pagelinks.c
 *
 * Usage :
 *   ./filter_pagelinks ids.txt pagelinks.sql output.txt
 *
 * Sortie : une paire par ligne →  (id_origine,id_dest)
 *
 * Stratégie :
 *   - Bitset de (MAX_ID / 64) uint64_t  →  ~2 Mo, tient en cache L2/L3
 *   - Parser manuel des lignes INSERT, sans regex, sans malloc par tuple
 *   - I/O bufférisées (8 Mo en lecture, buffer texte 4 Mo en écriture)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Paramètres                                                           */
/* ------------------------------------------------------------------ */

/* ID maximum connu + marge de sécurité                               */
#define MAX_ID          18000000UL

/* Nombre de mots de 64 bits nécessaires                              */
#define BITSET_WORDS    ((MAX_ID + 63) / 64)

/* Taille des buffers I/O                                             */
#define IO_BUF_SIZE     (8 * 1024 * 1024)   /* 8 Mo lecture SQL       */
#define OUT_BUF_SIZE    (4 * 1024 * 1024)   /* 4 Mo écriture résultat */

/* Fréquence d'affichage de la progression (en tuples)               */
#define REPORT_EVERY    10000000UL

/* ------------------------------------------------------------------ */
/* Bitset                                                              */
/* ------------------------------------------------------------------ */

static uint64_t bitset[BITSET_WORDS];   /* ~2.1 Mo sur la pile globale */

static inline void bitset_set(unsigned long id)
{
    bitset[id >> 6] |= (uint64_t)1 << (id & 63);
}

static inline int bitset_test(unsigned long id)
{
    if (id >= MAX_ID) return 0;
    return (bitset[id >> 6] >> (id & 63)) & 1;
}

/* ------------------------------------------------------------------ */
/* Lecture rapide d'un entier non-signé                                */
/* Retourne le pointeur après le dernier chiffre lu.                  */
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
/* Chargement des IDs valides dans le bitset                           */
/* ------------------------------------------------------------------ */
static long load_ids(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }

    char line[32];
    long count = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long id = strtoul(line, NULL, 10);
        if (id > 0 && id < MAX_ID) {
            bitset_set(id);
            count++;
        }
    }
    fclose(f);
    return count;
}

/* ------------------------------------------------------------------ */
/* Traitement d'une ligne INSERT                                        */
/* ------------------------------------------------------------------ */
static void process_insert(const char *line,
                            char       *out_buf,
                            size_t     *out_pos,
                            FILE       *out_fp,
                            long       *written)
{
    const char *p = strstr(line, "VALUES ");
    if (!p) return;
    p += 7;

    unsigned long pl_from, pl_from_ns, pl_target_id;

    while (*p) {
        /* cherche '(' */
        while (*p && *p != '(') p++;
        if (!*p) break;
        p++;

        p = parse_uint(p, &pl_from);
        if (*p != ',') break;
        p++;

        p = parse_uint(p, &pl_from_ns);
        if (*p != ',') break;
        p++;

        p = parse_uint(p, &pl_target_id);

        /* Filtre : namespace 0, les deux IDs dans le bitset */
        if (pl_from_ns == 0
                && bitset_test(pl_from)
                && bitset_test(pl_target_id))
        {
            /* itoa sans snprintf : écrit les chiffres en sens inverse */
            /* puis inverse, plus rapide que snprintf sur 300M appels  */
            char tmp[64];
            char digits[20];
            int  rlen;
            unsigned long v;
            char *t = tmp;

            *t++ = '(';

            /* pl_from */
            v = pl_from; rlen = 0;
            if (v == 0) { digits[rlen++] = '0'; }
            else { while (v) { digits[rlen++] = '0' + (v % 10); v /= 10; } }
            for (int i = rlen - 1; i >= 0; i--) *t++ = digits[i];

            *t++ = ',';

            /* pl_target_id */
            v = pl_target_id; rlen = 0;
            if (v == 0) { digits[rlen++] = '0'; }
            else { while (v) { digits[rlen++] = '0' + (v % 10); v /= 10; } }
            for (int i = rlen - 1; i >= 0; i--) *t++ = digits[i];

            *t++ = ')';
            *t++ = '\n';

            size_t len = (size_t)(t - tmp);

            if (*out_pos + len > OUT_BUF_SIZE) {
                fwrite(out_buf, 1, *out_pos, out_fp);
                *out_pos = 0;
            }
            memcpy(out_buf + *out_pos, tmp, len);
            *out_pos += len;
            (*written)++;
        }

        /* avance jusqu'après ')' */
        while (*p && *p != ')') p++;
        if (*p == ')') p++;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s ids.txt pagelinks.sql output.txt\n", argv[0]);
        return 1;
    }

    /* --- 1. Chargement des IDs --- */
    printf("[1/3] Chargement des IDs depuis '%s' …\n", argv[1]);
    fflush(stdout);
    long id_count = load_ids(argv[1]);
    printf("      %ld IDs chargés  (bitset ~%.1f Mo).\n",
           id_count, BITSET_WORDS * 8.0 / (1024 * 1024));

    /* --- 2. Ouverture des fichiers --- */
    FILE *sql_fp = fopen(argv[2], "r");
    if (!sql_fp) { perror(argv[2]); return 1; }

    FILE *out_fp = fopen(argv[3], "w");
    if (!out_fp) { perror(argv[3]); return 1; }

    char *sql_buf = malloc(IO_BUF_SIZE);
    char *out_buf = malloc(OUT_BUF_SIZE);
    if (!sql_buf || !out_buf) { fprintf(stderr, "malloc failed\n"); return 1; }
    setvbuf(sql_fp, sql_buf, _IOFBF, IO_BUF_SIZE);

    /* Les lignes INSERT peuvent faire plusieurs dizaines de Mo        */
    size_t line_cap = 64 * 1024 * 1024;
    char  *line_buf = malloc(line_cap);
    if (!line_buf) { fprintf(stderr, "malloc line_buf failed\n"); return 1; }

    /* --- 3. Parsing --- */
    printf("[2/3] Lecture de '%s' …\n", argv[2]);
    fflush(stdout);

    long   written = 0;
    long   total   = 0;
    size_t out_pos = 0;
    clock_t t0     = clock();
    clock_t last   = t0;

    while (fgets(line_buf, (int)line_cap, sql_fp)) {
        if (strncmp(line_buf, "INSERT INTO", 11) != 0)
            continue;

        /* Compte les tuples (approximatif : nombre de '(')            */
        const char *s = line_buf;
        long line_tuples = 0;
        while ((s = strchr(s, '(')) != NULL) { line_tuples++; s++; }
        total += line_tuples;

        process_insert(line_buf, out_buf, &out_pos, out_fp, &written);

        clock_t now = clock();
        if ((double)(now - last) / CLOCKS_PER_SEC >= 5.0) {
            double elapsed = (double)(now - t0) / CLOCKS_PER_SEC;
            printf("      %.0fM tuples | %ld conservés | %.1f M/s | %.0fs\n",
                   total / 1e6, written,
                   total / elapsed / 1e6, elapsed);
            fflush(stdout);
            last = now;
        }
    }

    /* Flush final */
    if (out_pos > 0)
        fwrite(out_buf, 1, out_pos, out_fp);

    fclose(sql_fp);
    fclose(out_fp);
    free(sql_buf);
    free(out_buf);
    free(line_buf);

    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("[3/3] Terminé en %.1fs — %.2f M tuples/s\n",
           elapsed, total / elapsed / 1e6);
    printf("      ✓ %ld paires conservées\n", written);
    printf("      ✗ %ld tuples filtrés\n", total - written);

    return 0;
}

// gcc -O2 -o filter_pagelinks filter_pagelinks.c
// ./filter_pagelinks ids.txt pagelinks.sql output.txt

// ./filter_pagelinks frwiki-page-namespace0-ids.txt frwiki-latest-pagelinks.sql output.txt