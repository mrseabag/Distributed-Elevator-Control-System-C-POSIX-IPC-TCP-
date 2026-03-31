#ifndef FLOORS_H
#define FLOORS_H

#include <stdbool.h>

// Validates a floor string: "B1".."B99" or "1".."999" (no leading zeros)
bool floor_is_valid(const char *s);

// Returns true if s represents a basement level (starts with 'B')
bool floor_is_basement(const char *s);

// Compare two valid floors by physical height: returns -1 if a<b (lower), 0 if equal, +1 if a>b (higher)
int floor_cmp_phys(const char *a, const char *b);

// Compute the next adjacent floor towards 'to' from 'from'. Writes result into out[4].
// e.g., from "1" to "3" -> "2"; from "2" to "B1" -> "1" then "B1".
void floor_step_towards(const char *from, const char *to, char out[4]);

// Compute one-step up or down from a valid floor. Returns false if cannot (above 999 or below B99).
bool floor_up(const char *cur, char out[4]);
bool floor_down(const char *cur, char out[4]);

#endif // FLOORS_H
