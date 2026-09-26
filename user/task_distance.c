#include <trykernel.h>
#include "vl53l1x.h"
#include "task_distance.h"
#include "motor.h"

#define DISTANCE_SAMPLE_INTERVAL_MS  100
#define DISTANCE_RETRY_INTERVAL_MS   1000

static volatile UH shared_average_mm;
static volatile UH shared_latest_mm;
static volatile UINT shared_sample_count;
static volatile BOOL shared_ready;

BOOL task_distance_get(UH *average_mm, UH *latest_mm, UINT *sample_count)
{
    UINT intsts;

    if((average_mm == NULL) || (latest_mm == NULL)
            || (sample_count == NULL)) return FALSE;

    DI(intsts);
    if(shared_ready == FALSE){
        EI(intsts);
        return FALSE;
    }
    *average_mm = shared_average_mm;
    *latest_mm = shared_latest_mm;
    *sample_count = shared_sample_count;
    EI(intsts);
    return TRUE;
}

void task_distance(INT stacd, void *exinf)
{
    UH samples[DISTANCE_MOVING_AVERAGE_SIZE];
    UW sum = 0U;
    UINT count = 0U;
    UINT next = 0U;
    UH distance_mm;
    UINT intsts;
    UINT init_index;

    (void)stacd;
    (void)exinf;

    for(init_index = 0U; init_index < DISTANCE_MOVING_AVERAGE_SIZE;
            init_index++){
        samples[init_index] = 0U;
    }

    while(1){
        if(vl53l1x_read_distance(&distance_mm) == FALSE){
            DI(intsts);
            shared_ready = FALSE;
            EI(intsts);
            motor_obstacle_update(0U, FALSE);
            (void)tk_dly_tsk(DISTANCE_RETRY_INTERVAL_MS);
            continue;
        }

        if(count == DISTANCE_MOVING_AVERAGE_SIZE){
            sum -= samples[next];
        }else{
            count++;
        }
        samples[next] = distance_mm;
        sum += distance_mm;
        next++;
        if(next == DISTANCE_MOVING_AVERAGE_SIZE) next = 0U;

        DI(intsts);
        shared_latest_mm = distance_mm;
        shared_average_mm = (UH)(sum / count);
        shared_sample_count = count;
        shared_ready = TRUE;
        EI(intsts);

        motor_obstacle_update(distance_mm, TRUE);

        (void)tk_dly_tsk(DISTANCE_SAMPLE_INTERVAL_MS);
    }
}
