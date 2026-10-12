#include "brackets.h"
#include <ctype.h>
#include <string.h>

/*
 * Counts the number of unclosed '(' brackets in the buffer.
 */
int count_unclosed_parens(const char *buf, size_t len) {
    if (!buf || len == 0) return 0;
    int depth = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '(') {
            depth++;
        } else if (buf[i] == ')') {
            if (depth > 0) depth--;
        }
    }
    return depth;
}

/*
 * Checks if there is an unmatched ')' after the given cursor position.
 */
bool has_unpaired_closing_bracket_after(const char *buf, size_t len, size_t cursor) {
    if (!buf || len == 0 || cursor >= len) return false;

    int open_before = 0;
    for (size_t i = 0; i < cursor && i < len; i++) {
        if (buf[i] == '(') {
            open_before++;
        } else if (buf[i] == ')') {
            if (open_before > 0) open_before--;
        }
    }

    int open_after = 0;
    for (size_t i = cursor; i < len; i++) {
        if (buf[i] == '(') {
            open_after++;
        } else if (buf[i] == ')') {
            if (open_after > 0) {
                open_after--;
            } else if (open_before > 0) {
                open_before--;
            } else {
                return true; /* Unpaired ')' found after cursor */
            }
        }
    }
    return false;
}

/*
 * Visual Bracket Matching:
 * Finds indices of the matching pair touching the cursor (match_a, match_b),
 * or sets both to -1 if cursor is not touching a bracket.
 */
void find_matching_bracket(const char *buf, size_t len, size_t cursor, int *match_a, int *match_b) {
    *match_a = -1;
    *match_b = -1;
    if (!buf || len == 0) return;

    int target_idx = -1;
    bool is_open = false;

    if (cursor > 0 && buf[cursor - 1] == '(') {
        target_idx = (int)(cursor - 1);
        is_open = true;
    } else if (cursor < len && buf[cursor] == '(') {
        target_idx = (int)cursor;
        is_open = true;
    } else if (cursor > 0 && buf[cursor - 1] == ')') {
        target_idx = (int)(cursor - 1);
        is_open = false;
    } else if (cursor < len && buf[cursor] == ')') {
        target_idx = (int)cursor;
        is_open = false;
    }

    if (target_idx < 0) return;

    if (is_open) {
        int depth = 0;
        for (size_t i = (size_t)target_idx; i < len; i++) {
            if (buf[i] == '(') {
                depth++;
            } else if (buf[i] == ')') {
                depth--;
                if (depth == 0) {
                    *match_a = target_idx;
                    *match_b = (int)i;
                    return;
                }
            }
        }
    } else {
        int depth = 0;
        for (int i = target_idx; i >= 0; i--) {
            if (buf[i] == ')') {
                depth++;
            } else if (buf[i] == '(') {
                depth--;
                if (depth == 0) {
                    *match_a = i;
                    *match_b = target_idx;
                    return;
                }
            }
        }
    }
}

/*
 * Helper: Find end of an operand/subexpression starting at 'start'.
 */
static size_t find_operand_end(const char *buf, size_t len, size_t start) {
    if (start >= len) return start;

    /* Bracketed subexpression: ( ... ) */
    if (buf[start] == '(') {
        int depth = 1;
        size_t idx = start + 1;
        while (idx < len && depth > 0) {
            if (buf[idx] == '(') depth++;
            else if (buf[idx] == ')') depth--;
            if (depth == 0) return idx + 1;
            idx++;
        }
        return idx;
    }

    /* Numeric literal: digits, decimal points, commas, scientific notation */
    if (isdigit((unsigned char)buf[start]) || buf[start] == '.') {
        size_t idx = start;
        while (idx < len && (isdigit((unsigned char)buf[idx]) || buf[idx] == '.' || buf[idx] == ',')) {
            idx++;
        }
        if (idx < len && (buf[idx] == 'e' || buf[idx] == 'E')) {
            size_t e_idx = idx + 1;
            if (e_idx < len && (buf[e_idx] == '+' || buf[e_idx] == '-')) e_idx++;
            if (e_idx < len && isdigit((unsigned char)buf[e_idx])) {
                while (e_idx < len && isdigit((unsigned char)buf[e_idx])) e_idx++;
                idx = e_idx;
            }
        }
        return idx;
    }

    /* Identifier or Function call: e.g. "pi", "ans", or "sqrt(144)" */
    if (isalpha((unsigned char)buf[start]) || buf[start] == '_') {
        size_t idx = start;
        while (idx < len && (isalnum((unsigned char)buf[idx]) || buf[idx] == '_')) {
            idx++;
        }
        if (idx < len && buf[idx] == '(') {
            int depth = 1;
            idx++;
            while (idx < len && depth > 0) {
                if (buf[idx] == '(') depth++;
                else if (buf[idx] == ')') depth--;
                if (depth == 0) return idx + 1;
                idx++;
            }
        }
        return idx;
    }

    return start;
}

/*
 * Intelligent insertion of '(':
 * - If unpaired ')' exists ahead, inserts '(' alone.
 * - If cursor is right before an operand, wraps that operand: (|operand)
 * - Otherwise auto-pairs: (|)
 */
bool smart_insert_open_paren(char *buf, size_t buf_sz, size_t *cursor, size_t *len) {
    if (!buf || !cursor || !len) return false;

    /* Case 1: Unpaired ')' ahead - insert single '(' to balance */
    if (has_unpaired_closing_bracket_after(buf, *len, *cursor)) {
        if (*len + 1 >= buf_sz) return false;
        memmove(&buf[*cursor + 1], &buf[*cursor], *len - *cursor + 1);
        buf[*cursor] = '(';
        (*cursor)++;
        (*len)++;
        return true;
    }

    /* Case 2: Before an operand - wrap operand into (|operand) */
    if (*cursor < *len) {
        char c = buf[*cursor];
        if (isdigit((unsigned char)c) || c == '.' || isalpha((unsigned char)c) || c == '_' || c == '(') {
            size_t op_end = find_operand_end(buf, *len, *cursor);
            if (op_end > *cursor) {
                if (*len + 2 >= buf_sz) return false;
                size_t op_len = op_end - *cursor;
                /* Move suffix right by 2 */
                memmove(&buf[op_end + 2], &buf[op_end], *len - op_end + 1);
                /* Put ')' after operand */
                buf[op_end + 1] = ')';
                /* Move operand right by 1 */
                memmove(&buf[*cursor + 1], &buf[*cursor], op_len);
                /* Put '(' before operand */
                buf[*cursor] = '(';
                (*cursor)++; /* cursor right after '(' */
                (*len) += 2;
                return true;
            }
        }
    }

    /* Case 3: General auto-pair: (|) */
    if (*len + 2 >= buf_sz) return false;
    memmove(&buf[*cursor + 2], &buf[*cursor], *len - *cursor + 1);
    buf[*cursor] = '(';
    buf[*cursor + 1] = ')';
    (*cursor)++;
    (*len) += 2;
    return true;
}

/*
 * Intelligent insertion of ')':
 * - Overtype: step over if cursor is immediately before ')'
 * - Normal: insert ')' if unmatched '(' exists before cursor
 * - Retroactive Match Prediction: wrap preceding subexpression: (expr)|
 */
bool smart_insert_close_paren(char *buf, size_t buf_sz, size_t *cursor, size_t *len) {
    if (!buf || !cursor || !len) return false;

    /* Case 1: Overtype */
    if (*cursor < *len && buf[*cursor] == ')') {
        (*cursor)++;
        return true;
    }

    /* Count unmatched '(' before cursor */
    int unclosed_before = 0;
    for (size_t i = 0; i < *cursor; i++) {
        if (buf[i] == '(') unclosed_before++;
        else if (buf[i] == ')') {
            if (unclosed_before > 0) unclosed_before--;
        }
    }

    /* Case 2: Normal close parenthesis */
    if (unclosed_before > 0) {
        if (*len + 1 >= buf_sz) return false;
        memmove(&buf[*cursor + 1], &buf[*cursor], *len - *cursor + 1);
        buf[*cursor] = ')';
        (*cursor)++;
        (*len)++;
        return true;
    }

    /* Case 3: Retroactive Match Prediction */
    /* Find beginning of the current subexpression or statement */
    size_t expr_start = 0;
    if (*cursor > 0) {
        size_t i = *cursor;
        while (i > 0) {
            char ch = buf[i - 1];
            if (ch == '=' || ch == ',') {
                expr_start = i;
                break;
            }
            i--;
        }
        /* Skip leading whitespace after '=' or ',' */
        while (expr_start < *cursor && isspace((unsigned char)buf[expr_start])) {
            expr_start++;
        }
    }

    if (*len + 2 >= buf_sz) return false;

    /* Move suffix right by 2 */
    memmove(&buf[*cursor + 2], &buf[*cursor], *len - *cursor + 1);
    buf[*cursor + 1] = ')';

    /* Move middle right by 1 */
    memmove(&buf[expr_start + 1], &buf[expr_start], *cursor - expr_start);
    buf[expr_start] = '(';

    (*cursor) += 2;
    (*len) += 2;
    return true;
}

/*
 * Intelligent backspace:
 * - If cursor is between an empty pair "()", deletes both.
 * - Otherwise standard backspace.
 */
bool smart_backspace_paren(char *buf, size_t *cursor, size_t *len) {
    if (!buf || !cursor || !len || *cursor == 0 || *len == 0) return false;

    if (buf[*cursor - 1] == '(' && *cursor < *len && buf[*cursor] == ')') {
        memmove(&buf[*cursor - 1], &buf[*cursor + 1], *len - *cursor);
        (*len) -= 2;
        (*cursor)--;
        buf[*len] = '\0';
        return true;
    }

    memmove(&buf[*cursor - 1], &buf[*cursor], *len - *cursor + 1);
    (*len)--;
    (*cursor)--;
    return true;
}

/*
 * Intelligent delete:
 * - If cursor is before an empty pair "()", deletes both.
 * - Otherwise standard delete.
 */
bool smart_delete_paren(char *buf, size_t *cursor, size_t *len) {
    if (!buf || !cursor || !len || *cursor >= *len) return false;

    if (buf[*cursor] == '(' && *cursor + 1 < *len && buf[*cursor + 1] == ')') {
        memmove(&buf[*cursor], &buf[*cursor + 2], *len - *cursor - 1);
        (*len) -= 2;
        buf[*len] = '\0';
        return true;
    }

    memmove(&buf[*cursor], &buf[*cursor + 1], *len - *cursor);
    (*len)--;
    return true;
}

/*
 * ==========================================================================
 * Intelligent Bracket Cycling Implementation (Retroactive & Proactive)
 * ==========================================================================
 */

void bracket_cycle_reset(BracketCycleState *state) {
    if (!state) return;
    state->active = false;
    state->mode = BRACKET_CYCLE_NONE;
    state->anchor = 0;
    state->cut_count = 0;
    state->current_idx = 0;
    for (int i = 0; i < 8; i++) state->cut_points[i] = 0;
}

int find_retroactive_cut_points(const char *buf, size_t cursor, size_t cut_points[8]) {
    if (!buf || cursor == 0) {
        cut_points[0] = 0;
        return 1;
    }

    /* 1. Find expr_start (stop at '=' or ',') */
    size_t expr_start = 0;
    for (size_t i = cursor; i > 0; i--) {
        char ch = buf[i - 1];
        if (ch == '=' || ch == ',') {
            expr_start = i;
            break;
        }
    }
    while (expr_start < cursor && isspace((unsigned char)buf[expr_start])) {
        expr_start++;
    }

    if (cursor <= expr_start) {
        cut_points[0] = expr_start;
        return 1;
    }

    /* 2. Scan backwards from cursor to expr_start identifying operator boundaries */
    int cut_count = 0;
    int depth = 0;
    bool skipped_last_operand = false;

    for (size_t i = cursor; i > expr_start; i--) {
        char ch = buf[i - 1];
        if (ch == ')') {
            depth++;
        } else if (ch == '(') {
            if (depth > 0) depth--;
        } else if (depth == 0) {
            /* Check if ch is a binary operator */
            if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' || ch == '^') {
                /* Preceded by non-operator character to avoid unary signs */
                bool is_binary = false;
                for (size_t j = i - 1; j > expr_start; j--) {
                    char prev = buf[j - 1];
                    if (isspace((unsigned char)prev)) continue;
                    if (isalnum((unsigned char)prev) || prev == ')' || prev == '_') {
                        is_binary = true;
                    }
                    break;
                }

                if (is_binary) {
                    if (!skipped_last_operand) {
                        /* The operator immediately before the rightmost operand */
                        skipped_last_operand = true;
                    } else {
                        /* Start of operand right after this operator */
                        size_t token_start = i;
                        while (token_start < cursor && isspace((unsigned char)buf[token_start])) {
                            token_start++;
                        }
                        if (token_start < cursor && cut_count < 7) {
                            /* Avoid duplicates */
                            bool dup = false;
                            for (int k = 0; k < cut_count; k++) {
                                if (cut_points[k] == token_start) { dup = true; break; }
                            }
                            if (!dup) cut_points[cut_count++] = token_start;
                        }
                    }
                }
            }
        }
    }

    /* 3. Always include expr_start at the end (outermost scope) */
    bool has_expr_start = false;
    for (int k = 0; k < cut_count; k++) {
        if (cut_points[k] == expr_start) { has_expr_start = true; break; }
    }
    if (!has_expr_start && cut_count < 8) {
        cut_points[cut_count++] = expr_start;
    }

    return cut_count > 0 ? cut_count : 1;
}

int find_proactive_cut_points(const char *buf, size_t len, size_t cursor, size_t cut_points[8]) {
    if (!buf || cursor >= len) {
        cut_points[0] = (buf && cursor <= len) ? cursor : 0;
        return 1;
    }

    /* 1. Skip leading whitespace after cursor */
    size_t start = cursor;
    while (start < len && isspace((unsigned char)buf[start])) {
        start++;
    }
    if (start >= len) {
        cut_points[0] = len;
        return 1;
    }

    /* 2. Find expr_end (stop at '=' or ',') */
    size_t expr_end = len;
    for (size_t i = start; i < len; i++) {
        char ch = buf[i];
        if (ch == '=' || ch == ',') {
            expr_end = i;
            break;
        }
    }
    while (expr_end > start && isspace((unsigned char)buf[expr_end - 1])) {
        expr_end--;
    }
    if (start >= expr_end) {
        cut_points[0] = expr_end;
        return 1;
    }

    /* 3. Find binary operator cuts at parenthesis depth 0 */
    size_t op_cuts[8];
    int n_op_cuts = 0;
    int depth = 0;

    for (size_t i = start; i < expr_end; i++) {
        char ch = buf[i];
        if (ch == '(') {
            depth++;
        } else if (ch == ')') {
            if (depth > 0) {
                depth--;
            } else {
                /* Unmatched ')' ahead: expression boundary stops here */
                expr_end = i;
                break;
            }
        } else if (depth == 0) {
            if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' || ch == '^') {
                /* Preceded by non-operator character to avoid unary signs */
                bool is_binary = false;
                for (size_t j = i; j > start; j--) {
                    char prev = buf[j - 1];
                    if (isspace((unsigned char)prev)) continue;
                    if (isalnum((unsigned char)prev) || prev == ')' || prev == '_') {
                        is_binary = true;
                    }
                    break;
                }
                if (is_binary) {
                    size_t cut = i;
                    while (cut > start && isspace((unsigned char)buf[cut - 1])) {
                        cut--;
                    }
                    if (cut > start && n_op_cuts < 8) {
                        bool dup = false;
                        for (int k = 0; k < n_op_cuts; k++) {
                            if (op_cuts[k] == cut) { dup = true; break; }
                        }
                        if (!dup) op_cuts[n_op_cuts++] = cut;
                    }
                }
            }
        }
    }

    while (expr_end > start && isspace((unsigned char)buf[expr_end - 1])) {
        expr_end--;
    }

    /*
     * 4. Build prioritized cut points list:
     * - Compound subexpression boundaries: op_cuts[1] .. op_cuts[n_op_cuts - 1]
     * - Full expression: expr_end
     * - Single operand: op_cuts[0] (or find_operand_end)
     */
    int cut_count = 0;

#define ADD_PRO_CUT(p) do { \
        size_t _p = (p); \
        if (_p > start && _p <= expr_end && cut_count < 8) { \
            bool _dup = false; \
            for (int _k = 0; _k < cut_count; _k++) { \
                if (cut_points[_k] == _p) { _dup = true; break; } \
            } \
            if (!_dup) cut_points[cut_count++] = _p; \
        } \
    } while (0)

    if (n_op_cuts > 1) {
        for (int i = 1; i < n_op_cuts; i++) {
            ADD_PRO_CUT(op_cuts[i]);
        }
    }

    ADD_PRO_CUT(expr_end);

    if (n_op_cuts > 0) {
        ADD_PRO_CUT(op_cuts[0]);
    } else {
        size_t op_end = find_operand_end(buf, expr_end, start);
        if (op_end > start) {
            ADD_PRO_CUT(op_end);
        }
    }

#undef ADD_PRO_CUT

    if (cut_count == 0) {
        cut_points[0] = expr_end;
        return 1;
    }

    return cut_count;
}

bool bracket_cycle_step_retro(BracketCycleState *state, const char *buf, size_t cursor) {
    if (!state || !buf) return false;

    if (state->active && state->mode == BRACKET_CYCLE_RETRO && state->anchor == cursor && state->cut_count > 0) {
        /* Advance to next candidate cut point */
        state->current_idx = (state->current_idx + 1) % state->cut_count;
        return true;
    }

    /* First tap: initialize candidate cut points */
    bracket_cycle_reset(state);
    state->mode = BRACKET_CYCLE_RETRO;
    state->anchor = cursor;
    state->cut_count = find_retroactive_cut_points(buf, cursor, state->cut_points);
    state->current_idx = 0;
    state->active = (state->cut_count > 0);
    return state->active;
}

bool bracket_cycle_step_pro(BracketCycleState *state, const char *buf, size_t len, size_t cursor) {
    if (!state || !buf) return false;

    if (state->active && state->mode == BRACKET_CYCLE_PRO && state->anchor == cursor && state->cut_count > 0) {
        /* Advance to next candidate cut point */
        state->current_idx = (state->current_idx + 1) % state->cut_count;
        return true;
    }

    /* First tap: initialize candidate cut points */
    bracket_cycle_reset(state);
    state->mode = BRACKET_CYCLE_PRO;
    state->anchor = cursor;
    state->cut_count = find_proactive_cut_points(buf, len, cursor, state->cut_points);
    state->current_idx = 0;
    state->active = (state->cut_count > 0);
    return state->active;
}

bool bracket_cycle_commit(BracketCycleState *state, char *buf, size_t buf_sz, size_t *cursor, size_t *len) {
    if (!state || !state->active || !buf || !cursor || !len) return false;

    if (state->current_idx >= state->cut_count) {
        bracket_cycle_reset(state);
        return false;
    }

    size_t cut_pos = state->cut_points[state->current_idx];
    size_t anchor_pos = state->anchor;
    size_t open_pos = anchor_pos < cut_pos ? anchor_pos : cut_pos;
    size_t close_pos = anchor_pos < cut_pos ? cut_pos : anchor_pos;

    if (open_pos > close_pos || close_pos > *len) {
        bracket_cycle_reset(state);
        return false;
    }

    if (*len + 2 >= buf_sz) {
        bracket_cycle_reset(state);
        return false;
    }

    /* Move suffix after close_pos right by 2 */
    memmove(&buf[close_pos + 2], &buf[close_pos], *len - close_pos + 1);
    /* Insert ')' */
    buf[close_pos + 1] = ')';

    /* Move middle between open_pos and close_pos right by 1 */
    memmove(&buf[open_pos + 1], &buf[open_pos], close_pos - open_pos);
    /* Insert '(' */
    buf[open_pos] = '(';

    *len += 2;

    if (state->mode == BRACKET_CYCLE_RETRO) {
        *cursor = close_pos + 2;
    } else {
        *cursor = open_pos + 1;
    }

    bracket_cycle_reset(state);
    return true;
}

void bracket_cycle_cancel(BracketCycleState *state) {
    bracket_cycle_reset(state);
}

void retro_paren_reset(RetroParenState *state) {
    bracket_cycle_reset(state);
}

bool retro_paren_step(RetroParenState *state, const char *buf, size_t cursor) {
    return bracket_cycle_step_retro(state, buf, cursor);
}

bool retro_paren_commit(RetroParenState *state, char *buf, size_t buf_sz, size_t *cursor, size_t *len) {
    return bracket_cycle_commit(state, buf, buf_sz, cursor, len);
}

void retro_paren_cancel(RetroParenState *state) {
    bracket_cycle_cancel(state);
}

