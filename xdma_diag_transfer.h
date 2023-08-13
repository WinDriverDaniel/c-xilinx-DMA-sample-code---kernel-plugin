/* Jungo Connectivity Confidential. Copyright (c) 2023 Jungo Connectivity Ltd.  https://www.jungo.com */

/****************************************************************************
*  File: xdma_diag_transfer.h
*
*  Header for common functions for user-mode diagnostics application for
*  accessing Xilinx PCI Express cards with XDMA support, using the WinDriver
*  WDC API.
*
*  Note: This code sample is provided AS-IS and as a guiding sample only.
*****************************************************************************/

#ifndef _XDMA_DIAG_TRANSFER_H_
#define _XDMA_DIAG_TRANSFER_H_

#include <stdio.h>
#include "wdc_lib.h"
#include "utils.h"
#include "status_strings.h"
#include "diag_lib.h"
#include "wdc_diag_lib.h"
#include "xdma_lib.h"

#if defined(UNIX)
    #include <sys/time.h>
    typedef struct timeval TIME_TYPE;
#else
    typedef LARGE_INTEGER TIME_TYPE;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* DMA performance menu options */
enum {
    MENU_DMA_PERF_TO_DEV = 1,
    MENU_DMA_PERF_FROM_DEV,
    MENU_DMA_PERF_BIDIR,
    MENU_DMA_PERF_EXIT = DIAG_EXIT_MENU
};

/* DMA performance common functions */
void DmaPerformanceBiDir(WDC_DEVICE_HANDLE hDev, DWORD dwBytes,
    BOOL fPolling, DWORD dwSeconds, BOOL fIsTransaction);
void DmaPerformanceSingleDir(WDC_DEVICE_HANDLE hDev, DWORD dwBytes,
    BOOL fPolling, DWORD dwSeconds, DWORD fToDevice, BOOL fIsTransaction);
void XDMA_DIAG_DmaPerformance(WDC_DEVICE_HANDLE hDev, DWORD dwOption,
    DWORD dwBytes, BOOL fPolling, DWORD dwSeconds, BOOL fIsTransaction);
void XDMA_DIAG_DumpDmaBuffer(XDMA_DMA_HANDLE hDma);

/* DMA transfer common functions */
XDMA_DMA_HANDLE XDMA_DIAG_DmaOpen(WDC_DEVICE_HANDLE hDev, BOOL fPolling,
    DWORD dwChannel, BOOL fToDevice, UINT32 u32Pattern, DWORD dwNumPackets,
    UINT64 u64FPGAOffset, BOOL fIsTransaction);
DWORD XDMA_DIAG_DmaTransferStart(XDMA_DMA_HANDLE hDma, HANDLE hOsEvent,
    BOOL fPolling, BOOL fIsTransaction);
void XDMA_DIAG_DmaClose(WDC_DEVICE_HANDLE hDev, XDMA_DMA_HANDLE hDma);
DWORD XDMA_DIAG_DmaTransactionExecute(XDMA_DMA_HANDLE hDma, BOOL fPolling);

#ifdef __cplusplus
}
#endif /* C */

#endif /* _XDMA_DIAG_TRANSFER_H_ */
