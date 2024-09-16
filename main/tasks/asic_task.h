#ifndef ASIC_TASK_H_
#define ASIC_TASK_H_

#include "freertos/FreeRTOS.h"
#include "mining.h"

/* Generic bit definitions. */
#define ASICTASK_JOB_NOW		( 0x01UL )
#define ASICTASK_RESTART		( 0x02UL )
typedef struct
{
    // ASIC may not return the nonce in the same order as the jobs were sent
    // it also may return a previous nonce under some circumstances
    // so we keep a list of jobs indexed by the job id
    bm_job **active_jobs;
} AsicTaskModule;

void ASIC_task(void *pvParameters);
void ASIC_task_send_event(uint8_t event);

#endif
