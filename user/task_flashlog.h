#ifndef TASK_FLASHLOG_H
#define TASK_FLASHLOG_H

#include <trykernel.h>

typedef enum {
    FLASHLOG_EVENT_MOVING = 1,
    FLASHLOG_EVENT_STOPPED = 2
} FLASHLOG_EVENT;

ER task_flashlog_init(void);
ER task_flashlog_notify(FLASHLOG_EVENT event, UW sample_count);
BOOL task_flashlog_clear(void);
void task_flashlog_dump(void);
void task_flashlog(INT stacd, void *exinf);

#endif /* TASK_FLASHLOG_H */