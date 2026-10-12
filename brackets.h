#ifndef ELEPHANT_BRACKETS_H
#define ELEPHANT_BRACKETS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * ==========================================================================
 * Elephant Calc - Intelligent Bracket Engine
 * ==========================================================================
 */

/*
 * Counts the number of open '(' without matching ')' in the given buffer.
 */
int count_unclosed_parens(const char *buf, size_t len);

/*
 * Checks if there is an unmatched ')' after the given cursor position.
 */
bool has_unpaired_closing_bracket_after(const char *buf, size_t len, size_t cursor);

/*
 * Visual Bracket Matching:
 * Finds indices of the matching pair touching the cursor (match_a, match_b),
 * or -1 if cursor is not adjacent to a bracket.
 */
void find_matching_bracket(const char *buf, size_t len, size_t cursor, int *match_a, int *match_b);

/*
 * Intelligent insertion of '(':
 * - If unpaired ')' exists ahead, inserts '(' alone to balance it.
 * - If cursor is right before an operand (number / identifier / function / group),
 *   wraps that operand: (|operand)
 * - Otherwise (empty, operator, space, end of string), auto-pairs: (|)
 * Returns true if modified.
 */
bool smart_insert_open_paren(char *buf, size_t buf_sz, size_t *cursor, size_t *len);

/*
 * Intelligent insertion of ')':
 * - Overtype: if next character is already ')', steps over without duplicating.
 * - Normal: if there are unmatched '(' before cursor, inserts ')' at cursor.
 * - Retroactive Match Prediction: if typing ')' with NO open '(' preceding it,
 *   retroactively inserts '(' at the start of current subexpression and ')' at cursor.
 * Returns true if modified.
 */
bool smart_insert_close_paren(char *buf, size_t buf_sz, size_t *cursor, size_t *len);

/*
 * Intelligent backspace:
 * - If cursor is between an empty pair "()", deletes both '(' and ')'.
 * - Otherwise standard backspace (removes character before cursor).
 * Returns true if backspaced, false if cursor was at 0.
 */
bool smart_backspace_paren(char *buf, size_t *cursor, size_t *len);

/*
 * Intelligent delete:
 * - If cursor is right before an empty pair "()", deletes both.
 * - Otherwise standard delete (removes character at cursor).
 * Returns true if deleted, false if cursor was at end.
 */
bool smart_delete_paren(char *buf, size_t *cursor, size_t *len);

/*
 * ==========================================================================
 * Intelligent Bracket Cycling Engine (Retroactive & Proactive)
 * ==========================================================================
 */

typedef enum {
    BRACKET_CYCLE_NONE = 0,
    BRACKET_CYCLE_RETRO,    /* ')' entered: anchor is close bracket, cycles open cuts backward */
    BRACKET_CYCLE_PRO       /* '(' entered: anchor is open bracket, cycles close cuts forward */
} BracketCycleMode;

typedef struct {
    bool active;
    BracketCycleMode mode;
    size_t anchor;          /* cursor position where typed bracket was entered */
    size_t cut_points[8];   /* candidate opposite bracket cut points in buffer */
    int cut_count;          /* number of candidate cut points */
    int current_idx;        /* currently selected cut point (0 .. cut_count - 1) */
} BracketCycleState;

/* Backwards compatibility aliases */
typedef BracketCycleState RetroParenState;

void bracket_cycle_reset(BracketCycleState *state);
int find_retroactive_cut_points(const char *buf, size_t cursor, size_t cut_points[8]);
int find_proactive_cut_points(const char *buf, size_t len, size_t cursor, size_t cut_points[8]);

bool bracket_cycle_step_retro(BracketCycleState *state, const char *buf, size_t cursor);
bool bracket_cycle_step_pro(BracketCycleState *state, const char *buf, size_t len, size_t cursor);
bool bracket_cycle_commit(BracketCycleState *state, char *buf, size_t buf_sz, size_t *cursor, size_t *len);
void bracket_cycle_cancel(BracketCycleState *state);

/* Convenience / backward compatibility wrappers */
void retro_paren_reset(RetroParenState *state);
bool retro_paren_step(RetroParenState *state, const char *buf, size_t cursor);
bool retro_paren_commit(RetroParenState *state, char *buf, size_t buf_sz, size_t *cursor, size_t *len);
void retro_paren_cancel(RetroParenState *state);

#endif /* ELEPHANT_BRACKETS_H */
