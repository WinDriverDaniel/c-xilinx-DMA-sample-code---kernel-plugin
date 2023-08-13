/* Jungo Connectivity Confidential. Copyright (c) 2023 Jungo Connectivity Ltd.  https://www.jungo.com */

/***************************************************************************
*  File: kp_xdma.c
*
*  Sample Kernel PlugIn driver for accessing Xilinx PCI Express cards with
*  XDMA design, using the WinDriver WDC API.
*
*  Note: This code sample is provided AS-IS and as a guiding sample only.
****************************************************************************/

#include "kpstdlib.h"
#include "wd_kp.h"
#include "wdc_defs.h"
#include "../xdma_lib.h"

/*************************************************************
  Functions prototypes
 *************************************************************/
BOOL __cdecl KP_XDMA_Open(KP_OPEN_CALL *kpOpenCall, HANDLE hWD, PVOID pOpenData,
    PVOID *ppDrvContext);
void __cdecl KP_XDMA_Close(PVOID pDrvContext);
void __cdecl KP_XDMA_Call(PVOID pDrvContext, WD_KERNEL_PLUGIN_CALL *kpCall);
BOOL __cdecl KP_XDMA_IntEnable(PVOID pDrvContext, WD_KERNEL_PLUGIN_CALL *kpCall,
    PVOID *ppIntContext);
void __cdecl KP_XDMA_IntDisable(PVOID pIntContext);
BOOL __cdecl KP_XDMA_IntAtIrql(PVOID pIntContext, BOOL *pfIsMyInterrupt);
DWORD __cdecl KP_XDMA_IntAtDpc(PVOID pIntContext, DWORD dwCount);
BOOL __cdecl KP_XDMA_IntAtIrqlMSI(PVOID pIntContext, ULONG dwLastMessage,
    DWORD dwReserved);
DWORD __cdecl KP_XDMA_IntAtDpcMSI(PVOID pIntContext, DWORD dwCount,
    ULONG dwLastMessage, DWORD dwReserved);
BOOL __cdecl KP_XDMA_Event(PVOID pDrvContext, WD_EVENT *wd_event);
static void KP_XDMA_Err(const CHAR *sFormat, ...);
static void KP_XDMA_Trace(const CHAR *sFormat, ...);

/*************************************************************
  Functions implementation
 *************************************************************/

/* KP_Init is called when the Kernel PlugIn driver is loaded.
   This function sets the name of the Kernel PlugIn driver and the driver's
   open callback function. */
BOOL __cdecl KP_Init(KP_INIT *kpInit)
{
    /* Verify that the version of the WinDriver Kernel PlugIn library
       is identical to that of the windrvr.h and wd_kp.h files */
    if (WD_VER != kpInit->dwVerWD)
    {
        /* Re-build your Kernel PlugIn driver project with the compatible
           version of the WinDriver Kernel PlugIn library (kp_nt<version>.lib)
           and windrvr.h and wd_kp.h files */

        return FALSE;
    }

    /* In this sample funcOpen and funcOpen_32_64 are identical */
    kpInit->funcOpen = KP_XDMA_Open;
    kpInit->funcOpen_32_64 = KP_XDMA_Open;
#if defined(WINNT)
    strcpy(kpInit->cDriverName, KP_XDMA_DRIVER_NAME);
#else
    strncpy(kpInit->cDriverName, KP_XDMA_DRIVER_NAME,
        sizeof(kpInit->cDriverName));
#endif
    kpInit->cDriverName[sizeof(kpInit->cDriverName) - 1] = 0;

    return TRUE;
}

/* KP_XDMA_Open is called when WD_KernelPlugInOpen() is called from the user
   mode application to open a handle Kernel PlugIn.
   In a scenario where a 32-bit user mode application calls a 64-bit Kernel
   PlugIn, in this specific sample, no user-space data is copied to the
   kernel-space, so there is no need to adjust 32bit data (pointers) to 64bit,
   etc, thus funcOpen_32_64 is mapped the same as funcOpen.
   pDrvContext will be passed to the rest of the Kernel PlugIn callback
   functions (even though it is not used). */
BOOL __cdecl KP_XDMA_Open(KP_OPEN_CALL *kpOpenCall, HANDLE hWD,
    PVOID pOpenData, PVOID *ppDrvContext)
{
    /* Initialize the XDMA library */
    if (WD_STATUS_SUCCESS != XDMA_LibInit(NULL))
    {
        KP_XDMA_Err("KP_XDMA_Open: Failed to initialize the XDMA library. "
            "Error [%s]", XDMA_GetLastErr());
        return FALSE;
    }

    KP_XDMA_Trace("KP_XDMA_Open: Entered. XDMA library initialized.\n");

    kpOpenCall->funcClose = KP_XDMA_Close;
    kpOpenCall->funcCall = KP_XDMA_Call;
    kpOpenCall->funcIntEnable = KP_XDMA_IntEnable;
    kpOpenCall->funcIntDisable = KP_XDMA_IntDisable;
    kpOpenCall->funcIntAtIrql = KP_XDMA_IntAtIrql;
    kpOpenCall->funcIntAtDpc = KP_XDMA_IntAtDpc;
    kpOpenCall->funcIntAtIrqlMSI = KP_XDMA_IntAtIrqlMSI;
    kpOpenCall->funcIntAtDpcMSI = KP_XDMA_IntAtDpcMSI;
    kpOpenCall->funcEvent = KP_XDMA_Event;

    /* In this sample the driver context is not used. */
    *ppDrvContext = NULL;

    KP_XDMA_Trace("KP_XDMA_Open: Kernel PlugIn driver opened successfully\n");

    return TRUE;
}

/* KP_XDMA_Close is called when WD_KernelPlugInClose() is called from the
   user mode */
void __cdecl KP_XDMA_Close(PVOID pDrvContext)
{
    KP_XDMA_Trace("KP_XDMA_Close entered\n");

    /* Uninit the XDMA library */
    if (WD_STATUS_SUCCESS != XDMA_LibUninit())
    {
        KP_XDMA_Err("KP_XDMA_Close: Failed to uninit the XDMA library: %s",
            XDMA_GetLastErr());
    }
}

/* KP_XDMA_Call is called when WD_KernelPlugInCall() is called from the
   user mode */
void __cdecl KP_XDMA_Call(PVOID pDrvContext, WD_KERNEL_PLUGIN_CALL *kpCall)
{
    KP_XDMA_Trace("KP_XDMA_Call: Entered. Message [0x%lx]\n", kpCall->dwMessage);

    kpCall->dwResult = KP_XDMA_STATUS_OK;

    switch (kpCall->dwMessage)
    {
    case KP_XDMA_MSG_VERSION: /* Get the version of the Kernel PlugIn */
        {
            KP_XDMA_VERSION *pUserKPVer = (KP_XDMA_VERSION *)(kpCall->pData);
            KP_XDMA_VERSION kernelKPVer;

            BZERO(kernelKPVer);
            kernelKPVer.dwVer = 100;
#define DRIVER_VER_STR "My Driver V1.00"
            memcpy(kernelKPVer.cVer, DRIVER_VER_STR, sizeof(DRIVER_VER_STR));
            COPY_TO_USER(pUserKPVer, &kernelKPVer, sizeof(KP_XDMA_VERSION));
            kpCall->dwResult = KP_XDMA_STATUS_OK;
        }
        break;

    default:
        kpCall->dwResult = KP_XDMA_STATUS_MSG_NO_IMPL;
    }

    /* NOTE: You can modify the messages above and/or add your own
             Kernel PlugIn messages.
             When changing/adding messages, be sure to also update the
             messages definitions in ../xdma_lib.h. */
}

/* KP_XDMA_IntEnable is called when WD_IntEnable() is called from the user
   mode with a Kernel PlugIn handle. The interrupt context (pIntContext) will
   be passed to the rest of the Kernel PlugIn interrupt functions.
   The function returns TRUE if interrupts are enabled successfully. */
BOOL __cdecl KP_XDMA_IntEnable(PVOID pDrvContext, WD_KERNEL_PLUGIN_CALL *kpCall,
    PVOID *ppIntContext)
{
    KP_XDMA_Trace("KP_XDMA_IntEnable: Entered\n");

    /* You can allocate specific memory for each interrupt in *ppIntContext */

    /* In this sample the interrupt context in not used */
    *ppIntContext = NULL;

    /* TODO: You can add code here to write to the device in order
             to physically enable the hardware interrupts */

    return TRUE;
}

/* KP_XDMA_IntDisable is called when WD_IntDisable() is called from the user
   mode with a Kernel PlugIn handle */
void __cdecl KP_XDMA_IntDisable(PVOID pIntContext)
{
    /* Free any memory allocated in KP_XDMA_IntEnable() here */
}

/* KP_XDMA_IntAtIrql returns TRUE if deferred interrupt processing (DPC) for
   level-sensitive interrupt is required.
   The function is called at HIGH IRQL - at physical interrupt handler.
   Most library calls are NOT allowed at this level, for example:
   NO   WDC_xxx() or WD_xxx calls, apart from the WDC read/write address or
        register functions, WDC_MultiTransfer(), WD_Transfer(),
        WD_MultiTransfer() or WD_DebugAdd().
   NO   malloc().
   NO   free().
   YES  WDC read/write address or configuration space functions,
        WDC_MultiTransfer(), WD_Transfer(), WD_MultiTransfer() or
        WD_DebugAdd(), or wrapper functions that call these functions.
   YES  specific kernel OS functions (such as WinDDK functions) that can
        be called from HIGH IRQL. [Note that the use of such functions may
        break the code's portability to other OSs.] */
BOOL __cdecl KP_XDMA_IntAtIrql(PVOID pIntContext, BOOL *pfIsMyInterrupt)
{
    /* This specific sample is designed to demonstrate Message-Signaled
       Interrupts (MSI) only! Using the sample as-is on an OS that cannot
       enable MSIs will cause the OS to HANG when an interrupt occurs! */

    /* If the data read from the hardware indicates that the interrupt belongs
       to you, you must set *pfIsMyInterrupt to TRUE;
       otherwise, set it to FALSE (this will allow ISRs of other drivers to be
       invoked). */
    *pfIsMyInterrupt = FALSE;
    return FALSE;
}

/* KP_XDMA_IntAtDpc is a Deferred Procedure Call for additional
   level-sensitive interrupt processing. This function is called if
   KP_XDMA_IntAtIrql returned TRUE. KP_XDMA_IntAtDpc returns the number of
   times to notify the user mode of the interrupt (i.e., return from
   WD_IntWait).
 */
DWORD __cdecl KP_XDMA_IntAtDpc(PVOID pIntContext, DWORD dwCount)
{
    return dwCount;
}

/* KP_XDMA_IntAtIrqlMSI returns TRUE if deferred interrupt processing (DPC)
   for Message-Signaled Interrupts (MSI) or Extended Message-Signaled Interrupts
   (MSI-X) is required.
   The function is called at HIGH IRQL - at physical interrupt handler.
   Note: Do not use the dwReserved parameter.
   Most library calls are NOT allowed at this level, for example:
   NO   WDC_xxx() or WD_xxx calls, apart from the WDC read/write address or
        register functions, WDC_MultiTransfer(), WD_Transfer(),
        WD_MultiTransfer() or WD_DebugAdd().
   NO   malloc().
   NO   free().
   YES  WDC read/write address or configuration space functions,
        WDC_MultiTransfer(), WD_Transfer(), WD_MultiTransfer() or
        WD_DebugAdd(), or wrapper functions that call these functions.
   YES  specific kernel OS functions (such as WinDDK functions) that can
        be called from HIGH IRQL. [Note that the use of such functions may
        break the code's portability to other OSs.] */
BOOL __cdecl KP_XDMA_IntAtIrqlMSI(PVOID pIntContext, ULONG dwLastMessage,
    DWORD dwReserved)
{
    /* There is no need to acknowledge MSI/MSI-X. However, you can implement
       the same functionality here as done in the KP_XDMA_IntAtIrql handler
       to read/write data from/to registers at HIGH IRQL. */
    return TRUE;
}

/* KP_XDMA_IntAtDpcMSI is a Deferred Procedure Call for additional
   Message-Signaled Interrupts (MSI) or Extended Message-Signaled Interrupts
   (MSI-X) processing.
   This function is called if KP_XDMA_IntAtIrqlMSI returned TRUE.
   KP_XDMA_IntAtDpcMSI returns the number of times to notify the user mode of
   the interrupt (i.e. return from WD_IntWait). */
DWORD __cdecl KP_XDMA_IntAtDpcMSI(PVOID pIntContext, DWORD dwCount,
    ULONG dwLastMessage, DWORD dwReserved)
{
    return dwCount;
}

/* KP_XDMA_Event is called when a Plug-and-Play/power management event for
   the device is received, if EventRegister() was first called from the
   user mode with the Kernel PlugIn handle. */
BOOL __cdecl KP_XDMA_Event(PVOID pDrvContext, WD_EVENT *wd_event)
{
    return TRUE; /* Return TRUE to notify the user mode of the event */
}

/* -----------------------------------------------
    Debugging and error handling
   ----------------------------------------------- */
static void KP_XDMA_Err(const CHAR *sFormat, ...)
{
#if defined(DEBUG)
    CHAR sMsg[256];
    va_list argp;

    va_start(argp, sFormat);
    vsnprintf(sMsg, sizeof(sMsg) - 1, sFormat, argp);
    WDC_Err("%s: %s", KP_XDMA_DRIVER_NAME, sMsg);
    va_end(argp);
#endif
}

static void KP_XDMA_Trace(const CHAR *sFormat, ...)
{
#if defined(DEBUG)
    CHAR sMsg[256];
    va_list argp;

    va_start(argp, sFormat);
    vsnprintf(sMsg, sizeof(sMsg) - 1, sFormat, argp);
    WDC_Trace("%s: %s", KP_XDMA_DRIVER_NAME, sMsg);
    va_end(argp);
#endif
}

