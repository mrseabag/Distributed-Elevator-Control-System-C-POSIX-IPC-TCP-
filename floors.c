#include "floors.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static int parse_level(const char *s, int *is_basement, int *num) {
    if (s == NULL || s[0] == '\0') return -1;
    if (s[0] == 'B') {
        *is_basement = 1;
        const char *p = s+1;
        if (*p == '\0') return -1;
        // No leading zeros permitted
        if (p[0] == '0') return -1;
        int val = 0;
        for (; *p; ++p) {
            if (!isdigit((unsigned char)*p)) return -1;
            val = val*10 + (*p - '0');
            if (val > 99) return -1;
        }
        if (val < 1 || val > 99) return -1;
        *num = val;
        return 0;
    } else {
        *is_basement = 0;
        const char *p = s;
        if (*p == '\0') return -1;
        if (p[0] == '0') return -1; // no leading zeros (disallow "01")
        int val = 0;
        for (; *p; ++p) {
            if (!isdigit((unsigned char)*p)) return -1;
            val = val*10 + (*p - '0');
            if (val > 999) return -1;
        }
        if (val < 1 || val > 999) return -1;
        *num = val;
        return 0;
    }
}

bool floor_is_valid(const char *s) {
    int b=0, n=0;
    return parse_level(s, &b, &n) == 0;
}

bool floor_is_basement(const char *s) {
    return s && s[0] == 'B';
}

int floor_cmp_phys(const char *a, const char *b) {
    int ba=0, na=0;
    int bb=0, nb=0;
    if (parse_level(a, &ba, &na) != 0) return 0;
    if (parse_level(b, &bb, &nb) != 0) return 0;
    // Physical order: ... B3 < B2 < B1 < 1 < 2 < ...
    if (ba && !bb) return -1;
    if (!ba && bb) return 1;
    if (ba && bb) {
        // Among basements higher number is LOWER physically (B3 is below B2)
        if (na == nb) return 0;
        return (na > nb) ? -1 : 1;
    } else {
        if (na == nb) return 0;
        return (na < nb) ? -1 : 1;
    }
}

static void to_string(int is_b, int num, char out[4]) {
    if (is_b) {
        if (num >= 10) { out[0]='B'; out[1]=(char)('0'+(num/10)); out[2]=(char)('0'+(num%10)); out[3]='\0'; }
        else { out[0]='B'; out[1]=(char)('0'+num); out[2]='\0'; }
    } else {
        if (num >= 100) {
            out[0]=(char)('0'+(num/100));
            out[1]=(char)('0'+((num/10)%10));
            out[2]=(char)('0'+(num%10));
            out[3]='\0';
        } else if (num >= 10) {
            out[0]=(char)('0'+(num/10));
            out[1]=(char)('0'+(num%10));
            out[2]='\0';
        } else {
            out[0]=(char)('0'+num);
            out[1]='\0';
        }
    }
}

void floor_step_towards(const char *from, const char *to, char out[4]) {
    int bf=0, nf=0; (void)parse_level(from, &bf, &nf);
    int bt=0, nt=0; (void)parse_level(to, &bt, &nt);

    int cmp = floor_cmp_phys(from, to);
    if (cmp == 0) {
        // same
        to_string(bf, nf, out);
        return;
    }
    if (cmp < 0) { // from lower to higher -> step up
        // stepping up: from B1 goes to 1; from 1 goes to 2; from Bn to B(n-1)
        if (bf) {
            if (nf == 1) {
                // next is 1
                to_string(0, 1, out);
            } else {
                to_string(1, nf-1, out);
            }
        } else {
            to_string(0, nf+1, out);
        }
    } else { // step down
        if (!bf) {
            if (nf == 1) {
                to_string(1, 1, out); // 1 -> B1
            } else {
                to_string(0, nf-1, out);
            }
        } else {
            to_string(1, nf+1, out);
        }
    }
}

bool floor_up(const char *cur, char out[4]) {
    int b=0, n=0;
    if (parse_level(cur, &b, &n) != 0) return false;
    if (b) {
        if (n == 1) { to_string(0, 1, out); return true; }
        to_string(1, n-1, out); return true;
    } else {
        if (n >= 999) return false;
        to_string(0, n+1, out); return true;
    }
}

bool floor_down(const char *cur, char out[4]) {
    int b=0, n=0;
    if (parse_level(cur, &b, &n) != 0) return false;
    if (!b) {
        if (n == 1) { to_string(1, 1, out); return true; }
        to_string(0, n-1, out); return true;
    } else {
        if (n >= 99) return false;
        to_string(1, n+1, out); return true;
    }
}
