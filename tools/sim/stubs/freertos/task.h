#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
static inline void vTaskDelay(TickType_t t) { (void)t; }
static inline int xTaskCreate(TaskFunction_t f, const char *n, uint32_t s, void *a, int p, void *h)
{ (void)f; (void)n; (void)s; (void)a; (void)p; (void)h; return 1; }
