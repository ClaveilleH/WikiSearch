#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/SAX2.h>

#define HASH_SIZE (1 << 23)
#define MAX_TITLE 512
#define TEXT_BUF_SIZE (1 << 22)  // 4 Mo par article

// --- Hash table ---
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

static void load_pages(const char *path, FILE *id_map) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[MAX_TITLE + 32];
    while (fgets(line, sizeof(line), f)) {
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

// --- Normalisation du titre ---
static void normalize_title(char *title) {
    // espaces -> underscores
    for (char *p = title; *p; p++)
        if (*p == ' ') *p = '_';
    // première lettre majuscule
    if (title[0] >= 'a' && title[0] <= 'z')
        title[0] -= 32;
}

// --- Parser wikicode : extrait les [[liens]] hors templates ---
static void parse_wikitext(const char *text, int src_id, FILE *out) {
    int depth = 0;  // profondeur dans {{...}}
    const char *p = text;

    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            depth++;
            p += 2;
            continue;
        }
        if (p[0] == '}' && p[1] == '}') {
            if (depth > 0) depth--;
            p += 2;
            continue;
        }
        if (depth == 0 && p[0] == '[' && p[1] == '[') {
            p += 2;
            // trouver la fin ]]
            const char *end = strstr(p, "]]");
            if (!end) break;

            // copier le contenu
            char link[MAX_TITLE];
            int len = end - p;
            if (len >= MAX_TITLE) { p = end + 2; continue; }
            memcpy(link, p, len);
            link[len] = '\0';

            // ignorer interwiki : contient ":"
            // sauf si c'est juste un ":" en début (lien vers catégorie etc.)
            char *colon = strchr(link, ':');
            if (colon) { p = end + 2; continue; }

            // tronquer au | (texte affiché)
            char *pipe = strchr(link, '|');
            if (pipe) *pipe = '\0';

            // tronquer au # (section)
            char *hash = strchr(link, '#');
            if (hash) *hash = '\0';

            // normaliser
            normalize_title(link);

            if (link[0] != '\0') {
                int dst_id = lookup(link);
                if (dst_id >= 0 && dst_id != src_id)
                    fprintf(out, "(%d,%d)\n", src_id, dst_id);
            }

            p = end + 2;
            continue;
        }
        p++;
    }
}

// --- État SAX ---
typedef struct {
    // balise courante
    int in_title, in_ns, in_id, in_text, in_redirect;

    // buffers
    char title[MAX_TITLE];
    char ns[16];
    char id_str[32];
    char *text;
    int text_len;

    // état page courante
    int page_ns;
    int page_id;
    int is_redirect;
    int src_new_id;

    // stats
    long pages_ok;
    long pages_skipped;

    FILE *out;
} SaxState;

static void on_start(void *ctx, const xmlChar *name, const xmlChar **attrs) {
    SaxState *s = ctx;
    if (strcmp((char*)name, "page") == 0) {
        s->title[0] = '\0';
        s->ns[0] = '\0';
        s->id_str[0] = '\0';
        s->text_len = 0;
        s->is_redirect = 0;
        s->page_id = -1;
        s->page_ns = -1;
        s->src_new_id = -1;
    }
    else if (strcmp((char*)name, "redirect") == 0) s->is_redirect = 1;
    else if (strcmp((char*)name, "ns") == 0)       s->in_ns = 1;
    else if (strcmp((char*)name, "id") == 0)       s->in_id = 1;
    else if (strcmp((char*)name, "title") == 0)    s->in_title = 1;
    else if (strcmp((char*)name, "text") == 0)     s->in_text = 1;
}

static void on_chars(void *ctx, const xmlChar *ch, int len) {
    SaxState *s = ctx;
    if (s->in_ns) {
        strncat(s->ns, (char*)ch, sizeof(s->ns) - strlen(s->ns) - 1);
    }
    else if (s->in_id && s->page_id == -1) {
        // on prend le premier <id> rencontré (= page_id, pas revision_id)
        char tmp[32] = {0};
        strncpy(tmp, (char*)ch, sizeof(tmp)-1);
        s->page_id = atoi(tmp);
        s->in_id = 0;
    }
    else if (s->in_title) {
        strncat(s->title, (char*)ch, sizeof(s->title) - strlen(s->title) - 1);
    }
    else if (s->in_text) {
        if (s->text_len + len < TEXT_BUF_SIZE - 1) {
            memcpy(s->text + s->text_len, ch, len);
            s->text_len += len;
        }
    }
}

static void on_end(void *ctx, const xmlChar *name) {
    SaxState *s = ctx;
    if (strcmp((char*)name, "ns") == 0) {
        s->page_ns = atoi(s->ns);
        s->in_ns = 0;
    }
    else if (strcmp((char*)name, "title") == 0) s->in_title = 0;
    else if (strcmp((char*)name, "text") == 0) {
        s->text[s->text_len] = '\0';
        s->in_text = 0;
    }
    else if (strcmp((char*)name, "page") == 0) {
        if (s->page_ns != 0 || s->is_redirect) {
            s->pages_skipped++;
            return;
        }
        // résoudre le title -> new_id
        char title_norm[MAX_TITLE];
        strncpy(title_norm, s->title, MAX_TITLE-1);
        normalize_title(title_norm);
        s->src_new_id = lookup(title_norm);
        if (s->src_new_id < 0) { s->pages_skipped++; return; }

        parse_wikitext(s->text, s->src_new_id, s->out);
        s->pages_ok++;

        if (s->pages_ok % 100000 == 0)
            fprintf(stderr, "  %ld pages traitées...\n", s->pages_ok);
    }
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

    FILE *out = fopen("liens_xml.output", "w");
    if (!out) { perror("liens_xml.output"); return 1; }

    // init état SAX
    SaxState state = {0};
    state.text = malloc(TEXT_BUF_SIZE);
    if (!state.text) { fprintf(stderr, "malloc failed\n"); return 1; }
    state.out = out;

    // callbacks SAX
    xmlSAXHandler handler = {0};
    handler.startElement = on_start;
    handler.characters   = on_chars;
    handler.endElement   = on_end;

    // ouvrir le bz2 via bzcat
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "bzcat %s", argv[2]);
    FILE *pipe = popen(cmd, "r");
    if (!pipe) { perror("popen"); return 1; }

    // feed libxml2 SAX en streaming
    char buf[65536];
    xmlParserCtxtPtr ctxt = NULL;
    size_t n;
    int first = 1;

    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) {
        if (first) {
            ctxt = xmlCreatePushParserCtxt(&handler, &state, buf, n, NULL);
            if (!ctxt) { fprintf(stderr, "xmlCreatePushParserCtxt failed\n"); return 1; }
            first = 0;
        } else {
            xmlParseChunk(ctxt, buf, n, 0);
        }
    }
    if (ctxt) {
        xmlParseChunk(ctxt, NULL, 0, 1);  // finalize
        xmlFreeParserCtxt(ctxt);
    }

    pclose(pipe);
    fclose(out);
    free(state.text);

    fprintf(stderr, "Terminé. Pages traitées : %ld, ignorées : %ld\n",
            state.pages_ok, state.pages_skipped);

    return 0;
}

/*

gcc -O2 -Wall -o parse_wikixml parse_wikixml.c $(xml2-config --cflags --libs)
./parse_wikixml output/pages_ns0.output data/frwiki-latest-pages-articles.xml.bz2

*/