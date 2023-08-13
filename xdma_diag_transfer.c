/* Jungo Connectivity Confidential. Copyright (c) 2023 Jungo Connectivity Ltd.  https://www.jungo.com */

/****************************************************************************
*  File: xdma_diag_transfer.c
*
*  Common functions for user-mode diagnostics application for accessing
*  Xilinx PCI Express cards with XDMA support, using the WinDriver WDC API.
*
*  Note: This code sample is provided AS-IS and as a guiding sample only.
*****************************************************************************/

#include "xdma_diag_transfer.h"

int XDMA_printf(char *fmt, ...)
#if defined(LINUX)
    __attribute__ ((format (printf, 1, 2)))
#endif
    ;


#define XDMA_OUT XDMA_printf
#define XDMA_ERR XDMA_printf

/* Interrupt handler routine for DMA performance testing */
void DiagXdmaDmaPerfIntHandler(WDC_DEVICE_HANDLE hDev,
    XDMA_INT_RESULT *pIntResult)
{
    DWORD dwStatus = OsEventSignal((HANDLE)pIntResult->pData);

    if (dwStatus != WD_STATUS_SUCCESS)
    {
        XDMA_ERR("Failed signalling DMA completion. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
    }

    UNUSED_VAR(hDev);
}

typedef struct {
    WDC_DEVICE_HANDLE hDev;
    XDMA_DMA_HANDLE hDma;
    DWORD dwBytes;
    BOOL fPolling;
    BOOL fToDevice;
    DWORD dwSeconds;
    HANDLE hOsEvent;
    BOOL fIsTransaction;
} DMA_PERF_THREAD_CTX;

void DmaPerfDevThread(void *pData)
{
    DMA_PERF_THREAD_CTX *ctx = (DMA_PERF_THREAD_CTX *)pData;
    TIME_TYPE time_start, time_end_temp;
    DWORD dwStatus = 0, restarts = 0;
    UINT64 u64BytesTransferred = 0;
    double time_elapsed = 0;

    get_cur_time(&time_start);
    while (time_elapsed < ctx->dwSeconds * 1000)
    {
        if (ctx->fIsTransaction)
            XDMA_DmaTransactionExecute(ctx->hDma, FALSE, NULL);

        dwStatus = XDMA_DmaTransferStart(ctx->hDma);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            XDMA_ERR("\nFailed starting DMA transfer. Error 0x%x - %s\n",
                dwStatus, Stat2Str(dwStatus));
            break;
        }

        if (ctx->fPolling)
        {
            dwStatus = XDMA_DmaPollCompletion(ctx->hDma);
            if (dwStatus != WD_STATUS_SUCCESS)
            {
                XDMA_ERR("\nFailed polling for DMA completion. "
                    "Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
                break;
            }
        }
        else
        {
            dwStatus = OsEventWait(ctx->hOsEvent, 1);
            if (dwStatus == WD_TIME_OUT_EXPIRED)
            {
#define MAX_RESTARTS 2
                /* In case of timeout try to restart the test because timeout
                 * may occur because of a missed interrupt */
                if (restarts++ >= MAX_RESTARTS)
                {
                    XDMA_ERR("Timeout occurred\n");
                    break;
                }
                XDMA_DmaTransferStop(ctx->hDma);
                time_elapsed = 0;
                u64BytesTransferred = 0;
                get_cur_time(&time_start);
                continue;
            }
            else if (dwStatus != WD_STATUS_SUCCESS)
            {
                XDMA_ERR("\nFailed waiting for completion event. "
                    "Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
                break;
            }
            if (ctx->fIsTransaction)
            {
                dwStatus = XDMA_DmaTransactionTransferEnded(ctx->hDma);
                if (dwStatus == WD_STATUS_SUCCESS)
                    XDMA_DmaTransactionRelease(ctx->hDma);
            }
        }

        u64BytesTransferred += (UINT64)ctx->dwBytes;
        get_cur_time(&time_end_temp);
        time_elapsed = time_diff(&time_end_temp, &time_start);
        if (time_elapsed == -1)
        {
            XDMA_ERR("Performance test failed\n");
            return;
        }
    }
     if (!time_elapsed)
    {
        XDMA_OUT("DMA %s performance test failed\n",
                ctx->fToDevice ? "host-to-device" : "device-to-host");
        return;
    }

    XDMA_OUT("\n\n");

    DIAG_PrintPerformance(u64BytesTransferred, &time_start);
}

HANDLE DmaPerformanceThreadStart(DMA_PERF_THREAD_CTX *ctx)
{
    DWORD dwStatus;
    HANDLE hThread;

    dwStatus = ThreadStart(&hThread, (HANDLER_FUNC)DmaPerfDevThread, ctx);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        XDMA_ERR("\nFailed starting performance thread. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
        return NULL;
    }

    return hThread;
}

DMA_PERF_THREAD_CTX *DmaPerfThreadInit(WDC_DEVICE_HANDLE hDev,
    DWORD dwBytes, UINT64 u64Offset, BOOL fPolling, DWORD dwSeconds,
    DWORD fToDevice, BOOL fIsTransaction)
{
    DMA_PERF_THREAD_CTX *ctx = NULL;
    DWORD dwStatus;

    ctx = (DMA_PERF_THREAD_CTX *)calloc(1, sizeof(DMA_PERF_THREAD_CTX));
    if (!ctx)
    {
        XDMA_ERR("Memory allocation error\n");
        return NULL;
    }

#ifdef HAS_INTS
    if (!fPolling)
    {
        if (XDMA_IntIsEnabled(hDev))
            XDMA_IntDisable(hDev);

        dwStatus = OsEventCreate(&ctx->hOsEvent);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            XDMA_ERR("\nFailed creating event. Error 0x%x - %s\n",
                dwStatus, Stat2Str(dwStatus));
            goto Error;
        }

        if (!XDMA_IntIsEnabled(hDev))
        {
            dwStatus = XDMA_IntEnable(hDev, DiagXdmaDmaPerfIntHandler);
            if (dwStatus != WD_STATUS_SUCCESS)
            {
                XDMA_ERR("\nFailed enabling interrupts. Error 0x%x - %s\n",
                    dwStatus, Stat2Str(dwStatus));
                goto Error;
            }
        }
    }
#endif /* ifdef HAS_INTS */

    dwStatus = XDMA_DmaOpen(hDev, &ctx->hDma, dwBytes, u64Offset, fToDevice, 0,
        fPolling, FALSE, ctx->hOsEvent, fIsTransaction);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        XDMA_ERR("\nFailed to open DMA handle. Error 0x%x - %s\n", dwStatus,
            Stat2Str(dwStatus));
        goto Error;
    }

    ctx->hDev = hDev;
    ctx->fPolling = fPolling;
    ctx->dwBytes = dwBytes;
    ctx->fToDevice = fToDevice;
    ctx->dwSeconds = dwSeconds;
    ctx->fIsTransaction = fIsTransaction;

    return ctx;

Error:
#ifdef HAS_INTS
    if (!fPolling && XDMA_IntIsEnabled(hDev))
        XDMA_IntDisable(hDev);
    if (ctx->hOsEvent)
        OsEventClose(ctx->hOsEvent);
#endif /* ifdef HAS_INTS */

    free(ctx);
    return NULL;
}

void DmaPerfThreadUninit(DMA_PERF_THREAD_CTX *ctx)
{
    DWORD dwStatus;

    XDMA_DmaTransferStop(ctx->hDma);

    if (!ctx->fPolling)
    {
#ifdef HAS_INTS
        XDMA_IntDisable(ctx->hDev);
        OsEventClose(ctx->hOsEvent);
#endif /* ifdef HAS_INTS */
    }

    dwStatus = XDMA_DmaClose(ctx->hDma);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        XDMA_ERR("\nFailed closing DMA handle. Error 0x%x - %s\n", dwStatus,
            Stat2Str(dwStatus));
    }

    free(ctx);
}

void DmaPerformanceSingleDir(WDC_DEVICE_HANDLE hDev, DWORD dwBytes,
    BOOL fPolling, DWORD dwSeconds, DWORD fToDevice, BOOL fIsTransaction)
{
    HANDLE hThread;
    DMA_PERF_THREAD_CTX *ctx;

    ctx = DmaPerfThreadInit(hDev, dwBytes, 0, fPolling, dwSeconds, fToDevice,
        fIsTransaction);
    if (!ctx)
    {
        XDMA_ERR("Failed initializing performance thread context\n");
        return;
    }

    hThread = DmaPerformanceThreadStart(ctx);
    if (!hThread)
    {
        XDMA_ERR("Failed starting performance thread\n");
        goto Exit;
    }

    ThreadWait(hThread);

Exit:
    DmaPerfThreadUninit(ctx);
}

void DmaPerformanceBiDir(WDC_DEVICE_HANDLE hDev, DWORD dwBytes,
    BOOL fPolling, DWORD dwSeconds, BOOL fIsTransaction)
{
    HANDLE hThreadToDev, hThreadFromDev;
    DMA_PERF_THREAD_CTX *pCtxToDev = NULL, *pCtxFromDev = NULL;

    pCtxToDev = DmaPerfThreadInit(hDev, dwBytes, 0, fPolling, dwSeconds, TRUE,
        fIsTransaction);
    if (!pCtxToDev)
    {
        XDMA_ERR("Failed initializing performance thread context\n");
        return;
    }

    pCtxFromDev = DmaPerfThreadInit(hDev, dwBytes, (UINT64)(dwBytes * 2),
        fPolling, dwSeconds, FALSE, fIsTransaction);
    if (!pCtxFromDev)
    {
        XDMA_ERR("Failed initializing performance thread context\n");
        goto Exit;
    }

    hThreadToDev = DmaPerformanceThreadStart(pCtxToDev);
    if (!hThreadToDev)
        XDMA_ERR("Failed starting DMA host-to-device performance thread\n");

    hThreadFromDev = DmaPerformanceThreadStart(pCtxFromDev);
    if (!hThreadFromDev)
        XDMA_ERR("Failed starting DMA device-to-host performance thread\n");

    if (hThreadToDev)
        ThreadWait(hThreadToDev);
    if (hThreadFromDev)
        ThreadWait(hThreadFromDev);

Exit:
    if (pCtxToDev)
        DmaPerfThreadUninit(pCtxToDev);
    if (pCtxFromDev)
        DmaPerfThreadUninit(pCtxFromDev);
}

void XDMA_DIAG_DmaPerformance(WDC_DEVICE_HANDLE hDev, DWORD dwOption,
    DWORD dwBytes, BOOL fPolling, DWORD dwSeconds, BOOL fIsTransaction)
{
#if 0
    char direction[32];

    /* done separately to allow printing of the complete message before
     * threads are started */
    switch (dwOption)
    {
    case MENU_DMA_PERF_TO_DEV:
        sprintf(direction, "host-to-device");
        break;
    case MENU_DMA_PERF_FROM_DEV:
        sprintf(direction, "device-to-host");
        break;
    case MENU_DMA_PERF_BIDIR:
        sprintf(direction, "bi-directional");
        break;
    }
    XDMA_OUT("\nRunning DMA %s performance test, wait %d seconds "
        "to finish...\n", direction, dwSeconds);
#else
    XDMA_OUT("\nRunning DMA %s performance test, wait %d seconds "
        "to finish...\n",
        dwOption == MENU_DMA_PERF_TO_DEV ? "host-to-device" :
        dwOption == MENU_DMA_PERF_FROM_DEV ? "device-to-host" :
        "bi-directional",
        dwSeconds);
#endif

    switch (dwOption)
    {
    case MENU_DMA_PERF_TO_DEV:
        DmaPerformanceSingleDir(hDev, dwBytes, fPolling, dwSeconds, TRUE,
            fIsTransaction);
        break;
    case MENU_DMA_PERF_FROM_DEV:
        DmaPerformanceSingleDir(hDev, dwBytes, fPolling, dwSeconds, FALSE,
            fIsTransaction);
        break;
    case MENU_DMA_PERF_BIDIR:
        DmaPerformanceBiDir(hDev, dwBytes, fPolling, dwSeconds,
            fIsTransaction);
        break;
    }
}

/* DMA Transfer functions */

static VOID DumpBuffer(UINT32 *buf, DWORD dwBytes)
{
    DWORD i;

    XDMA_OUT("Buffer:\n\n");
    for (i = 0; i < dwBytes / sizeof(UINT32); i++)
    {
        XDMA_OUT("%08x ", buf[i]);
        if (i && !(i % 32))
            XDMA_OUT("\n");
    }
    XDMA_OUT("\n\n");
}

void XDMA_DIAG_DumpDmaBuffer(XDMA_DMA_HANDLE hDma)
{
    PVOID pBuf;
    DWORD dwBytes;

    pBuf = XDMA_DmaBufferGet(hDma, &dwBytes);
    if (!pBuf || !dwBytes)
    {
        XDMA_OUT("Invalid DMA buffer\n");
        return;
    }
    DumpBuffer((UINT32 *)pBuf, dwBytes);
}

/* Interrupt handler routine */
static void DiagXdmaTransferIntHandler(WDC_DEVICE_HANDLE hDev,
    XDMA_INT_RESULT *pIntResult)
{
    UNUSED_VAR(hDev);

    XDMA_OUT("\n###\n%s Interrupt #%d received, DMA status 0x%08x, "
        "interrupt status 0x%08x\n",
        pIntResult->fIsMessageBased ?  "Message Signalled" : "Level Sensitive",
        pIntResult->dwCounter, pIntResult->u32DmaStatus,
        pIntResult->u32IntStatus);

    if (pIntResult->fIsMessageBased)
        XDMA_OUT("MSI data 0x%x\n", pIntResult->dwLastMessage);

    XDMA_OUT("###\n\n");

    if (pIntResult->hDma)
    {
        DWORD dwStatus = OsEventSignal((HANDLE)pIntResult->pData);

        if (dwStatus != WD_STATUS_SUCCESS)
        {
            XDMA_ERR("Failed signalling DMA completion. Error 0x%x - %s\n",
                dwStatus, Stat2Str(dwStatus));
        }
    }
    else
    {
        XDMA_OUT("Error: DMA handle is NULL\n");
    }
}

/* Open DMA handle and perform DMA transfer */
XDMA_DMA_HANDLE XDMA_DIAG_DmaOpen(WDC_DEVICE_HANDLE hDev, BOOL fPolling,
    DWORD dwChannel, BOOL fToDevice, UINT32 u32Pattern, DWORD dwNumPackets,
    UINT64 u64FPGAOffset, BOOL fIsTransaction)
{
    XDMA_DMA_HANDLE hDma = NULL;
    DWORD dwStatus;
    HANDLE hOsEvent = NULL;

#ifdef HAS_INTS
    if (!fPolling)
    {
        if (!fIsTransaction)
        {
            dwStatus = OsEventCreate(&hOsEvent);
            if (dwStatus != WD_STATUS_SUCCESS)
            {
                XDMA_ERR("\nFailed creating event. Error 0x%x - %s\n",
                    dwStatus, Stat2Str(dwStatus));
                return NULL;
            }
        }

        if (!XDMA_IntIsEnabled(hDev))
        {
            dwStatus = XDMA_IntEnable(hDev, DiagXdmaTransferIntHandler);
            if (dwStatus != WD_STATUS_SUCCESS)
            {
                XDMA_ERR("Failed enabling interrupts, (%s)\n",
                    XDMA_GetLastErr());
                goto Exit;
            }
        }
    }
#endif /* ifdef HAS_INTS */

    /* Pass hOsEvent as last parameter, to enable signalling when DMA
     * completion interrupt occurs */
    dwStatus = XDMA_DmaOpen(hDev, &hDma, (dwNumPackets * sizeof(UINT32)),
        u64FPGAOffset, fToDevice, dwChannel, fPolling, FALSE, hOsEvent,
        fIsTransaction);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        XDMA_ERR("\nFailed to open DMA handle. Error 0x%x - %s\n", dwStatus,
            Stat2Str(dwStatus));
        return NULL;
    }

    if (fToDevice)
    {
        DWORD dwBytes;
        DWORD i;
        UINT32 *pu32Buf =
            (UINT32 *)XDMA_DmaBufferGet(hDma, &dwBytes);

        for (i = 0; i < dwNumPackets; i++)
            pu32Buf[i] = u32Pattern;
    }

    if (!fIsTransaction)
    {
        dwStatus = XDMA_DIAG_DmaTransferStart(hDma, hOsEvent, fPolling, FALSE);
        if (dwStatus == WD_STATUS_SUCCESS)
        {
            if (!fToDevice)
                XDMA_DIAG_DumpDmaBuffer(hDma);
            XDMA_OUT("\nDMA %s completed successfully\n",
                fIsTransaction ? "transaction" : "transfer");
        }
    }

#ifdef HAS_INTS
Exit:
    if (!fPolling && !fIsTransaction)
        OsEventClose(hOsEvent);
#endif /* ifdef HAS_INTS */
    return hDma;
}

DWORD XDMA_DIAG_DmaTransactionExecute(XDMA_DMA_HANDLE hDma, BOOL fPolling)
{
    DWORD dwStatus;
    HANDLE hOsEvent = NULL;

    if (!fPolling)
        dwStatus = OsEventCreate(&hOsEvent);

    dwStatus = XDMA_DmaTransactionExecute(hDma, TRUE, hOsEvent);
    if (dwStatus != WD_STATUS_SUCCESS)
        goto Exit;

    do {
        XDMA_DIAG_DmaTransferStart(hDma, hOsEvent, fPolling, TRUE);
        XDMA_OUT("DMA transfer has been finished\n");

        dwStatus = XDMA_DmaTransactionTransferEnded(hDma);
    } while (dwStatus == (DWORD)WD_MORE_PROCESSING_REQUIRED);

Exit:
    XDMA_OUT("DMA transaction %s\n",
        (dwStatus == WD_STATUS_SUCCESS) ? "completed" : "failed");

    if (!fPolling)
        OsEventClose(hOsEvent);

    return dwStatus;
}

DWORD XDMA_DIAG_DmaTransferStart(XDMA_DMA_HANDLE hDma, HANDLE hOsEvent,
    BOOL fPolling, BOOL fIsTransaction)
{
    UNUSED_VAR(fIsTransaction);

    DWORD dwStatus = XDMA_DmaTransferStart(hDma);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        XDMA_ERR("\nFailed starting DMA transfer. Error 0x%x - %s\n", dwStatus,
            Stat2Str(dwStatus));
    }

    if (fPolling)
    {
        dwStatus = XDMA_DmaPollCompletion(hDma);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            XDMA_ERR("\nFailed polling for DMA completion. Error 0x%x - %s\n",
                dwStatus, Stat2Str(dwStatus));
            goto Exit;
        }
    }
    else
    {
        dwStatus = OsEventWait(hOsEvent, 5);
        if (dwStatus == WD_TIME_OUT_EXPIRED)
        {
            XDMA_ERR("\nInterrupt time out. Error 0x%x - %s\n", dwStatus,
                Stat2Str(dwStatus));
            goto Exit;
        }
        else if (dwStatus != WD_STATUS_SUCCESS)
        {
            XDMA_ERR("\nFailed waiting for completion event. "
                "Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
            goto Exit;
        }
    }

Exit:
    return dwStatus;
}

/* Close DMA */
void XDMA_DIAG_DmaClose(WDC_DEVICE_HANDLE hDev, XDMA_DMA_HANDLE hDma)
{
    DWORD dwStatus = XDMA_DmaTransferStop(hDma);

    if (dwStatus != WD_STATUS_SUCCESS)
    {
        XDMA_ERR("\nFailed stopping DMA transfer. Error 0x%x - %s\n", dwStatus,
            Stat2Str(dwStatus));
    }
#ifdef HAS_INTS
    if (XDMA_IntIsEnabled(hDev))
    {
        dwStatus = XDMA_IntDisable(hDev);
        XDMA_OUT("DMA interrupts disable%s\n",
            (WD_STATUS_SUCCESS == dwStatus) ? "d" : " failed");

    }
#endif /* ifdef HAS_INTS */

    dwStatus = XDMA_DmaClose(hDma);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        XDMA_ERR("\nFailed to close DMA handle. Error 0x%x - %s\n", dwStatus,
            Stat2Str(dwStatus));
    }
}
