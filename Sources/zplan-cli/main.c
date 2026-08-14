/* zplan-cli — command-line front end, drop-in-ish for zplan.exe
 * usage: zplan [-s] [profile.dat]
 *   -s  save end-of-dive tissues to tissue.dat (like the original)
 */
#include "czplan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATM 1.01325   /* original tissue.dat values are in atmospheres */

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b) { size_t rd = fread(b, 1, (size_t)n, f); b[rd] = 0; }
    fclose(f);
    return b;
}

static void load_tissues(const char *path, zp_config *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    int idx = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "CNS:", 4)) { cfg->init_cns_pct = atof(line+4); continue; }
        double v = atof(line);
        if (idx < ZP_COMPARTMENTS) cfg->init_pn2[idx] = v * ATM;
        else if (idx < 2*ZP_COMPARTMENTS) cfg->init_phe[idx-ZP_COMPARTMENTS] = v * ATM;
        idx++;
    }
    fclose(f);
    if (idx >= 2*ZP_COMPARTMENTS) cfg->have_initial_tissues = true;
    else fprintf(stderr, "tissue file %s: wrong entry count, ignored\n", path);
}

static void save_tissues(const char *path, const zp_result *r) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "CNS:%f\n", r->end_cns_pct);
    for (int i = 0; i < ZP_COMPARTMENTS; i++) fprintf(f, "%f\n", r->end_pn2[i] / ATM);
    for (int i = 0; i < ZP_COMPARTMENTS; i++) fprintf(f, "%f\n", r->end_phe[i] / ATM);
    fclose(f);
    printf("Tissues saved to %s\n", path);
}

int main(int argc, char **argv) {
    const char *profile = "profile.dat";
    int save = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s")) save = 1;
        else profile = argv[i];
    }
    char *text = slurp(profile);
    if (!text) { fprintf(stderr, "cannot read %s\n", profile); return 1; }

    zp_config cfg; char err[256] = "";
    if (zp_parse_profile(text, &cfg, err, sizeof err)) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }
    if (err[0]) fprintf(stderr, "note: %s\n", err);
    free(text);

    load_tissues("tissue.dat", &cfg);

    zp_result res;
    if (zp_plan(&cfg, &res)) { fprintf(stderr, "planning failed\n"); return 1; }

    char report[16384];
    zp_report(&cfg, &res, report, sizeof report);
    fputs(report, stdout);

    FILE *out = fopen("plan.out", "w");
    if (out) { fputs(report, out); fclose(out); }

    if (save) save_tissues("tissue.dat", &res);
    return 0;
}
