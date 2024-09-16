#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"

#include "system.h"
#include "work_queue.h"
#include "serial.h"
#include "bm1397.h"
#include "asic_task.h"


#define ASIC_JOB_QUEUE_SIZE 128

static const char *TAG = "ASIC_task";

// Declare a variable to hold the created asci_task event group.
EventGroupHandle_t asicTaskEventGroup;

static void init_asic_task(GlobalState *GLOBAL_STATE);

void ASIC_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    asicTaskEventGroup = xEventGroupCreate(); //create the event group
    EventBits_t eventBits;

    while (1) {
        //wait for task event to continue
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
        ESP_LOGI(TAG, "ASIC Task Started");

        init_asic_task(GLOBAL_STATE);

        while (1) {
            //wait here for an event or a timeout
            eventBits = xEventGroupWaitBits(asicTaskEventGroup,   
                ASICTASK_JOB_NOW | ASICTASK_RESTART, //events to wait for
                pdTRUE, pdFALSE,
                (GLOBAL_STATE->asic_job_frequency_ms / portTICK_PERIOD_MS)); // timeout

            if (eventBits & ASICTASK_RESTART) {
                break;
            }

            bm_job *next_bm_job = (bm_job *)queue_dequeue(&GLOBAL_STATE->ASIC_jobs_queue);

            if (next_bm_job->pool_diff != GLOBAL_STATE->stratum_difficulty)
            {
                ESP_LOGI(TAG, "New pool difficulty %lu", next_bm_job->pool_diff);
                GLOBAL_STATE->stratum_difficulty = next_bm_job->pool_diff;
            }

            (*GLOBAL_STATE->ASIC_functions.send_work_fn)(GLOBAL_STATE, next_bm_job); // send the job to the ASIC
        }
    }
}

void ASIC_task_send_event(uint8_t event) {
    xEventGroupSetBits(asicTaskEventGroup, event);
}

static void init_asic_task(GlobalState *GLOBAL_STATE)
{

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * ASIC_JOB_QUEUE_SIZE);
    GLOBAL_STATE->valid_jobs = malloc(sizeof(uint8_t) * ASIC_JOB_QUEUE_SIZE);
    for (int i = 0; i < ASIC_JOB_QUEUE_SIZE; i++)
    {
        GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
    }

    ESP_LOGI(TAG, "ASIC Job Interval: %.2f ms", GLOBAL_STATE->asic_job_frequency_ms);
    SYSTEM_notify_mining_started(GLOBAL_STATE);
    ESP_LOGI(TAG, "ASIC Ready!");
}
