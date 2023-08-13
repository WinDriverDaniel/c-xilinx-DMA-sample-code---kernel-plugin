/* Jungo Connectivity Confidential. Copyright (c) 2023 Jungo Connectivity Ltd.  https://www.jungo.com */

/****************************************************************************
*  File: xdma_lib.c
*
*  Implementation of a sample library for accessing Xilinx PCI Express cards
*  with XDMA design, using the WinDriver WDC API.
*
*  Note: This code sample is provided AS-IS and as a guiding sample only.
*****************************************************************************/

#if defined(__KERNEL__)
    #include "kpstdlib.h"
#endif
#include "utils.h"
#include "status_strings.h"
#include "xdma_lib.h"

/*************************************************************
  Internal definitions
 *************************************************************/
/* WinDriver license registration string */
/* TODO: When using a registered WinDriver version, replace the license string
         below with the development license in order to use on the development
         machine.
         Once you require to distribute the driver's package to other machines,
         please replace the string with a distribution license */
#define XDMA_DEFAULT_LICENSE_STRING "12345abcde12345.abcde"

#define XDMA_DEFAULT_DRIVER_NAME WD_DEFAULT_DRIVER_NAME_BASE

#define DMA_ADDR_LOW(addr)   ((UINT32)((addr) & 0xffffffff))
#define DMA_ADDR_HIGH(addr)  ((UINT32)((addr) >> 32))

#define XDMA_TRANSACTION_SAMPLE_MAX_TRANSFER_SIZE 0x00FFFFFF


typedef struct {
#define XDMA_DESC_MAGIC   0xAD4B0000
#define XDMA_MAX_ADJACENT 15
    UINT32 u32Control;
    UINT32 u32Bytes;    /* Transfer length in bytes */
    UINT64 u64SrcAddr;  /* Source address */
    UINT64 u64DstAddr;  /* Destination address */
    UINT64 u64NextDesc; /* Next descriptor address */
} XDMA_DMA_DESC;

typedef struct {
    UINT32 u32CompletedDescs; /* Completed descriptors count */
    UINT32 Reserved[7];
} XDMA_DMA_POLL_WB;

#define ENGINE_IDX(dwChannel, fToDevice) \
    (fToDevice ? dwChannel : dwChannel + XDMA_CHANNELS_NUM)

/* Last error information string */
static CHAR gsXDMA_LastErr[256];

/*************************************************************
  Static functions prototypes and inline implementation
 *************************************************************/
#if !defined(__KERNEL__)
static BOOL DeviceValidate(const PWDC_DEVICE pDev);
#endif
static void DLLCALLCONV XDMA_IntHandler(PVOID pData);
static void XDMA_EventHandler(WD_EVENT *pEvent, PVOID pData);
static void ErrLog(const CHAR *sFormat, ...);
static void TraceLog(const CHAR *sFormat, ...);

#if !defined(__KERNEL__)
/* Allocate buffer with page aligned address */
static void *__valloc(unsigned long size)
{
#if defined(WIN32)
    UPTR p = 0, buf, page_size;

    page_size = GetPageSize();
    buf = (UPTR)malloc(size + page_size);
    if (buf)
    {
        p = __ALIGN_DOWN(buf + page_size, page_size);
        *(UPTR *)(p - sizeof(UPTR)) = buf;
    }
    return (void *)p;
#else
    return valloc(size);
#endif
}

/* Free buffer with page aligned address */
static void __vfree(void *p)
{
#if defined(WIN32)
    UPTR buf = *(UPTR *)((UPTR)p - sizeof(UPTR));
    free((void *)buf);
#else
    free(p);
#endif
}
#endif

/* Validate a WDC device handle */
static inline BOOL IsValidDevice(PWDC_DEVICE pDev, const CHAR *sFunc)
{
    if (!pDev || !WDC_GetDevContext(pDev))
    {
        ErrLog("%s: NULL device %s\n", sFunc, !pDev ? "handle" : "context");
        return FALSE;
    }

    return TRUE;
}

/*************************************************************
  Functions implementation
 *************************************************************/
/* -----------------------------------------------
    XDMA and WDC libraries initialize/uninitialize
   ----------------------------------------------- */
/* Initialize the Xilinx XDMA and WDC libraries */
DWORD XDMA_LibInit(const CHAR *sLicense)
{
    DWORD dwStatus;

#if defined(WD_DRIVER_NAME_CHANGE)
    /* Set the driver name */
    if (!WD_DriverName(XDMA_DEFAULT_DRIVER_NAME))
    {
        ErrLog("Failed to set the driver name for WDC library.\n");
        return WD_SYSTEM_INTERNAL_ERROR;
    }
#endif

    /* Set WDC library's debug options
     * (default: level=TRACE; redirect output to the Debug Monitor) */
    dwStatus = WDC_SetDebugOptions(WDC_DBG_DEFAULT, NULL);
    if (WD_STATUS_SUCCESS != dwStatus)
    {
        ErrLog("Failed to initialize debug options for WDC library.\n"
            "Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
        return dwStatus;
    }

    /* Open a handle to the driver and initialize the WDC library */
    dwStatus = WDC_DriverOpen(WDC_DRV_OPEN_DEFAULT,
        sLicense ? sLicense : XDMA_DEFAULT_LICENSE_STRING);
    if (WD_STATUS_SUCCESS != dwStatus)
    {
        ErrLog("Failed to initialize the WDC library. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
        return dwStatus;
    }

    return WD_STATUS_SUCCESS;
}

/* Uninitialize the Xilinx XDMA and WDC libraries */
DWORD XDMA_LibUninit(void)
{
    DWORD dwStatus;

    /* Uninitialize the WDC library and close the handle to WinDriver */
    dwStatus = WDC_DriverClose();
    if (WD_STATUS_SUCCESS != dwStatus)
    {
        ErrLog("Failed to uninit the WDC library. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
    }

    return dwStatus;
}

#if !defined(__KERNEL__)

static DWORD getConfigBar(WDC_DEVICE_HANDLE hDev)
{
    UINT32 i, irqId, configId;
    DWORD dwStatus;

    for (i = 0; i < WDC_DIAG_GetNumAddrSpaces(hDev); i++)
    {
        if (!WDC_AddrSpaceIsActive(hDev, i) ||
            WDC_GET_ADDR_SPACE_SIZE(hDev, i) < (UINT64)XDMA_MIN_CONFIG_BAR_SIZE)
        {
            continue;
        }

        dwStatus = WDC_ReadAddr32(hDev, i, XDMA_IRQ_BLOCK_IDENTIFIER_OFFSET,
            &irqId);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            ErrLog("Failed reading IRQ Block Identifier register\n");
            return (DWORD)-1;
        }

        WDC_ReadAddr32(hDev, i, XDMA_CONFIG_BLOCK_IDENTIFIER_OFFSET,
            &configId);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            ErrLog("Failed reading Config Block Identifier register\n");
            return (DWORD)-1;
        }

        // check if the current BAR is a config BAR
        if ((irqId & XDMA_ID_MASK) == XDMA_ID &&
            (configId & XDMA_ID_MASK) == XDMA_ID)
        {
            return i;
        }
    }

    return (DWORD)-1;
}

/* This function checks if the DMA engine (by dwChannel and fToDevice) exists
 * on the card */
static BOOL EngineExists(WDC_DEVICE_HANDLE hDev, BOOL fToDevice,
    ULONG dwChannel)
{
    PWDC_DEVICE pDev = (PWDC_DEVICE)hDev;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pDev);
    UINT32 u32EngineID = 0;

    WDC_ReadAddr32(hDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(dwChannel, fToDevice ?
            XDMA_H2C_CHANNEL_IDENTIFIER_OFFSET :
            XDMA_C2H_CHANNEL_IDENTIFIER_OFFSET),
        &u32EngineID);

    return (u32EngineID & XDMA_ID_MASK) == XDMA_ID;
}

/* This function prepares the DMA context using the number of active DMA
 * engines */
static void EnginesCreate(WDC_DEVICE_HANDLE hDev)
{
    PWDC_DEVICE pDev = (PWDC_DEVICE)hDev;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pDev);
    XDMA_DMA_STRUCT *pXdmaDma;
    UINT32 i, u32EngineIndex = 0;
    BOOL fToDevice;
    DWORD dwChannel;

    for (i = 0; i < XDMA_CHANNELS_NUM * 2; i++)
    {
        fToDevice = i < XDMA_CHANNELS_NUM;
        dwChannel = i % XDMA_CHANNELS_NUM;

        if (EngineExists(hDev, fToDevice, dwChannel))
        {
            pXdmaDma = &(pDevCtx->pEnginesArr[i]);
            pXdmaDma->u32IrqBitMask = (1 << XDMA_ENG_IRQ_NUM) - 1;
            pXdmaDma->u32IrqBitMask <<= (u32EngineIndex * XDMA_ENG_IRQ_NUM);
            pXdmaDma->fIsEnabled = TRUE;
            u32EngineIndex++;
        }
    }
}

BOOL DeviceInit(WDC_DEVICE_HANDLE hDev)
{
    PXDMA_DEV_CTX pDevCtx;

    if (!hDev)
        return FALSE;

    pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);
    pDevCtx->dwConfigBarNum = getConfigBar(hDev);
    if (pDevCtx->dwConfigBarNum == (DWORD)-1)
    {
        ErrLog("Could not find config BAR, probably not an PCI device\n");
        return FALSE;
    }

    /* Validate device information */
    if (!DeviceValidate((PWDC_DEVICE)hDev))
        return FALSE;

    EnginesCreate(hDev);

    return TRUE;
}
/* -----------------------------------------------
    Device open/close
   ----------------------------------------------- */
/* Open a device handle */
WDC_DEVICE_HANDLE XDMA_DeviceOpen(DWORD dwVendorID, DWORD dwDeviceID)
{
    WDC_DEVICE_HANDLE hDev = WDC_DIAG_DeviceFindAndOpen(dwVendorID,
        dwDeviceID, KP_XDMA_DRIVER_NAME, sizeof(XDMA_DEV_CTX));

    if (!hDev || !DeviceInit(hDev))
        goto Error;

    return hDev;

Error:
    if (hDev)
        XDMA_DeviceClose(hDev);

    printf("XDMA_DeviceOpen: Failed opening PCI device: %s",
         XDMA_GetLastErr());
    return NULL;
}
/* Close a device handle */
BOOL XDMA_DeviceClose(WDC_DEVICE_HANDLE hDev)
{

    TraceLog("XDMA_DeviceClose: Entered. Device handle [0x%p]\n", hDev);

    /* Validate the WDC device handle */
    if (!hDev)
    {
        ErrLog("XDMA_DeviceClose: Error - NULL device handle\n");
        return FALSE;
    }
#ifdef HAS_INTS
    /* Disable interrupts (if enabled) */
    if (XDMA_IntIsEnabled(hDev))
    {
        DWORD dwStatus = XDMA_IntDisable(hDev);
        if (WD_STATUS_SUCCESS != dwStatus)
        {
            ErrLog("Failed disabling interrupts. Error 0x%x - %s\n", dwStatus,
                Stat2Str(dwStatus));
        }
    }
#endif /* ifdef HAS_INTS */

    return WDC_DIAG_DeviceClose(hDev);
}

/* Validate device information */
static BOOL DeviceValidate(const PWDC_DEVICE pDev)
{
    DWORD i, dwNumAddrSpaces = pDev->dwNumAddrSpaces;

    /* TODO: You can modify the implementation of this function in order to
             verify that the device has all expected resources. */

    /* Verify that the device has at least one active address space */
    for (i = 0; i < dwNumAddrSpaces; i++)
    {
        if (WDC_AddrSpaceIsActive(pDev, i))
            return TRUE;
    }

    ErrLog("Device does not have any active memory or I/O address spaces\n");
    return FALSE;
}
#ifdef HAS_INTS
/* -----------------------------------------------
    Interrupts
   ----------------------------------------------- */

static void HandleEngineInterrupt(XDMA_DMA_STRUCT *pXdmaDma, UINT32 val)
{
    PWDC_DEVICE pDev = (PWDC_DEVICE)pXdmaDma->hDev;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pDev);
    XDMA_INT_RESULT intResult;

    BZERO(intResult);
    intResult.u32IntStatus = val;

    if (!pXdmaDma->fToDevice)
        WDC_DMASyncIo(pXdmaDma->pDma);

    XDMA_EngineStatusRead(pXdmaDma, TRUE, &intResult.u32DmaStatus);
    XDMA_DmaTransferStop(pXdmaDma);

    intResult.hDma = pXdmaDma;

    WDC_ReadAddr32(pDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
        pXdmaDma->fToDevice ?  XDMA_H2C_CHANNEL_COMPLETED_DESC_COUNT_OFFSET :
        XDMA_C2H_CHANNEL_COMPLETED_DESC_COUNT_OFFSET),
        &val);

    TraceLog("XDMA_IntHandler: Completed DMA descriptors %d\n", val);

    intResult.dwCounter = pDev->Int.dwCounter;
    intResult.dwLost = pDev->Int.dwLost;
    intResult.waitResult = (WD_INTERRUPT_WAIT_RESULT)pDev->Int.fStopped;

    intResult.fIsMessageBased =
        (WDC_GET_ENABLED_INT_TYPE(pDev) == INTERRUPT_MESSAGE ||
        WDC_GET_ENABLED_INT_TYPE(pDev) == INTERRUPT_MESSAGE_X) ?
        TRUE : FALSE;
    intResult.dwLastMessage = WDC_GET_ENABLED_INT_LAST_MSG(pDev);
    intResult.pData = pXdmaDma->pData;

    /* Execute the diagnostics application's interrupt handler routine */
    pDevCtx->funcDiagIntHandler((WDC_DEVICE_HANDLE)pDev, &intResult);
}

/* Interrupt handler routine */
static void DLLCALLCONV XDMA_IntHandler(PVOID pData)
{
    PWDC_DEVICE pDev = (PWDC_DEVICE)pData;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pDev);
    XDMA_DMA_STRUCT *pXdmaDma = NULL;
    UINT32 i, u32IntRequest = pDevCtx->pTrans[0].Data.Dword;

    /* Disable interrupts of completed engines. If level sensitive interrupts
     * are used, interrupts should be disabled by transfer commands or by
     * kernel plugin */
    XDMA_ChannelInterruptsDisable(pDev, u32IntRequest);

    for (i = 0; i < XDMA_CHANNELS_NUM * 2; i++)
    {
        pXdmaDma = &(pDevCtx->pEnginesArr[i]);

        if (u32IntRequest & pXdmaDma->u32IrqBitMask)
        {
            if (pXdmaDma->fIsEnabled)
                HandleEngineInterrupt(pXdmaDma, u32IntRequest);
            else
                ErrLog("Engine [%d] is disabled\n", i);
        }
    }
}

/* Enable interrupts */
DWORD XDMA_IntEnable(WDC_DEVICE_HANDLE hDev, XDMA_INT_HANDLER funcIntHandler)
{
    DWORD dwStatus;
    PWDC_DEVICE pDev = (PWDC_DEVICE)hDev;
    PXDMA_DEV_CTX pDevCtx;
    WDC_ADDR_DESC *pAddrDesc;
    WD_TRANSFER *pTrans = NULL;

    TraceLog("XDMA_IntEnable: Entered. Device handle [0x%p]\n", hDev);

    /* Validate the WDC device handle */
    if (!IsValidDevice(pDev, "XDMA_IntEnable"))
        return WD_INVALID_PARAMETER;

    pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pDev);

    /* Check whether interrupts are already enabled */
    if (WDC_IntIsEnabled(hDev))
    {
        ErrLog("Interrupts are already enabled ...\n");
        return WD_OPERATION_ALREADY_DONE;
    }

#if 0
    /* TODO: Uncomment in case level sensitive interrupts are used */
    #define NUM_TRANS_CMDS 3
    pTrans = (WD_TRANSFER *)calloc(NUM_TRANS_CMDS, sizeof(WD_TRANSFER));
    if (!pTrans)
    {
        ErrLog("Failed allocating memory for interrupt transfer commands\n");
        return WD_INSUFFICIENT_RESOURCES;
    }

    pAddrDesc = WDC_GET_ADDR_DESC(pDev, pDevCtx->dwConfigBarNum);

    /* Read IRQ Block Channel Interrupt Request register */
    pTrans[0].pPort = pAddrDesc->pAddr +
        XDMA_IRQ_BLOCK_CHANNEL_INT_REQUEST_OFFSET;
    pTrans[0].cmdTrans = WDC_ADDR_IS_MEM(pAddrDesc) ? RM_DWORD : RP_DWORD;

    #define XDMA_CHANNEL_INT_MASK 0x000000FF
    /* Check if it was a DMA completion interrupt */
    pTrans[1].cmdTrans = CMD_MASK;
    pTrans[1].Data.Dword = XDMA_CHANNEL_INT_MASK;

    /* Disable all channel interrupts.
     * Note: If kernel plugin is used only the relevant channel's interrupts can
     * be disabled */
    pTrans[2].pPort = pAddrDesc->pAddr +
        XDMA_IRQ_BLOCK_CHANNEL_INT_ENABLE_MASK_W1C_OFFSET;
    pTrans[2].Data.Dword = 0xFFFFFFFF;
    pTrans[2].cmdTrans = WDC_ADDR_IS_MEM(pAddrDesc) ? WM_DWORD : WP_DWORD;
#else
    /* Define a transfer command to read the interrupt status. This is needed in
     * order to determine in user mode ISR the DMA engines that completed DMA
     * transfer */
    #define NUM_TRANS_CMDS 1
    pTrans = (WD_TRANSFER *)calloc(NUM_TRANS_CMDS, sizeof(WD_TRANSFER));
    if (!pTrans)
    {
        ErrLog("Failed allocating memory for interrupt transfer commands\n");
        return WD_INSUFFICIENT_RESOURCES;
    }

    pAddrDesc = WDC_GET_ADDR_DESC(pDev, pDevCtx->dwConfigBarNum);

    /* Read IRQ Block Channel Interrupt Request register */
    pTrans[0].pPort = pAddrDesc->pAddr +
        XDMA_IRQ_BLOCK_CHANNEL_INT_REQUEST_OFFSET;
    pTrans[0].cmdTrans = WDC_ADDR_IS_MEM(pAddrDesc) ? RM_DWORD : RP_DWORD;
#endif

    /* Store the diag interrupt handler routine, which will be executed by
       XDMA_IntHandler() when an interrupt is received */
    pDevCtx->funcDiagIntHandler = funcIntHandler;

    /* Enable interrupts */
    dwStatus = WDC_IntEnable(hDev, pTrans, NUM_TRANS_CMDS, INTERRUPT_CMD_COPY,
        XDMA_IntHandler, (PVOID)pDev, WDC_IS_KP(hDev));

    if (WD_STATUS_SUCCESS != dwStatus)
    {
        ErrLog("Failed enabling interrupts. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
        free(pTrans);
        return dwStatus;
    }

    pDevCtx->pTrans = pTrans;
    pDevCtx->dwEnabledIntType = WDC_GET_ENABLED_INT_TYPE(hDev);

    TraceLog("XDMA_IntEnable: '%s' Interrupts enabled\n",
        WDC_IntType2Str(pDevCtx->dwEnabledIntType));

    if (pDevCtx->dwEnabledIntType == INTERRUPT_LEVEL_SENSITIVE)
    {
        UINT32 u32WriteVal;
        BYTE bIntPin;

        dwStatus = WDC_PciReadCfg8(hDev, PCI_IPR, &bIntPin);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            ErrLog("Failed reading interrupt pin value. Error 0x%x - %s\n",
                dwStatus, Stat2Str(dwStatus));
            goto Error;
        }

        u32WriteVal = (bIntPin << 24) | (bIntPin << 16) |
            (bIntPin << 8) | bIntPin;

        /* Write Interrupt Pin value to IRQ Block Channel Vector Number
           registers */
        WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum,
            XDMA_IRQ_BLOCK_CHANNEL_VECTOR_1_OFFSET, u32WriteVal);
        WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum,
            XDMA_IRQ_BLOCK_CHANNEL_VECTOR_2_OFFSET, u32WriteVal);
    }

    return WD_STATUS_SUCCESS;

Error:
    dwStatus = WDC_IntDisable(hDev);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed disabling interrupts. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
    }

    return dwStatus;
}

/* Disable interrupts */
DWORD XDMA_IntDisable(WDC_DEVICE_HANDLE hDev)
{
    DWORD dwStatus;
    PWDC_DEVICE pDev = (PWDC_DEVICE)hDev;
    PXDMA_DEV_CTX pDevCtx;

    TraceLog("XDMA_IntDisable: Entered. Device handle [0x%p]\n", hDev);

    /* Validate the WDC device handle */
    if (!IsValidDevice(pDev, "XDMA_IntDisable"))
        return WD_INVALID_PARAMETER;

    pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pDev);

    /* Check whether interrupts are already disabled */
    if (!WDC_IntIsEnabled(hDev))
    {
        ErrLog("Interrupts are already disabled ...\n");
        return WD_OPERATION_ALREADY_DONE;
    }

    XDMA_ChannelInterruptsDisable(hDev, 0xFFFFFFFF);
    XDMA_UserInterruptsDisable(hDev, 0xFFFFFFFF);

    /* Disable interrupts */
    dwStatus = WDC_IntDisable(hDev);
    if (WD_STATUS_SUCCESS != dwStatus)
    {
        ErrLog("Failed disabling interrupts. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
    }

    if (pDevCtx->pTrans)
        free(pDevCtx->pTrans);

    return dwStatus;
}

DWORD XDMA_UserInterruptsEnable(WDC_DEVICE_HANDLE hDev, UINT32 mask)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);

    return WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum,
        XDMA_IRQ_BLOCK_USER_INT_ENABLE_MASK_W1S_OFFSET, mask);
}

DWORD XDMA_UserInterruptsDisable(WDC_DEVICE_HANDLE hDev, UINT32 mask)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);

    return WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum,
        XDMA_IRQ_BLOCK_USER_INT_ENABLE_MASK_W1C_OFFSET, mask);
}

DWORD XDMA_ChannelInterruptsEnable(WDC_DEVICE_HANDLE hDev, UINT32 mask)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);

    return WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum,
        XDMA_IRQ_BLOCK_CHANNEL_INT_ENABLE_MASK_W1S_OFFSET, mask);
}

DWORD XDMA_ChannelInterruptsDisable(WDC_DEVICE_HANDLE hDev, UINT32 mask)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);

    return WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum,
        XDMA_IRQ_BLOCK_CHANNEL_INT_ENABLE_MASK_W1C_OFFSET, mask);
}

/* Check whether interrupts are enabled for the given device */
BOOL XDMA_IntIsEnabled(WDC_DEVICE_HANDLE hDev)
{
    /* Validate the WDC device handle */
    if (!IsValidDevice((PWDC_DEVICE)hDev, "XDMA_IntIsEnabled"))
        return FALSE;

    /* Check whether interrupts are already enabled */
    return WDC_IntIsEnabled(hDev);
}
#endif /* ifdef HAS_INTS */

/* -----------------------------------------------
    Direct Memory Access (DMA)
   ----------------------------------------------- */

DWORD XDMA_EngineStatusRead(XDMA_DMA_HANDLE hDma, BOOL fClear, UINT32 *pStatus)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)hDma;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pXdmaDma->hDev);
    DWORD dwOffset;

    if (fClear)
    {
        dwOffset = XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
            pXdmaDma->fToDevice ? XDMA_H2C_CHANNEL_STATUS_RC_OFFSET :
            XDMA_C2H_CHANNEL_STATUS_RC_OFFSET);
    }
    else
    {
        dwOffset = XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
            pXdmaDma->fToDevice ? XDMA_H2C_CHANNEL_STATUS_OFFSET :
            XDMA_C2H_CHANNEL_STATUS_OFFSET);
    }

    return WDC_ReadAddr32(pXdmaDma->hDev, pDevCtx->dwConfigBarNum, dwOffset,
        pStatus);
}

static DWORD ValidateTransferParams(WDC_DEVICE_HANDLE hDev, BOOL fToDevice,
    DWORD dwChannel)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);
    DWORD dwStatus;
    UINT32 engine_id_reg, engine_id, engine_channel_num;

    if (dwChannel > (XDMA_CHANNELS_NUM - 1))
    {
        ErrLog("Wrong channel (%d), should be between 0 and 3\n", dwChannel);
        return WD_INVALID_PARAMETER;
    }

    dwStatus = WDC_ReadAddr32(hDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(dwChannel, fToDevice ?
        XDMA_H2C_CHANNEL_IDENTIFIER_OFFSET :
        XDMA_C2H_CHANNEL_IDENTIFIER_OFFSET),
        &engine_id_reg);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed reading engine id register\n");
        return dwStatus;
    }

    engine_id = XDMA_ENGINE_ID(engine_id_reg);
    engine_channel_num = XDMA_ENGINE_CHANNEL_NUM(engine_id_reg);
    if (dwChannel != engine_channel_num)
    {
        ErrLog("Wrong channel, expected 0x%x, read 0x%x\n", dwChannel,
            engine_channel_num);
        return WD_INVALID_PARAMETER;
    }

    TraceLog("engine_id 0x%x, engine_channel_num %d\n", engine_id,
        engine_channel_num);

    return WD_STATUS_SUCCESS;
}

static BOOL EngineIsStreaming(WDC_DEVICE_HANDLE hDev, DWORD dwChannel,
    BOOL fToDevice)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);
    UINT32 engine_id_reg;

    WDC_ReadAddr32(hDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(dwChannel, fToDevice ?
        XDMA_H2C_CHANNEL_IDENTIFIER_OFFSET :
        XDMA_C2H_CHANNEL_IDENTIFIER_OFFSET),
        &engine_id_reg);

    return (BOOL)(engine_id_reg & 0x8000);
}

static DWORD LockDmaBuffer(WDC_DEVICE_HANDLE hDev, BOOL fToDevice, PVOID *ppBuf,
    DWORD dwBytes, WD_DMA **ppDma, BOOL fIsTransaction)
{
    DWORD dwStatus, dwOptions;

    *ppBuf = __valloc(dwBytes); /* Make sure that the buffer is aligned */
    if (!*ppBuf)
    {
        ErrLog("Memory allocation failure\n");
        return WD_INSUFFICIENT_RESOURCES;
    }

    dwOptions = DMA_ALLOW_64BIT_ADDRESS |
        (fToDevice ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

    if (!fIsTransaction)
    {
        /* DMA_DISABLE_MERGE_ADJACENT_PAGES is needed to make sure that each SG
         * page is not larger than 0x0FFFFFFF */
        dwStatus = WDC_DMASGBufLock(hDev, *ppBuf,
            dwOptions | DMA_DISABLE_MERGE_ADJACENT_PAGES, dwBytes, ppDma);
    }
    else
    {
        dwStatus = WDC_DMATransactionSGInit(hDev, *ppBuf, dwOptions, dwBytes, ppDma,
            NULL, XDMA_TRANSACTION_SAMPLE_MAX_TRANSFER_SIZE,
            sizeof(XDMA_DMA_DESC));
    }
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed locking DMA buffer. Error 0x%x - %s\n", dwStatus,
            Stat2Str(dwStatus));
        goto Error;
    }

    return WD_STATUS_SUCCESS;

Error:
    free(*ppBuf);
    *ppBuf = NULL;

    return dwStatus;
}

static DWORD EngineCtrlRegisterSet(WDC_DEVICE_HANDLE hDev, DWORD dwChannel,
    BOOL fToDevice, UINT32 val)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);
    DWORD offset = XDMA_CHANNEL_OFFSET(dwChannel,
        fToDevice ? XDMA_H2C_CHANNEL_CONTROL_OFFSET :
        XDMA_C2H_CHANNEL_CONTROL_OFFSET);

    return WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum, offset, val);
}

#ifdef HAS_INTS
static DWORD EnableDmaInterrupts(WDC_DEVICE_HANDLE hDev, DWORD dwChannel,
    BOOL fStreaming, BOOL fToDevice)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);
    DWORD offset;
    UINT32 val;

    /* Error interrupts */
    val = XDMA_CTRL_IE_DESC_ALIGN_MISMATCH |
        XDMA_CTRL_IE_MAGIC_STOPPED |
        XDMA_CTRL_IE_READ_ERROR |
        XDMA_CTRL_IE_DESC_ERROR;

    /* Enable completion interrupts */
    val |= XDMA_CTRL_IE_DESC_STOPPED | XDMA_CTRL_IE_DESC_COMPLETED;
    if (fStreaming)
        val |= XDMA_CTRL_IE_IDLE_STOPPED;

    offset = XDMA_CHANNEL_OFFSET(dwChannel, fToDevice ?
        XDMA_H2C_CHANNEL_INT_ENABLE_MASK_OFFSET :
        XDMA_C2H_CHANNEL_INT_ENABLE_MASK_OFFSET);

    WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum, offset, val);

    /* Make sure channel interrupts are enabled */
    XDMA_ChannelInterruptsEnable(hDev, 0xFFFFFFFF);

    return WD_STATUS_SUCCESS;
}
#endif /* ifdef HAS_INTS */

static void DmaDescDump(XDMA_DMA_STRUCT *pXdmaDma)
{
    XDMA_DMA_DESC *desc = (XDMA_DMA_DESC *)pXdmaDma->pDescBuf;
    DWORD i;

    for (i = 0; i < pXdmaDma->pDma->dwPages; i++)
    {
        TraceLog("DmaDescDump: desc[%d].u32Control 0x%x\n", i,
            desc[i].u32Control);
        TraceLog("DmaDescDump: desc[%d].dwBytes 0x%x\n", i, desc[i].u32Bytes);
        TraceLog("DmaDescDump: desc[%d].u64SrcAddr 0x%llx\n", i,
            desc[i].u64SrcAddr);
        TraceLog("DmaDescDump: desc[%d].u64DstAddr 0x%llx\n", i,
            desc[i].u64DstAddr);
        TraceLog("DmaDescDump: desc[%d].u64NextAddr 0x%llx\n", i,
            desc[i].u64NextDesc);
    }
}

static DWORD DmaBuildDescBuffer(XDMA_DMA_STRUCT *pXdmaDma, BOOL fIsTransaction)
{
    DWORD dwStatus = WD_STATUS_SUCCESS;
    DWORD dwPages, dwSize;

    if (fIsTransaction)
    {
        dwPages = ((XDMA_TRANSACTION_SAMPLE_MAX_TRANSFER_SIZE +
            GetPageSize() - 1) / GetPageSize()) + 1;
    }
    else
    {
        dwPages = pXdmaDma->pDma->dwPages;
    }

    dwSize = dwPages * sizeof(XDMA_DMA_DESC);

    dwStatus = WDC_DMAContigBufLock(pXdmaDma->hDev, &pXdmaDma->pDescBuf,
        DMA_ALLOW_64BIT_ADDRESS | DMA_TO_DEVICE, dwSize, &pXdmaDma->pDmaDesc);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed locking DMA descriptors buffer. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
    }

    return dwStatus;
}

static void DLLCALLCONV DmaTransferBuild(PVOID pData)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)pData;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pXdmaDma->hDev);
    XDMA_DMA_DESC *desc = (XDMA_DMA_DESC *)pXdmaDma->pDescBuf;
    XDMA_DMA_DESC *desc_virt = (XDMA_DMA_DESC *)pXdmaDma->pDescBuf;
    UINT64 offset = pXdmaDma->u64FPGAOffset;
    DWORD dwPages = pXdmaDma->pDma->dwPages;
    DWORD dwSize = dwPages * sizeof(XDMA_DMA_DESC);
    DMA_ADDR desc_phys = pXdmaDma->pDmaDesc->Page[0].pPhysicalAddr;
    DWORD i;

    TraceLog("DmaTransferBuild: dwPages %d\n", dwPages);

    memset(desc_virt, 0, dwSize);

    for (i = 0; i < dwPages; i++)
    {
        desc_phys += sizeof(XDMA_DMA_DESC);
        desc_virt[i].u32Control = XDMA_DESC_MAGIC; /* Descriptor magic number */

        if (pXdmaDma->fToDevice)
        {
            desc[i].u64SrcAddr = pXdmaDma->pDma->Page[i].pPhysicalAddr;
            desc[i].u64DstAddr = offset;
        }
        else
        {
            desc[i].u64SrcAddr = offset;
            desc[i].u64DstAddr = pXdmaDma->pDma->Page[i].pPhysicalAddr;
        }

        /* Buffer size should not exceed 0x0FFFFFFF bytes, but this should not
         * happen when using s/g DMA buffer */
        desc[i].u32Bytes = pXdmaDma->pDma->Page[i].dwBytes;
        if (!pXdmaDma->fNonIncMode)
            offset += desc[i].u32Bytes;

        if (i < dwPages - 1)
        {
            desc_virt[i].u64NextDesc = (UINT64)desc_phys;
        }
        else /* Last descriptor */
        {
            desc[i].u32Control |= XDMA_DESC_STOPPED | XDMA_DESC_EOP |
                XDMA_DESC_COMPLETED;
        }
    }


    WDC_WriteAddr32(pXdmaDma->hDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
        pXdmaDma->fToDevice ? XDMA_H2C_SGDMA_DESC_LOW_OFFSET :
        XDMA_C2H_SGDMA_DESC_LOW_OFFSET),
        DMA_ADDR_LOW(pXdmaDma->pDmaDesc->Page[0].pPhysicalAddr));
    WDC_WriteAddr32(pXdmaDma->hDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
        pXdmaDma->fToDevice ? XDMA_H2C_SGDMA_DESC_HIGH_OFFSET :
        XDMA_C2H_SGDMA_DESC_HIGH_OFFSET),
        DMA_ADDR_HIGH(pXdmaDma->pDmaDesc->Page[0].pPhysicalAddr));

    WDC_WriteAddr32(pXdmaDma->hDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
        pXdmaDma->fToDevice ? XDMA_H2C_SGDMA_DESC_ADJACENT_OFFSET :
        XDMA_C2H_SGDMA_DESC_ADJACENT_OFFSET),
        0);

    DmaDescDump(pXdmaDma);

    /* TODO: Set adjacent descriptors */

    WDC_DMASyncCpu(pXdmaDma->pDmaDesc);
}

static DWORD ConfigureDmaDesc(XDMA_DMA_STRUCT *pXdmaDma)
{
    DWORD dwStatus;

    dwStatus = DmaBuildDescBuffer(pXdmaDma, FALSE);
    if (dwStatus != WD_STATUS_SUCCESS)
        goto Exit;

    DmaTransferBuild(pXdmaDma);

Exit:
    return dwStatus;
}

static DWORD CheckAlignment(XDMA_DMA_STRUCT *pXdmaDma)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pXdmaDma->hDev);
    UINT32 u32AlignmentsReg, u32Align, u32Granularity;
    UINT32 u32BufLsb, u32OffsetLsb, u32SizeLsb;
    DWORD dwStatus;

    dwStatus = WDC_ReadAddr32(pXdmaDma->hDev,
        pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
        pXdmaDma->fToDevice ? XDMA_H2C_CHANNEL_ALIGNMENTS_OFFSET :
        XDMA_C2H_CHANNEL_ALIGNMENTS_OFFSET),
        &u32AlignmentsReg);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed reading alignments register\n");
        return dwStatus;
    }

    TraceLog("u32AlignmentsReg 0x%x\n", u32AlignmentsReg);

    if (!u32AlignmentsReg)
    {
        TraceLog("Alignments register not set\n");
        return WD_STATUS_SUCCESS;
    }

    u32Align = (u32AlignmentsReg & 0x00FF0000) >> 16;
    u32Granularity = (u32AlignmentsReg & 0x0000FF00) >> 8;
    TraceLog("u32Align %d, u32Granularity %d\n", u32Align, u32Granularity);

    u32BufLsb = (UINT32)((UPTR)pXdmaDma->pBuf & (u32Align - 1));
    u32OffsetLsb = (UINT32)(pXdmaDma->u64FPGAOffset) & (u32Align - 1);
    u32SizeLsb = (UINT32)pXdmaDma->dwBytes & ((UINT32)u32Granularity - 1);

    if (pXdmaDma->fStreaming || pXdmaDma->fNonIncMode)
    {
        if (u32BufLsb != 0)
        {
            ErrLog("Buffer not aligned (%p)\n", pXdmaDma->pBuf);
            return WD_INVALID_PARAMETER;
        }

        if (u32SizeLsb != 0)
        {
            ErrLog("Buffer size %d not multiple of %d\n", pXdmaDma->dwBytes,
                u32Granularity);
            return WD_INVALID_PARAMETER;
        }

        if (!pXdmaDma->fStreaming && u32OffsetLsb != 0)
        {
            ErrLog("FPGA offset %x not aligned\n", pXdmaDma->u64FPGAOffset);
            return WD_INVALID_PARAMETER;
        }
    }
    else if (u32BufLsb != u32OffsetLsb)
    {
        ErrLog("Buffer alignment 0x%p and FPGA offset alignment 0x%x do not "
            "match\n", pXdmaDma->pBuf, pXdmaDma->u64FPGAOffset);
        return WD_INVALID_PARAMETER;
    }

    return WD_STATUS_SUCCESS;
}

DWORD XDMA_DmaTransferStart(XDMA_DMA_HANDLE hDma)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)hDma;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pXdmaDma->hDev);
    UINT32 val;
    DWORD dwStatus;

#ifdef HAS_INTS
    if (!pXdmaDma->fPolling)
    {
        dwStatus = EnableDmaInterrupts(pXdmaDma->hDev, pXdmaDma->dwChannel,
            pXdmaDma->fStreaming, pXdmaDma->fToDevice);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            ErrLog("Failed enabling DMA interrupts. Error 0x%x - %s\n",
                dwStatus, Stat2Str(dwStatus));
            return dwStatus;
        }

        /* Dummy read to flush pending writes */
        WDC_ReadAddr32(pXdmaDma->hDev, pDevCtx->dwConfigBarNum,
            XDMA_IRQ_BLOCK_CHANNEL_INT_REQUEST_OFFSET, &val);
    }
    else
#endif /* ifdef HAS_INTS */
    {
        XDMA_DMA_POLL_WB *pWB = (XDMA_DMA_POLL_WB *)pXdmaDma->pWBBuf;
        pWB->u32CompletedDescs = 0;
    }

    if (pXdmaDma->fToDevice)
        WDC_DMASyncCpu(pXdmaDma->pDma);

    val = XDMA_CTRL_RUN_STOP |
        XDMA_CTRL_IE_READ_ERROR |
        XDMA_CTRL_IE_DESC_ERROR |
        XDMA_CTRL_IE_DESC_ALIGN_MISMATCH |
        XDMA_CTRL_IE_MAGIC_STOPPED;

#ifdef HAS_INTS
    if (pXdmaDma->fPolling)
    {
        val |= XDMA_CTRL_POLL_MODE_WB;
    }
    else
#endif /* ifdef HAS_INTS */
    {
        val |= XDMA_CTRL_IE_DESC_STOPPED | XDMA_CTRL_IE_DESC_COMPLETED;
        if (pXdmaDma->fStreaming && !pXdmaDma->fToDevice)
            val |= XDMA_CTRL_IE_IDLE_STOPPED;
    }

    if (pXdmaDma->fNonIncMode)
        val |= XDMA_CTRL_NON_INCR_ADDR;

    dwStatus = EngineCtrlRegisterSet(pXdmaDma->hDev, pXdmaDma->dwChannel,
        pXdmaDma->fToDevice, val);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed starting DMA transfer\n");
        return dwStatus;
    }

    /* Dummy read to flush all previous writes */
    WDC_ReadAddr32(pXdmaDma->hDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
        pXdmaDma->fToDevice ? XDMA_H2C_CHANNEL_STATUS_OFFSET :
        XDMA_C2H_CHANNEL_STATUS_OFFSET),
        &val);

    return WD_STATUS_SUCCESS;
}

DWORD XDMA_DmaTransferStop(XDMA_DMA_HANDLE hDma)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)hDma;
    UINT32 val = XDMA_CTRL_IE_DESC_ALIGN_MISMATCH |
        XDMA_CTRL_IE_MAGIC_STOPPED |
        XDMA_CTRL_IE_READ_ERROR |
        XDMA_CTRL_IE_DESC_ERROR;

    if (pXdmaDma->fPolling)
    {
         val |= XDMA_CTRL_POLL_MODE_WB;
    }
    else
    {
         val |= XDMA_CTRL_IE_DESC_STOPPED | XDMA_CTRL_IE_DESC_COMPLETED;
         if (pXdmaDma->fStreaming && !pXdmaDma->fToDevice)
             val |= XDMA_CTRL_IE_IDLE_STOPPED;
    }

    return EngineCtrlRegisterSet(pXdmaDma->hDev, pXdmaDma->dwChannel,
        pXdmaDma->fToDevice, val);
}

DWORD XDMA_DmaPollCompletion(XDMA_DMA_HANDLE hDma)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)hDma;
    XDMA_DMA_POLL_WB *pWB;
    DWORD dwStatus = WD_STATUS_SUCCESS;

    if (!pXdmaDma->pWBDma || !pXdmaDma->pWBBuf)
    {
        ErrLog("Polling WB address not defined\n");
        return WD_INVALID_PARAMETER;
    }

    pWB = (XDMA_DMA_POLL_WB *)pXdmaDma->pWBBuf;
    while (pWB->u32CompletedDescs < pXdmaDma->pDma->dwPages)
    {
        WDC_DMASyncIo(pXdmaDma->pWBDma);

        if (pWB->u32CompletedDescs & XDMA_WB_ERR_MASK)
        {
            UINT32 val;

            XDMA_EngineStatusRead(pXdmaDma, TRUE, &val);
            ErrLog("XDMA_DmaPollCompletion: DMA Transfer failed, "
                "DMA status 0x%08x\n", val);
            dwStatus = WD_OPERATION_FAILED;
            break;
        }
    }

    XDMA_DmaTransferStop(pXdmaDma);

    if (!pXdmaDma->fToDevice)
        WDC_DMASyncIo(pXdmaDma->pDma);

    TraceLog("XDMA_DmaPollCompletion: completed descs %d\n",
        pWB->u32CompletedDescs);

    return dwStatus;
}

static DWORD ConfigureWriteBackAddress(XDMA_DMA_STRUCT *pXdmaDma)
{
    DWORD dwStatus;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pXdmaDma->hDev);

    dwStatus = WDC_DMAContigBufLock(pXdmaDma->hDev,
        &pXdmaDma->pWBBuf,
        DMA_FROM_DEVICE | DMA_ALLOW_64BIT_ADDRESS,
        sizeof(XDMA_DMA_POLL_WB),
        &pXdmaDma->pWBDma);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed allocating DMA for polling WB\n");
        return dwStatus;
    }

    WDC_WriteAddr32(pXdmaDma->hDev,
        pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
        pXdmaDma->fToDevice ? XDMA_H2C_CHANNEL_POLL_LOW_WRITE_BACK_ADDR_OFFSET :
        XDMA_C2H_CHANNEL_POLL_LOW_WRITE_BACK_ADDR_OFFSET),
        DMA_ADDR_LOW(pXdmaDma->pWBDma->Page[0].pPhysicalAddr));
    WDC_WriteAddr32(pXdmaDma->hDev,
        pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(pXdmaDma->dwChannel,
        pXdmaDma->fToDevice ? XDMA_H2C_CHANNEL_POLL_HIGH_WRITE_BACK_ADDR_OFFSET :
        XDMA_C2H_CHANNEL_POLL_HIGH_WRITE_BACK_ADDR_OFFSET),
        DMA_ADDR_HIGH(pXdmaDma->pWBDma->Page[0].pPhysicalAddr));

    return WD_STATUS_SUCCESS;
}

/* Open a DMA handle: Allocate and initialize a XDMA DMA information structure,
 * including allocation of a scatter/gather DMA buffer */
DWORD XDMA_DmaOpen(WDC_DEVICE_HANDLE hDev, XDMA_DMA_HANDLE *phDma,
    DWORD dwBytes, UINT64 u64FPGAOffset, BOOL fToDevice, DWORD dwChannel,
    BOOL fPolling, BOOL fNonIncMode, PVOID pData, BOOL fIsTransaction)
{
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);
    DWORD idx = ENGINE_IDX(dwChannel, fToDevice);
    XDMA_DMA_STRUCT *pXdmaDma = &(pDevCtx->pEnginesArr[idx]);
    DWORD dwStatus;

    TraceLog("XDMA_DmaOpen: Entered. Device handle [0x%p], dwBytes [%d], "
        "fToDevice [%d], dwChannel [%d], fPolling [%d]\n", hDev, dwBytes,
        fToDevice, dwChannel, fPolling);

    if (!phDma)
        return WD_INVALID_PARAMETER;

    dwStatus = ValidateTransferParams(hDev, fToDevice, dwChannel);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed validating transfer params. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
        return dwStatus;
    }

    if (!pXdmaDma->fIsEnabled)
    {
        ErrLog("DMA engine channel [%d] for [%s] is disabled\n", dwChannel,
            fToDevice ? "writing" : "reading");
        return WD_INVALID_PARAMETER;
    }

    if (pXdmaDma->fIsInitialized)
    {
        ErrLog("DMA handle already open for this channel\n");
        *phDma = &pXdmaDma;
        return WD_OPERATION_ALREADY_DONE;
    }

    pXdmaDma->fStreaming = EngineIsStreaming(hDev, dwChannel, fToDevice);

    dwStatus = LockDmaBuffer(hDev, fToDevice, &pXdmaDma->pBuf, dwBytes,
        &pXdmaDma->pDma, fIsTransaction);
    if (dwStatus != WD_STATUS_SUCCESS)
        goto Error;

    pXdmaDma->hDev = hDev;
    pXdmaDma->dwBytes = dwBytes;
    pXdmaDma->dwChannel = dwChannel;
    pXdmaDma->u64FPGAOffset = u64FPGAOffset;
    pXdmaDma->fPolling = fPolling;
    pXdmaDma->fToDevice = fToDevice;
    pXdmaDma->fNonIncMode = fNonIncMode;
    pXdmaDma->pData = pData;
    *phDma = (XDMA_DMA_HANDLE)pXdmaDma;

    WDC_WriteAddr32(hDev, pDevCtx->dwConfigBarNum,
        XDMA_CHANNEL_OFFSET(dwChannel, fToDevice ?
        XDMA_H2C_CHANNEL_CONTROL_W1C_OFFSET :
        XDMA_C2H_CHANNEL_CONTROL_W1C_OFFSET),
        XDMA_CTRL_NON_INCR_ADDR);

    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed %s non-incrementing address mode\n",
            fNonIncMode ? "setting" : "clearing");
        goto Error;
    }

    dwStatus = CheckAlignment(pXdmaDma);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Alignment validation failed\n");
        goto Error;
    }

    if (fPolling)
    {
        dwStatus = ConfigureWriteBackAddress(pXdmaDma);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            ErrLog("Failed configuring WriteBack address. Error 0x%x - %s\n",
                dwStatus, Stat2Str(dwStatus));
            goto Error;
        }
    }

    if (fIsTransaction)
        dwStatus = DmaBuildDescBuffer(pXdmaDma, TRUE);
    else
        dwStatus = ConfigureDmaDesc(pXdmaDma);

    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("Failed configuring DMA descriptors buffer. Error 0x%x - %s\n",
            dwStatus, Stat2Str(dwStatus));
        goto Error;
    }

    TraceLog("Opened DMA: handle %p, fPolling %d, fToDevice %d, dwChannel %d, "
        "dwBytes %d, u64FPGAOffset %d, fStreaming %d, fNonIncMode %d\n",
        pXdmaDma, pXdmaDma->fPolling, pXdmaDma->fToDevice, pXdmaDma->dwChannel,
        pXdmaDma->dwBytes, pXdmaDma->u64FPGAOffset, pXdmaDma->fStreaming,
        pXdmaDma->fNonIncMode);

    pXdmaDma->fIsInitialized = TRUE;
    return WD_STATUS_SUCCESS;

Error:
    if (pXdmaDma->pDmaDesc)
        WDC_DMABufUnlock(pXdmaDma->pDmaDesc);
    if (pXdmaDma->pDma)
        WDC_DMABufUnlock(pXdmaDma->pDma);
    if (pXdmaDma->pBuf)
        __vfree(pXdmaDma->pBuf);

    pXdmaDma->fIsInitialized = FALSE;

    return dwStatus;
}

DWORD XDMA_DmaTransactionExecute(XDMA_DMA_HANDLE hDma, BOOL fNewContext,
    PVOID pData)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)hDma;
    DWORD dwStatus = WD_STATUS_SUCCESS;
    WD_DMA *pDma = pXdmaDma->pDma;

    TraceLog("\n%s: Executing DMA transaction for channel %d.\n", __FUNCTION__,
        pXdmaDma->dwChannel);

    if (fNewContext)
        pXdmaDma->pData = pData;

    dwStatus = WDC_DMATransactionExecute(pDma, DmaTransferBuild, hDma);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("%s: Failed to execute DMA transaction for channel %d. Error "
            "0x%x - %s\n", __FUNCTION__, pXdmaDma->dwChannel, dwStatus,
            Stat2Str(dwStatus));
    }

    return dwStatus;
}

DWORD XDMA_DmaTransactionTransferEnded(XDMA_DMA_HANDLE hDma)
{
    XDMA_DMA_STRUCT *pxdmaDma = (XDMA_DMA_STRUCT *)hDma;

    DWORD dwStatus = WDC_DMATransferCompletedAndCheck(pxdmaDma->pDma, TRUE);
    if (dwStatus == WD_STATUS_SUCCESS)
        TraceLog("DMA transaction completed");
    else if (dwStatus != (DWORD)WD_MORE_PROCESSING_REQUIRED)
        ErrLog("DMA transfer failed\n");

    return dwStatus;
}

DWORD XDMA_DmaTransactionRelease(XDMA_DMA_HANDLE hDma)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)hDma;
    DWORD dwStatus = WD_STATUS_SUCCESS;
    WD_DMA* pDma = pXdmaDma->pDma;

    dwStatus = WDC_DMATransactionRelease(pDma);
    if (dwStatus != WD_STATUS_SUCCESS)
    {
        ErrLog("%s: Failed to release DMA transaction for channel %d. Error "
            "0x%x - %s\n", __FUNCTION__, pXdmaDma->dwChannel, dwStatus,
            Stat2Str(dwStatus));
    }
    else
    {
        TraceLog("%s: DMA transaction of channel %d was released. \n",
            __FUNCTION__, pXdmaDma->dwChannel);
    }

    return dwStatus;
}


DWORD XDMA_DmaClose(XDMA_DMA_HANDLE hDma)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)hDma;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pXdmaDma->hDev);
    DWORD idx = ENGINE_IDX(pXdmaDma->dwChannel, pXdmaDma->fToDevice);
    DWORD dwStatus = WD_STATUS_SUCCESS;

    if (pXdmaDma->pWBDma)
    {
        dwStatus = WDC_DMABufUnlock(pXdmaDma->pWBDma);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            ErrLog("Failed unlocking DMA polling WB buffer. Error 0x%x - %s\n",
                dwStatus, Stat2Str(dwStatus));
        }
    }

    if (pXdmaDma->pDmaDesc)
    {
        dwStatus = WDC_DMABufUnlock(pXdmaDma->pDmaDesc);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            ErrLog("Failed unlocking DMA descriptors buffer. "
                "Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
        }
    }

    if (pXdmaDma->pDma)
    {
        dwStatus = WDC_DMABufUnlock(pXdmaDma->pDma);
        if (dwStatus != WD_STATUS_SUCCESS)
        {
            ErrLog("Failed unlocking DMA buffer. Error 0x%x - %s\n", dwStatus,
                Stat2Str(dwStatus));
        }
    }

    if (pXdmaDma->pBuf)
        __vfree(pXdmaDma->pBuf);

    pDevCtx->pEnginesArr[idx].fIsInitialized = FALSE;

    return dwStatus;
}

/* Returns DMA direction. TRUE - host to device, FALSE - device to host */
BOOL XDMA_DmaIsToDevice(XDMA_DMA_HANDLE hDma)
{
    return ((XDMA_DMA_STRUCT *)hDma)->fToDevice;
}

/* Returns pointer to the allocated virtual buffer and buffer size in bytes */
PVOID XDMA_DmaBufferGet(XDMA_DMA_HANDLE hDma, DWORD *pBytes)
{
    XDMA_DMA_STRUCT *pXdmaDma = (XDMA_DMA_STRUCT *)hDma;

    if (!hDma || !pBytes)
        return NULL;

    *pBytes = pXdmaDma->dwBytes;
    return pXdmaDma->pBuf;
}

/* -----------------------------------------------
    Plug-and-play and power management events
   ----------------------------------------------- */
/* Plug-and-play or power management event handler routine */
static void XDMA_EventHandler(WD_EVENT *pEvent, PVOID pData)
{
    PWDC_DEVICE pDev = (PWDC_DEVICE)pData;
    PXDMA_DEV_CTX pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pDev);

    TraceLog("XDMA_EventHandler: Entered. pData [0x%p], dwAction [0x%x]\n",
        pData, pEvent->dwAction);

    /* Execute the diagnostics application's event handler function */
    pDevCtx->funcDiagEventHandler((WDC_DEVICE_HANDLE)pDev, pEvent->dwAction);
}

/* Register a plug-and-play or power management event */
DWORD XDMA_EventRegister(WDC_DEVICE_HANDLE hDev,
    XDMA_EVENT_HANDLER funcEventHandler)
{
    DWORD dwStatus;
    PWDC_DEVICE pDev = (PWDC_DEVICE)hDev;
    PXDMA_DEV_CTX pDevCtx;
    DWORD dwActions = WD_ACTIONS_ALL;
    /* TODO: Modify the above to set up the plug-and-play/power management
             events for which you wish to receive notifications.
             dwActions can be set to any combination of the WD_EVENT_ACTION
             flags defined in windrvr.h. */

    TraceLog("XDMA_EventRegister: Entered. Device handle [0x%p]\n", hDev);

    /* Validate the WDC device handle */
    if (!IsValidDevice(pDev, "XDMA_EventRegister"))
        return WD_INVALID_PARAMETER;

    pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(pDev);

    /* Check whether the event is already registered */
    if (WDC_EventIsRegistered(hDev))
    {
        ErrLog("XDMA_EventRegister: Events are already registered ...\n");
        return WD_OPERATION_ALREADY_DONE;
    }

    /* Store the diag event handler routine to be executed from
     * XDMA_EventHandler() upon an event */
    pDevCtx->funcDiagEventHandler = funcEventHandler;

    /* Register the event */
    dwStatus = WDC_EventRegister(hDev, dwActions, XDMA_EventHandler, hDev,
        WDC_IS_KP(hDev));

    if (WD_STATUS_SUCCESS != dwStatus)
    {
        ErrLog("XDMA_EventRegister: Failed to register events. "
            "Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
        return dwStatus;
    }

    TraceLog("XDMA_EventRegister: Events registered\n");

    return WD_STATUS_SUCCESS;
}

/* Unregister a plug-and-play or power management event */
DWORD XDMA_EventUnregister(WDC_DEVICE_HANDLE hDev)
{
    DWORD dwStatus;

    TraceLog("XDMA_EventUnregister: Entered. Device handle [0x%p]\n", hDev);

    /* Validate the WDC device handle */
    if (!IsValidDevice((PWDC_DEVICE)hDev, "XDMA_EventUnregister"))
        return WD_INVALID_PARAMETER;

    /* Check whether the event is currently registered */
    if (!WDC_EventIsRegistered(hDev))
    {
        ErrLog("XDMA_EventUnregister: Cannot unregister events. No events "
            "currently registered ...\n");
        return WD_OPERATION_ALREADY_DONE;
    }

    /* Unregister the event */
    dwStatus = WDC_EventUnregister(hDev);

    if (WD_STATUS_SUCCESS != dwStatus)
    {
        ErrLog("XDMA_EventUnregister: Failed to unregister events. "
            "Error 0x%x - %s\n", dwStatus, Stat2Str(dwStatus));
    }

    return dwStatus;
}

/* Check whether a given plug-and-play or power management event is registered
 */
BOOL XDMA_EventIsRegistered(WDC_DEVICE_HANDLE hDev)
{
    /* Validate the WDC device handle */
    if (!IsValidDevice((PWDC_DEVICE)hDev, "XDMA_EventIsRegistered"))
        return FALSE;

    /* Check whether the event is registered */
    return WDC_EventIsRegistered(hDev);
}
#endif

/* Get configuration BAR number */
DWORD XDMA_ConfigBarNumGet(WDC_DEVICE_HANDLE hDev)
{
    PXDMA_DEV_CTX pDevCtx;;

    if (!IsValidDevice((PWDC_DEVICE)hDev, "XDMA_ConfigBarNumGet"))
        return (DWORD)-1;

    pDevCtx = (PXDMA_DEV_CTX)WDC_GetDevContext(hDev);

    return pDevCtx->dwConfigBarNum;
}

/* -----------------------------------------------
    Debugging and error handling
   ----------------------------------------------- */
/* Log a debug error message */
static void ErrLog(const CHAR *sFormat, ...)
{
    va_list argp;

    va_start(argp, sFormat);
    vsnprintf(gsXDMA_LastErr, sizeof(gsXDMA_LastErr) - 1, sFormat, argp);
#if defined(DEBUG)
    #if defined(__KERNEL__)
        WDC_Err("KP XDMA lib: %s", gsXDMA_LastErr);
     #else
        WDC_Err("XDMA lib: %s", gsXDMA_LastErr);
    #endif
#endif
    va_end(argp);
}

/* Log a debug trace message */
static void TraceLog(const CHAR *sFormat, ...)
{
#if defined(DEBUG)
    CHAR sMsg[256];
    va_list argp;

    va_start(argp, sFormat);
    vsnprintf(sMsg, sizeof(sMsg) - 1, sFormat, argp);
    #if defined(__KERNEL__)
        WDC_Trace("KP XDMA lib: %s", sMsg);
    #else
        WDC_Trace("XDMA lib: %s", sMsg);
    #endif
    va_end(argp);
#else
    UNUSED_VAR(sFormat);
#endif
}

/* Get last error */
const char *XDMA_GetLastErr(void)
{
    return gsXDMA_LastErr;
}

