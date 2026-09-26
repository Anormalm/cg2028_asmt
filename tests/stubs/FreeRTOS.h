/* Single-threaded recorder tests only; production uses FreeRTOS critical sections. */
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
