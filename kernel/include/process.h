#ifndef ON_PROCESS_H
#define ON_PROCESS_H
#include <stdbool.h>
#include <stdint.h>
/* Future isolated process contract. No loader, private CR3 or ring-3 entry yet. */
struct process_user_layout { uintptr_t entry, stack_bottom, stack_top; };
bool process_user_layout_valid(const struct process_user_layout *layout);
bool process_user_execution_enabled(void);
#endif
