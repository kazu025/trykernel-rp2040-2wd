#ifndef TASK_DISTANCE_H
#define TASK_DISTANCE_H

#include <trykernel.h>

#define DISTANCE_MOVING_AVERAGE_SIZE  5U

void task_distance(INT stacd, void *exinf);
BOOL task_distance_get(UH *average_mm, UH *latest_mm, UINT *sample_count);

#endif /* TASK_DISTANCE_H */
