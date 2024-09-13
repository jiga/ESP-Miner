#ifndef STRATUM_TASK_H_
#define STRATUM_TASK_H_

#include "global_state.h"
typedef struct
{
    uint32_t stratum_difficulty;
} SystemTaskModule;

/* Generic bit definitions. */
#define eBIT_0		( 0x01UL )
#define eBIT_1		( 0x02UL )
#define eBIT_2		( 0x04UL )
#define eBIT_3		( 0x08UL )
#define eBIT_4		( 0x10UL )
#define eBIT_5		( 0x20UL )
#define eBIT_6		( 0x40UL )
#define eBIT_7		( 0x80UL )

void stratum_task(void *pvParameters);
esp_err_t Stratum_socket_connect(GlobalState *);

#endif