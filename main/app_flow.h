#ifndef APP_FLOW_H
#define APP_FLOW_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start application flow: show login first, then enter main feature after success.
 */
void app_flow_start(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // APP_FLOW_H
