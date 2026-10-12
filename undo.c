#include "undo.h"
#include <string.h>

void undo_stack_init(UndoStack *stack, const char *initial_buf, size_t cursor) {
    if (!stack) return;
    memset(stack, 0, sizeof(*stack));
    if (initial_buf) {
        strncpy(stack->entries[0].buf, initial_buf, sizeof(stack->entries[0].buf) - 1);
        stack->entries[0].buf[sizeof(stack->entries[0].buf) - 1] = '\0';
    } else {
        stack->entries[0].buf[0] = '\0';
    }
    stack->entries[0].cursor = cursor;
    stack->count = 1;
    stack->index = 0;
    stack->last_action = UNDO_ACTION_OTHER;
}

void undo_stack_push(UndoStack *stack, const char *buf, size_t cursor, UndoActionType action) {
    if (!stack || !buf) return;

    /* If identical to current state, ignore or just update cursor */
    if (stack->count > 0 && stack->index >= 0 && stack->index < stack->count) {
        if (strcmp(stack->entries[stack->index].buf, buf) == 0) {
            stack->entries[stack->index].cursor = cursor;
            return;
        }

        /* Check if we can coalesce consecutive typing of alphanumeric characters */
        if (action == UNDO_ACTION_TYPE && stack->last_action == UNDO_ACTION_TYPE) {
            /* Coalesce: update current entry */
            strncpy(stack->entries[stack->index].buf, buf, sizeof(stack->entries[stack->index].buf) - 1);
            stack->entries[stack->index].buf[sizeof(stack->entries[stack->index].buf) - 1] = '\0';
            stack->entries[stack->index].cursor = cursor;
            return;
        }
    }

    /* Truncate forward redo history on new edit branch */
    if (stack->index + 1 < stack->count) {
        stack->count = stack->index + 1;
    }

    /* Check if stack is full */
    if (stack->count >= MAX_UNDO_STATES) {
        memmove(&stack->entries[0], &stack->entries[1], (MAX_UNDO_STATES - 1) * sizeof(UndoEntry));
        stack->count--;
        stack->index--;
    }

    /* Append new entry */
    int new_idx = stack->index + 1;
    if (new_idx >= MAX_UNDO_STATES) new_idx = MAX_UNDO_STATES - 1;

    strncpy(stack->entries[new_idx].buf, buf, sizeof(stack->entries[new_idx].buf) - 1);
    stack->entries[new_idx].buf[sizeof(stack->entries[new_idx].buf) - 1] = '\0';
    stack->entries[new_idx].cursor = cursor;

    stack->index = new_idx;
    stack->count = new_idx + 1;
    stack->last_action = action;
}

bool undo_stack_undo(UndoStack *stack, char *out_buf, size_t buf_sz, size_t *out_cursor) {
    if (!stack || !out_buf || !out_cursor || stack->index <= 0) return false;

    stack->index--;
    strncpy(out_buf, stack->entries[stack->index].buf, buf_sz - 1);
    out_buf[buf_sz - 1] = '\0';
    *out_cursor = stack->entries[stack->index].cursor;
    stack->last_action = UNDO_ACTION_OTHER;
    return true;
}

bool undo_stack_redo(UndoStack *stack, char *out_buf, size_t buf_sz, size_t *out_cursor) {
    if (!stack || !out_buf || !out_cursor || stack->index + 1 >= stack->count) return false;

    stack->index++;
    strncpy(out_buf, stack->entries[stack->index].buf, buf_sz - 1);
    out_buf[buf_sz - 1] = '\0';
    *out_cursor = stack->entries[stack->index].cursor;
    stack->last_action = UNDO_ACTION_OTHER;
    return true;
}

bool undo_stack_can_undo(const UndoStack *stack) {
    return (stack && stack->index > 0);
}

bool undo_stack_can_redo(const UndoStack *stack) {
    return (stack && stack->index + 1 < stack->count);
}
