#include "status_data.h"
#include <stdio.h>

StatusData g_status;
SemaphoreHandle_t g_statusMtx = nullptr;

void statusBegin()
{
    memset((void *)&g_status, 0, sizeof(g_status));
    if (!g_statusMtx)
        g_statusMtx = xSemaphoreCreateMutex();
}

void statusIpToStr(uint32_t ip, char *out8)
{
    if (!ip) { strcpy(out8, "?"); return; }
    snprintf(out8, 8, "%u.%u.%u.%u",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}
