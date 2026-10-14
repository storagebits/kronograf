/*
 * Network toolbox runners, shared by the watch and the simulator: each tool fills a
 * kg_tool_state_t that the UI polls. The platform supplies a lock, the Wi-Fi link and a
 * cancel flag; LAN (ARP) and Bluetooth scans are platform-specific and live in the HAL.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KG_TOOL_NONE, KG_TOOL_PING, KG_TOOL_TRACE, KG_TOOL_DNS, KG_TOOL_PORTS, KG_TOOL_LAN,
    KG_TOOL_SPEED, KG_TOOL_PUBLIC, KG_TOOL_BLE,
} kg_tool_t;

/* Row colour in the UI. */
enum { KG_ROW_NORMAL, KG_ROW_GOOD, KG_ROW_WARN, KG_ROW_DIM };

#define KG_TOOL_ROWS 32
#define KG_PING_HIST 48
#define KG_SPEED_HIST 40
typedef struct {
    char a[24], b[32];           /* left / right columns, ready to display */
    int32_t v;                   /* sort key or value (RTT µs, RSSI…) */
    uint8_t kind;                /* KG_ROW_* */
} kg_tool_row_t;

typedef struct {
    kg_tool_t tool;
    bool running;
    bool failed;                 /* `status` is an error */
    char status[48];             /* "Connecting to Wi-Fi…", "Resolving…", summary or error */
    char target[48];
    uint32_t ip;                 /* resolved target */
    uint16_t n;                  /* rows used */
    kg_tool_row_t rows[KG_TOOL_ROWS];
    /* ping */
    uint16_t sent, recv;
    uint32_t min_us, max_us, sum_us, last_us;
    int32_t hist[KG_PING_HIST];  /* RTT µs, most recent last, -1 = lost */
    uint16_t hist_n;
    /* speed test */
    uint32_t bytes, elapsed_ms, ttfb_ms;
    float mbps, peak_mbps;
    float speed_hist[KG_SPEED_HIST];
    uint16_t speed_n;
    uint32_t seq;                /* +1 on every change */
} kg_tool_state_t;

typedef struct {
    void (*lock)(void);
    void (*unlock)(void);
    bool (*link_up)(void);       /* Wi-Fi associated with an address */
    uint32_t (*gateway)(void);
    uint32_t (*dns)(void);
    volatile bool *cancel;
} kg_tools_env_t;

/* Runs PING, TRACE, DNS, PORTS, SPEED or PUBLIC to completion (or cancel), updating `st`
 * under the lock. `target` "" or "gateway" means the default gateway. */
void kg_tool_run(kg_tool_t tool, const char *target, kg_tool_state_t *st, const kg_tools_env_t *env);

/* Common helpers for the platform scans. */
void kg_tool_begin(kg_tool_state_t *st, kg_tool_t tool, const char *target);
bool kg_tool_wait_link(kg_tool_state_t *st, const kg_tools_env_t *env);
void kg_tool_status(kg_tool_state_t *st, const kg_tools_env_t *env, bool failed, const char *fmt, ...);
/* Inserts a row keeping rows sorted by descending v (sorted = true) or appends it. */
void kg_tool_add_row(kg_tool_state_t *st, const kg_tools_env_t *env, const kg_tool_row_t *row, bool sorted);
void kg_tool_end(kg_tool_state_t *st, const kg_tools_env_t *env);

/* Formats an RTT: "0.8 ms", "12.4 ms", "143 ms". */
char *kg_fmt_rtt(uint32_t us, char *out, int n);
/* Bluetooth SIG company name ("Apple"…), NULL if unknown. */
const char *kg_ble_company(uint16_t id);
/* "a4:83:e7:12:34:56" (out: 18 bytes). */
char *kg_fmt_mac(const uint8_t mac[6], char *out);

#ifdef __cplusplus
}
#endif
