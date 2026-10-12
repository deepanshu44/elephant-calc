#ifndef ELEPHANT_UNDO_H
#define ELEPHANT_UNDO_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_UNDO_STATES 128

typedef enum {
    UNDO_ACTION_OTHER = 0,
    UNDO_ACTION_TYPE,
    UNDO_ACTION_DELETE
} UndoActionType;

typedef struct {
    char buf[256];
    size_t cursor;
} UndoEntry;

typedef struct {
    UndoEntry entries[MAX_UNDO_STATES];
    int count;          /* total snapshots recorded in active history (1 .. MAX_UNDO_STATES) */
    int index;          /* current active state index (0 .. count - 1) */
    UndoActionType last_action;
} UndoStack;

/*
 * Initialize the undo stack with an initial starting state.
 */
void undo_stack_init(UndoStack *stack, const char *initial_buf, size_t cursor);

/*
 * Record a state snapshot after mutation.
 * action: type of mutation (UNDO_ACTION_TYPE, UNDO_ACTION_DELETE, UNDO_ACTION_OTHER).
 * Coalesces consecutive alphanumeric typing into a single undo step.
 */
void undo_stack_push(UndoStack *stack, const char *buf, size_t cursor, UndoActionType action);

/*
 * Undo: reverts to previous state if available.
 * Returns true if undo succeeded, copying restored state to out_buf and out_cursor.
 */
bool undo_stack_undo(UndoStack *stack, char *out_buf, size_t buf_sz, size_t *out_cursor);

/*
 * Redo: advances to next state if available.
 * Returns true if redo succeeded, copying restored state to out_buf and out_cursor.
 */
bool undo_stack_redo(UndoStack *stack, char *out_buf, size_t buf_sz, size_t *out_cursor);

/*
 * Status queries: check if undo or redo can currently be performed.
 */
bool undo_stack_can_undo(const UndoStack *stack);
bool undo_stack_can_redo(const UndoStack *stack);

#endif /* ELEPHANT_UNDO_H */
