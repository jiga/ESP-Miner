#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "i2c_master.h"
#include "asic_result_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "system.h"
#include "nvs_device.h"
#include "stratum_task.h"
#include "self_test.h"
#include "network.h"
#include "display_task.h"
#include "main.h"

#define POOL_WATCHDOG_TIMEOUT_S 60 // 60 seconds
#define ASIC_WATCHDOG_TIMEOUT_S 60 // 60 seconds

// Struct for display state machine
typedef struct {
    main_state_t state;
} main_state_machine_t;

static main_state_machine_t mainStateMachine;

// Declare a variable to hold the created main event group.
EventGroupHandle_t mainEventGroup;

static GlobalState GLOBAL_STATE = {.extranonce_str = NULL, .extranonce_2_len = 0, .abandon_work = 0, .version_mask = 0};

static const char * TAG = "main"; //tag for ESP_LOG

static uint64_t watchdog_pool_last = 0;
static uint64_t watchdog_asic_last = 0;

/* Handles for the tasks create by app_main(). */  
static TaskHandle_t stratum_task_h = NULL; 

static void unblock_task(TaskHandle_t);

void app_main(void)
{
    ESP_LOGI(TAG, "Welcome to the bitaxe - hack the planet!");

    //init I2C
    if (i2c_master_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2C");
        return;
    }

    //initialize the Bitaxe Display
    xTaskCreate(DISPLAY_task, "DISPLAY_task", 4096, (void *) &GLOBAL_STATE, 3, NULL);

    //initialize the ESP32 NVS
    if (NVSDevice_init() != ESP_OK){
        ESP_LOGE(TAG, "Failed to init NVS");
        return;
    }

    //parse the NVS config into GLOBAL_STATE
    if (NVSDevice_parse_config(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse NVS config");
        //show the error on the display
        Display_bad_NVS();
        return;
    }

    //should we run the self test?
    if (should_test(&GLOBAL_STATE)) {
        self_test((void *) &GLOBAL_STATE);
        vTaskDelay(60 * 60 * 1000 / portTICK_PERIOD_MS);
    }

    System_init_system(&GLOBAL_STATE);

    xTaskCreate(POWER_MANAGEMENT_task, "power mangement", 8192, (void *) &GLOBAL_STATE, 10, NULL);

    xTaskCreate(stratum_task, "stratum task", 8192, (void *) &GLOBAL_STATE, 5, &stratum_task_h);

    // Initialize the main state machine
    mainStateMachine.state = MAIN_STATE_INIT;

    /* Attempt to create the event group. */
    mainEventGroup = xEventGroupCreate();
    EventBits_t eventBits;
    if (mainEventGroup == NULL) {
        ESP_LOGE(TAG, "Main Event group creation failed");
        return;
    }

    EventBits_t result_bits;
    uint8_t s_retry_num = 0;

    while(1) {

        switch (mainStateMachine.state) {
            case MAIN_STATE_INIT:
                mainStateMachine.state = MAIN_STATE_NET_CONNECT;
                break;

            case MAIN_STATE_NET_CONNECT:
                Display_change_state(DISPLAY_STATE_NET_CONNECT); //Change display state

                result_bits = Network_connect(&GLOBAL_STATE);

                if (result_bits & WIFI_CONNECTED_BIT) {
                    ESP_LOGI(TAG, "Connected to SSID: %s", GLOBAL_STATE.SYSTEM_MODULE.ssid);
                    s_retry_num = 0;
                    SYSTEM_set_wifi_status(&GLOBAL_STATE, WIFI_CONNECTED, s_retry_num);
                    mainStateMachine.state = MAIN_STATE_ASIC_INIT;
                    Network_AP_off();
                } else if (result_bits & WIFI_FAIL_BIT) {
                    ESP_LOGE(TAG, "Failed to connect to SSID: %s", GLOBAL_STATE.SYSTEM_MODULE.ssid);
                    s_retry_num++;
                    SYSTEM_set_wifi_status(&GLOBAL_STATE, WIFI_RETRYING, s_retry_num);
                    // User might be trying to configure with AP, just chill here
                    ESP_LOGI(TAG, "Finished, waiting for user input.");
                    //wait 1 second
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                } else {
                    ESP_LOGE(TAG, "UNEXPECTED EVENT");
                    SYSTEM_set_wifi_status(&GLOBAL_STATE, WIFI_CONNECT_FAILED, s_retry_num);
                    // User might be trying to configure with AP, just chill here
                    ESP_LOGI(TAG, "Finished, waiting for user input.");
                    //wait 1 second
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                }
                break;

            case MAIN_STATE_ASIC_INIT:
                Display_change_state(DISPLAY_STATE_ASIC_INIT); //Change display state

                //initialize the stratum queues
                queue_init(&GLOBAL_STATE.stratum_queue);
                queue_init(&GLOBAL_STATE.ASIC_jobs_queue);

                //init serial ports and buffers for ASIC communications
                SERIAL_init();
                //call the ASIC init function (pointer)
                (*GLOBAL_STATE.ASIC_functions.init_fn)(GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value, GLOBAL_STATE.asic_count);
                SERIAL_set_baud((*GLOBAL_STATE.ASIC_functions.set_max_baud_fn)());
                SERIAL_clear_buffer();

                mainStateMachine.state = MAIN_STATE_POOL_CONNECT;
                break;

            case MAIN_STATE_POOL_CONNECT:
                Display_change_state(DISPLAY_STATE_POOL_CONNECT); //Change display state

                //try to connect to open the socket and connect to the pool
                if (Stratum_socket_connect(&GLOBAL_STATE) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to connect to stratum server");
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    mainStateMachine.state = MAIN_STATE_POOL_CONNECT;
                    break;
                }

                unblock_task(stratum_task_h);

                mainStateMachine.state = MAIN_STATE_MINING_INIT;
                break;

            case MAIN_STATE_MINING_INIT:
                Display_change_state(DISPLAY_STATE_NORMAL); //Change display state

                xTaskCreate(create_jobs_task, "create jobs task", 8192, (void *) &GLOBAL_STATE, 10, NULL);
                xTaskCreate(ASIC_task, "asic task", 8192, (void *) &GLOBAL_STATE, 10, NULL);
                xTaskCreate(ASIC_result_task, "asic result task", 8192, (void *) &GLOBAL_STATE, 15, NULL);
                mainStateMachine.state = MAIN_STATE_NORMAL;
                break;

            case MAIN_STATE_NORMAL:
                //wait here for an event or a timeout
                eventBits = xEventGroupWaitBits(mainEventGroup,   
                        POOL_FAIL | ASIC_FAIL, //events to wait for
                        pdTRUE, pdFALSE,
                        10000 / portTICK_PERIOD_MS); // timeout

                if (eventBits & POOL_FAIL) {
                    ESP_LOGE(TAG, "POOL_FAIL event detected");
                    mainStateMachine.state = MAIN_STATE_POOL_CONNECT;
                } else if (eventBits & ASIC_FAIL) {
                    ESP_LOGE(TAG, "ASIC_FAIL event detected");
                    mainStateMachine.state = MAIN_STATE_ASIC_INIT;
                } else {
                    //no events, continue normal operation
                    //check the watchdogs
                    uint64_t now = esp_timer_get_time();

                    uint16_t pool_staleness_s = (now - watchdog_pool_last) / 1000000;
                    uint16_t asic_staleness_s = (now - watchdog_asic_last) / 1000000;

                    if (pool_staleness_s > POOL_WATCHDOG_TIMEOUT_S) {
                        ESP_LOGE(TAG, "POOL_WATCHDOG_TIMEOUT: %d seconds", pool_staleness_s);
                        Main_event(POOL_FAIL);
                    }
                    if (asic_staleness_s > ASIC_WATCHDOG_TIMEOUT_S) {
                        ESP_LOGE(TAG, "ASIC_WATCHDOG_TIMEOUT: %d seconds", asic_staleness_s);
                        Main_event(ASIC_FAIL);
                    }
                }
                break;

            default:
                break;
        }
    }
}

static void unblock_task(TaskHandle_t task_handle) {
    xTaskNotifyGive(task_handle);  
}

void Main_event(EventBits_t type) {
    xEventGroupSetBits(mainEventGroup, type);
}

void Main_feed_pool_watchdog(watchdog_feed_t feed_type) {

    ESP_LOGI(TAG, "Feeding the pool watchdog: %d", feed_type);
    //feed the watchdog;
    watchdog_pool_last = esp_timer_get_time();
}

void Main_feed_asic_watchdog(watchdog_feed_t feed_type) {

    ESP_LOGI(TAG, "Feeding the asic watchdog: %d", feed_type);
    //feed the watchdog;
    watchdog_asic_last = esp_timer_get_time();
}


