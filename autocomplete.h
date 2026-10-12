#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_AUTOCOMP_MATCHES 16

typedef struct {
    char ghost_suffix[64];       /* Suffix to render in ghost style (e.g. "rt()") */
    char full_completion[64];    /* Full token (e.g. "sqrt()") */
    size_t prefix_len;           /* Length of prefix being completed (e.g. 2 for "sq") */
    size_t cursor_offset;        /* Relative cursor offset inside full_completion */
    bool is_func;                /* True if suggestion is a function */
    bool active;                 /* True if a suggestion is available */

    /* Multi-candidate cycling */
    size_t match_count;          /* Total matching candidates */
    size_t current_index;        /* Currently selected candidate index (0-based) */
    char preview_bar[256];       /* Lightweight candidate preview, e.g. "Tab: [before] • berlin (1/2)" */
    char candidate_names[MAX_AUTOCOMP_MATCHES][32]; /* All candidate names */
    bool candidate_is_func[MAX_AUTOCOMP_MATCHES];
} AutocompleteSuggestion;

/* Query autocomplete suggestion with explicit candidate cycle index */
bool autocomplete_query_cycle(const char *input, size_t cursor_pos, size_t cycle_idx, AutocompleteSuggestion *out);

/* Query autocomplete suggestion (defaults to cycle_idx = 0) */
bool autocomplete_query(const char *input, size_t cursor_pos, AutocompleteSuggestion *out);

/* Commit suggestion into buffer, updating cursor_pos and input_len */
bool autocomplete_commit(char *buf, size_t buf_sz, size_t *cursor_pos, size_t *input_len, const AutocompleteSuggestion *sug);

/* Cycle to candidate in buffer, replacing previous committed completion */
bool autocomplete_cycle_replace(char *buf, size_t buf_sz, size_t *cursor_pos, size_t *input_len,
                                size_t prev_comp_len, const AutocompleteSuggestion *sug);

#endif /* AUTOCOMPLETE_H */
