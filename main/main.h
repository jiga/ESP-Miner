#ifndef MAIN_H_
#define MAIN_H_

#include "connect.h"
#include "freertos/event_groups.h"

/* Generic bit definitions. */
#define POOL_FAIL		( 0x01UL )
#define ASIC_FAIL		( 0x02UL )
#define WIFI_FAIL		( 0x04UL )
#define eBIT_3		( 0x08UL )
#define eBIT_4		( 0x10UL )
#define eBIT_5		( 0x20UL )
#define eBIT_6		( 0x40UL )
#define eBIT_7		( 0x80UL )

// Enum for display states
typedef enum {
    MAIN_STATE_INIT,
    MAIN_STATE_NET_CONNECT,
    MAIN_STATE_ASIC_INIT,
    MAIN_STATE_POOL_CONNECT,
    MAIN_STATE_MINING_INIT,
    MAIN_STATE_NORMAL,
} main_state_t;


typedef enum {
    POOL_STRATUM,
    ASIC_NONCE,
} watchdog_feed_t;

void Main_feed_pool_watchdog(watchdog_feed_t);
void Main_feed_asic_watchdog(watchdog_feed_t);
void Main_event(EventBits_t);

#endif /* MAIN_H_ */