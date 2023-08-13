/* Jungo Connectivity Confidential. Copyright (c) 2023 Jungo Connectivity Ltd.  https://www.jungo.com */

#ifndef _XDMA_LIB_H_
#define _XDMA_LIB_H_

/***************************************************************************
*  File: xdma_lib.h
*
*  Header file of a sample library for accessing Xilinx PCI Express cards
*  with XDMA design, using the WinDriver WDC API.
****************************************************************************/

#include "wdc_defs.h"
#include "wdc_diag_lib.h"
#include "pci_menus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************
  General definitions
 *************************************************************/
/* Kernel PlugIn driver name (should be no more than 8 characters) */
#define KP_XDMA_DRIVER_NAME "KP_XDMA"

/* Kernel PlugIn messages - used in WDC_CallKerPlug() calls (user mode) /
 * KP_XDMA_Call() (kernel mode) */
enum {
    KP_XDMA_MSG_VERSION = 1, /* Query the version of the Kernel PlugIn */
};

/* Kernel PlugIn messages status */
enum {
    KP_XDMA_STATUS_OK = 0x1,
    KP_XDMA_STATUS_MSG_NO_IMPL = 0x1000,
};

/* Default vendor and device IDs (0 == all) */
#define XDMA_DEFAULT_VENDOR_ID 0x10EE    /* Vendor ID */
#define XDMA_DEFAULT_DEVICE_ID 0x0       /* All Xilinx devices */
  /* TODO: Change the device ID value to match your specific device. */

/* Kernel PlugIn version information struct */
typedef struct {
    DWORD dwVer;
    CHAR cVer[100];
} KP_XDMA_VERSION;

/* Device address description struct */
typedef struct {
    DWORD dwNumAddrSpaces;    /* Total number of device address spaces */
    WDC_ADDR_DESC *pAddrDesc; /* Array of device address spaces information */
} XDMA_DEV_ADDR_DESC;

/* Address space information struct */
#define MAX_TYPE 8
typedef struct {
    DWORD dwAddrSpace;
    CHAR sType[MAX_TYPE];
    CHAR sName[MAX_NAME];
    CHAR sDesc[MAX_DESC];
} XDMA_ADDR_SPACE_INFO;

typedef void *XDMA_DMA_HANDLE;

/* Interrupt result information struct */
typedef struct
{
    DWORD dwCounter;        /* Number of interrupts received */
    DWORD dwLost;           /* Number of interrupts not yet handled */
    WD_INTERRUPT_WAIT_RESULT waitResult; /* See WD_INTERRUPT_WAIT_RESULT values
                                            in windrvr.h */
    BOOL fIsMessageBased;
    DWORD dwLastMessage;    /* Message data of the last received MSI/MSI-X
                             * (Windows Vista and higher); N/A to line-based
                             * interrupts) */
    UINT32 u32DmaStatus;    /* Status of the completed DMA transfer */
    UINT32 u32IntStatus;    /* Interrupt status */
    XDMA_DMA_HANDLE hDma;   /* Completed DMA handle */
    PVOID pData;            /* Custom context */
} XDMA_INT_RESULT;
/* TODO: You can add fields to XDMA_INT_RESULT to store any additional
         information that you wish to pass to your diagnostics interrupt
         handler routine (DiagIntHandler() in xdma_diag.c). */

/* XDMA diagnostics interrupt handler function type */
typedef void (*XDMA_INT_HANDLER)(WDC_DEVICE_HANDLE hDev,
    XDMA_INT_RESULT *pIntResult);

/* XDMA diagnostics plug-and-play and power management events handler
 * function type */
typedef void (*XDMA_EVENT_HANDLER)(WDC_DEVICE_HANDLE hDev, DWORD dwAction);

#define XDMA_MIN_CONFIG_BAR_SIZE 0x8FE4

#define XDMA_BLOCK_ID_HEAD      0x1FC00000
#define XDMA_BLOCK_ID_HEAD_MASK 0xFFF00000
#define XDMA_ID_MASK            0xFFF00000
#define XDMA_ID                 0x1FC00000
#define XDMA_IRQ_BLOCK_ID       (XDMA_ID | 0x20000)
#define XDMA_CONFIG_BLOCK_ID    (XDMA_ID | 0x30000)
#define XDMA_CHANNEL_MASK       0x00000F00

#define XDMA_ENG_IRQ_NUM        (1)
#define XDMA_CHANNELS_NUM       4       /* Up to 4 channels 0..3 */
#define XDMA_CHANNEL_SPACING    0x100
#define XDMA_CHANNEL_OFFSET(channel, reg) \
    ((reg) + ((channel) * XDMA_CHANNEL_SPACING))
#define XDMA_ENGINE_ID(reg) ((reg) & XDMA_ID_MASK) /* ID of the DMA engine */
#define XDMA_ENGINE_CHANNEL_NUM(reg) (((reg) & XDMA_CHANNEL_MASK) >> 8)

/* H2C/C2H control register bits */
#define XDMA_CTRL_RUN_STOP                      (1 << 0)
#define XDMA_CTRL_IE_DESC_STOPPED               (1 << 1)
#define XDMA_CTRL_IE_DESC_COMPLETED             (1 << 2)
#define XDMA_CTRL_IE_DESC_ALIGN_MISMATCH        (1 << 3)
#define XDMA_CTRL_IE_MAGIC_STOPPED              (1 << 4)
#define XDMA_CTRL_IE_IDLE_STOPPED               (1 << 6)
#define XDMA_CTRL_IE_READ_ERROR                 (0x1F << 9)
#define XDMA_CTRL_IE_DESC_ERROR                 (0x1F << 19)
#define XDMA_CTRL_NON_INCR_ADDR                 (1 << 25)
#define XDMA_CTRL_POLL_MODE_WB                  (1 << 26)

/* SGDMA descriptor control field bits */
#define XDMA_DESC_STOPPED       (1 << 0)
#define XDMA_DESC_COMPLETED     (1 << 1)
#define XDMA_DESC_EOP           (1 << 4)

/* DMA status register bits */
#define XDMA_STAT_BUSY                  (1 << 0)
#define XDMA_STAT_DESC_STOPPED          (1 << 1)
#define XDMA_STAT_DESC_COMPLETED        (1 << 2)
#define XDMA_STAT_ALIGN_MISMATCH        (1 << 3)
#define XDMA_STAT_MAGIC_STOPPED         (1 << 4)
#define XDMA_STAT_FETCH_STOPPED         (1 << 5)
#define XDMA_STAT_IDLE_STOPPED          (1 << 6)
#define XDMA_STAT_READ_ERROR            (0x1F << 9)
#define XDMA_STAT_DESC_ERROR            (0x1F << 19)

#define XDMA_WB_ERR_MASK                (1 << 31)

typedef struct {
    WDC_DEVICE_HANDLE hDev; /* Device handle */
    WD_DMA *pDma;           /* S/G DMA buffer for data transfer */
    PVOID pBuf;             /* Virtual buffer that represents DMA buffer */
    DWORD dwBytes;          /* DMA buffer size in bytes */
    UINT64 u64FPGAOffset;   /* FPGA offset */
    DWORD dwChannel;        /* DMA channel number */
    BOOL fToDevice;
    BOOL fPolling;
    BOOL fStreaming;
    BOOL fNonIncMode;
    WD_DMA *pDmaDesc;       /* S/G DMA descriptors */
    PVOID pDescBuf;         /* S/G DMA descriptors virtual buffer */
    WD_DMA *pWBDma;         /* Polling WriteBack DMA */
    PVOID pWBBuf;           /* Polling WriteBack DMA virtual buffer */
    PVOID pData;            /* Private data of the calling thread */
    UINT32 u32IrqBitMask;   /* Engine interrupt request bit(s) */
    BOOL fIsInitialized;    /* Is the engine struct (this struct) initialized */
    BOOL fIsEnabled;        /* Is the engine enabled on the card */
} XDMA_DMA_STRUCT;

/* XDMA device information struct */
typedef struct {
    XDMA_INT_HANDLER funcDiagIntHandler;     /* Interrupt handler routine */
    XDMA_EVENT_HANDLER funcDiagEventHandler; /* Event handler routine */
    DWORD dwConfigBarNum;                    /* Configuration BAR number. Can be
                                                BAR0 or BAR1, depending on FPGA
                                                configuration */
    DWORD dwEnabledIntType;                  /* Enabled Interrupt type. Possible
                                                values: INTERRUPT_MESSAGE_X,
                                                INTERRUPT_MESSAGE,
                                                INTERRUPT_LEVEL_SENSITIVE */
    WD_TRANSFER *pTrans;                     /* Interrupt transfer commands */

    XDMA_DMA_STRUCT pEnginesArr[XDMA_CHANNELS_NUM * 2]; /* Array of active XDMA
                                                            engines. */
} XDMA_DEV_CTX, *PXDMA_DEV_CTX;
/* TODO: You can add fields to store additional device-specific information. */

/* XDMA registers offsets */
enum {
    /* H2C Channel Registers. Up to 4 channels with 0x100 bytes spacing */
    XDMA_H2C_CHANNEL_IDENTIFIER_OFFSET                  = 0x0000,
    XDMA_H2C_CHANNEL_CONTROL_OFFSET                     = 0x0004,
    XDMA_H2C_CHANNEL_CONTROL_W1S_OFFSET                 = 0x0008,
    XDMA_H2C_CHANNEL_CONTROL_W1C_OFFSET                 = 0x000C,
    XDMA_H2C_CHANNEL_STATUS_OFFSET                      = 0x0040,
    XDMA_H2C_CHANNEL_STATUS_RC_OFFSET                   = 0x0044,
    XDMA_H2C_CHANNEL_COMPLETED_DESC_COUNT_OFFSET        = 0x0048,
    XDMA_H2C_CHANNEL_ALIGNMENTS_OFFSET                  = 0x004C,
    XDMA_H2C_CHANNEL_POLL_WRITE_BACK_ADDR_OFFSET        = 0x0088, /* 64 bit */
    XDMA_H2C_CHANNEL_POLL_LOW_WRITE_BACK_ADDR_OFFSET    = 0x0088, /* Low 32 bit */
    XDMA_H2C_CHANNEL_POLL_HIGH_WRITE_BACK_ADDR_OFFSET   = 0x008C, /* High 32 bit */
    XDMA_H2C_CHANNEL_INT_ENABLE_MASK_OFFSET             = 0x0090,
    XDMA_H2C_CHANNEL_INT_ENABLE_MASK_W1S_OFFSET         = 0x0094,
    XDMA_H2C_CHANNEL_INT_ENABLE_MASK_W1C_OFFSET         = 0x0098,
    XDMA_H2C_CHANNEL_PERFORMANCE_MONITOR_CONTROL_OFFSET = 0x00C0,
    XDMA_H2C_CHANNEL_PERFORMANCE_CYCLE_COUNT_OFFSET     = 0x00C4,
    XDMA_H2C_CHANNEL_PERFORMANCE_DATA_COUNT_OFFSET      = 0x00CC,

    /* C2H Channel Registers. Up to 4 channels with 0x100 bytes spacing */
    XDMA_C2H_CHANNEL_IDENTIFIER_OFFSET                  = 0x1000,
    XDMA_C2H_CHANNEL_CONTROL_OFFSET                     = 0x1004,
    XDMA_C2H_CHANNEL_CONTROL_W1S_OFFSET                 = 0x1008,
    XDMA_C2H_CHANNEL_CONTROL_W1C_OFFSET                 = 0x100C,
    XDMA_C2H_CHANNEL_STATUS_OFFSET                      = 0x1040,
    XDMA_C2H_CHANNEL_STATUS_RC_OFFSET                   = 0x1044,
    XDMA_C2H_CHANNEL_COMPLETED_DESC_COUNT_OFFSET        = 0x1048,
    XDMA_C2H_CHANNEL_ALIGNMENTS_OFFSET                  = 0x104C,
    XDMA_C2H_CHANNEL_POLL_WRITE_BACK_ADDR_OFFSET        = 0x1088, /* 64 bit */
    XDMA_C2H_CHANNEL_POLL_LOW_WRITE_BACK_ADDR_OFFSET    = 0x1088, /* Low 32 bit */
    XDMA_C2H_CHANNEL_POLL_HIGH_WRITE_BACK_ADDR_OFFSET   = 0x108C, /* High 32 bit */
    XDMA_C2H_CHANNEL_INT_ENABLE_MASK_OFFSET             = 0x1090,
    XDMA_C2H_CHANNEL_INT_ENABLE_MASK_W1S_OFFSET         = 0x1094,
    XDMA_C2H_CHANNEL_INT_ENABLE_MASK_W1C_OFFSET         = 0x1098,
    XDMA_C2H_CHANNEL_PERFORMANCE_MONITOR_CONTROL_OFFSET = 0x10C0,
    XDMA_C2H_CHANNEL_PERFORMANCE_CYCLE_COUNT_OFFSET     = 0x10C4,
    XDMA_C2H_CHANNEL_PERFORMANCE_DATA_COUNT_OFFSET      = 0x10CC,

    /* IRQ Block Registers */
    XDMA_IRQ_BLOCK_IDENTIFIER_OFFSET                    = 0x2000,
    XDMA_IRQ_BLOCK_USER_INT_ENABLE_MASK_OFFSET          = 0x2004,
    XDMA_IRQ_BLOCK_USER_INT_ENABLE_MASK_W1S_OFFSET      = 0x2008,
    XDMA_IRQ_BLOCK_USER_INT_ENABLE_MASK_W1C_OFFSET      = 0x200C,
    XDMA_IRQ_BLOCK_CHANNEL_INT_ENABLE_MASK_OFFSET       = 0x2010,
    XDMA_IRQ_BLOCK_CHANNEL_INT_ENABLE_MASK_W1S_OFFSET   = 0x2014,
    XDMA_IRQ_BLOCK_CHANNEL_INT_ENABLE_MASK_W1C_OFFSET   = 0x2018,
    XDMA_IRQ_USER_INT_REQUEST_OFFSET                    = 0x2040,
    XDMA_IRQ_BLOCK_CHANNEL_INT_REQUEST_OFFSET           = 0x2044,
    XDMA_IRQ_BLOCK_USER_INT_PENDING_OFFSET              = 0x2048,
    XDMA_IRQ_BLOCK_CHANNEL_INT_PENDING_OFFSET           = 0x204C,
    XDMA_IRQ_BLOCK_USER_VECTOR_1_OFFSET                 = 0x2080,
    XDMA_IRQ_BLOCK_USER_VECTOR_2_OFFSET                 = 0x2084,
    XDMA_IRQ_BLOCK_USER_VECTOR_3_OFFSET                 = 0x2088,
    XDMA_IRQ_BLOCK_USER_VECTOR_4_OFFSET                 = 0x208C,
    XDMA_IRQ_BLOCK_CHANNEL_VECTOR_1_OFFSET              = 0x20A0,
    XDMA_IRQ_BLOCK_CHANNEL_VECTOR_2_OFFSET              = 0x20A4,

    /* Config Block Registers */
    XDMA_CONFIG_BLOCK_IDENTIFIER_OFFSET                 = 0x3000,
    XDMA_CONFIG_BLOCK_BUSDEV_OFFSET                     = 0x3004,
    XDMA_CONFIG_BLOCK_PCIE_MAX_PAYLOAD_SIZE_OFFSET      = 0x3008,
    XDMA_CONFIG_BLOCK_PCIE_MAX_READ_REQUEST_SIZE_OFFSET = 0x300C,
    XDMA_CONFIG_BLOCK_SYSTEM_ID_OFFSET                  = 0x3010,
    XDMA_CONFIG_BLOCK_MSI_ENABLE_OFFSET                 = 0x3014,
    XDMA_CONFIG_BLOCK_PCIE_DATA_WIDTH_OFFSET            = 0x3018,
    XDMA_CONFIG_PCIE_CONTROL_OFFSET                     = 0x301C,
    XDMA_CONFIG_AXI_USER_MAX_PAYLOAD_SIZE_OFFSET        = 0x3040,
    XDMA_CONFIG_AXI_USER_MAX_READ_REQUSEST_SIZE_OFFSET  = 0x3044,
    XDMA_CONFIG_WRITE_FLUSH_TIMEOUT_OFFSET              = 0x3060,

    /* H2C SGDMA Registers */
    XDMA_H2C_SGDMA_IDENTIFIER_OFFSET                    = 0x4000,
    XDMA_H2C_SGDMA_DESC_OFFSET                          = 0x4080, /* 64 bit */
    XDMA_H2C_SGDMA_DESC_LOW_OFFSET                      = 0x4080, /* Low 32 bit */
    XDMA_H2C_SGDMA_DESC_HIGH_OFFSET                     = 0x4084, /* High 32 bit */
    XDMA_H2C_SGDMA_DESC_ADJACENT_OFFSET                 = 0x4088,

    /* C2H SGDMA Registers */
    XDMA_C2H_SGDMA_IDENTIFIER_OFFSET                    = 0x5000,
    XDMA_C2H_SGDMA_DESC_OFFSET                          = 0x5080, /* 64 bit */
    XDMA_C2H_SGDMA_DESC_LOW_OFFSET                      = 0x5080, /* Low 32 bit */
    XDMA_C2H_SGDMA_DESC_HIGH_OFFSET                     = 0x5084, /* High 32 bit */
    XDMA_C2H_SGDMA_DESC_ADJACENT_OFFSET                 = 0x5088,
};

/*************************************************************
  Function prototypes
 *************************************************************/
/* -----------------------------------------------
    XDMA and WDC libraries initialize/uninitialize
   ----------------------------------------------- */
/* Initialize the Xilinx XDMA and WDC libraries */
DWORD XDMA_LibInit(const CHAR *sLicense);
/* Uninitialize the Xilinx XDMA and WDC libraries */
DWORD XDMA_LibUninit(void);

#if !defined(__KERNEL__)
/* -----------------------------------------------
    Device open/close
   ----------------------------------------------- */
BOOL DeviceInit(WDC_DEVICE_HANDLE hDev);
/* Open a device handle */
WDC_DEVICE_HANDLE XDMA_DeviceOpen(DWORD dwVendorID, DWORD dwDeviceID);
/* Close a device handle */
BOOL XDMA_DeviceClose(WDC_DEVICE_HANDLE hDev);

#ifdef HAS_INTS
/* -----------------------------------------------
    Interrupts
   ----------------------------------------------- */
/* Enable interrupts */
DWORD XDMA_IntEnable(WDC_DEVICE_HANDLE hDev, XDMA_INT_HANDLER funcIntHandler);
/* Disable interrupts */
DWORD XDMA_IntDisable(WDC_DEVICE_HANDLE hDev);
/* Check whether interrupts are enabled for the given device */
BOOL XDMA_IntIsEnabled(WDC_DEVICE_HANDLE hDev);
/* Enable user interrupts */
DWORD XDMA_UserInterruptsEnable(WDC_DEVICE_HANDLE hDev, UINT32 mask);
/* Disable user interrupts */
DWORD XDMA_UserInterruptsDisable(WDC_DEVICE_HANDLE hDev, UINT32 mask);
/* Enable channel interrupts */
DWORD XDMA_ChannelInterruptsEnable(WDC_DEVICE_HANDLE hDev, UINT32 mask);
/* Disable channel interrupts */
DWORD XDMA_ChannelInterruptsDisable(WDC_DEVICE_HANDLE hDev, UINT32 mask);
#endif /* ifdef HAS_INTS */

/* -----------------------------------------------
    Direct Memory Access (DMA)
   ----------------------------------------------- */
/* Open a DMA handle: Allocate and initialize a XDMA DMA information structure,
 * including allocation of a scatter/gather DMA buffer */
DWORD XDMA_DmaOpen(WDC_DEVICE_HANDLE hDev, XDMA_DMA_HANDLE *phDma,
    DWORD dwBytes, UINT64 u64FPGAOffset, BOOL fToDevice, DWORD dwChannel,
    BOOL fPolling, BOOL fNonIncMode, PVOID pData, BOOL fIsTransaction);
/* Close DMA handle */
DWORD XDMA_DmaClose(XDMA_DMA_HANDLE hDma);
/* Start DMA transfer */
DWORD XDMA_DmaTransferStart(XDMA_DMA_HANDLE hDma);
/* Stop DMA transfer */
DWORD XDMA_DmaTransferStop(XDMA_DMA_HANDLE hDma);
/* Poll for DMA transfer completion */
DWORD XDMA_DmaPollCompletion(XDMA_DMA_HANDLE hDma);
/* Read XDMA engine status */
DWORD XDMA_EngineStatusRead(XDMA_DMA_HANDLE hDma, BOOL fClear, UINT32 *pStatus);
/* Returns DMA direction. TRUE - host to device, FALSE - device to host */
BOOL XDMA_DmaIsToDevice(XDMA_DMA_HANDLE hDma);
/* Returns pointer to the allocated virtual buffer and buffer size in bytes */
PVOID XDMA_DmaBufferGet(XDMA_DMA_HANDLE hDma, DWORD *pBytes);

DWORD XDMA_DmaTransactionTransferEnded(XDMA_DMA_HANDLE hDma);
DWORD XDMA_DmaTransactionExecute(XDMA_DMA_HANDLE hDma, BOOL fNewContext,
    PVOID pData);
DWORD XDMA_DmaTransactionRelease(XDMA_DMA_HANDLE hDma);

/* -----------------------------------------------
    Plug-and-play and power management events
   ----------------------------------------------- */
/* Register a plug-and-play or power management event */
DWORD XDMA_EventRegister(WDC_DEVICE_HANDLE hDev,
    XDMA_EVENT_HANDLER funcEventHandler);
/* Unregister a plug-and-play or power management event */
DWORD XDMA_EventUnregister(WDC_DEVICE_HANDLE hDev);
/* Check whether a given plug-and-play or power management event is registered
 */
BOOL XDMA_EventIsRegistered(WDC_DEVICE_HANDLE hDev);

#endif

/* -----------------------------------------------
    Address spaces information
   ----------------------------------------------- */
/* Get number of address spaces */
DWORD XDMA_GetNumAddrSpaces(WDC_DEVICE_HANDLE hDev);
/* Get address space information */
BOOL XDMA_GetAddrSpaceInfo(WDC_DEVICE_HANDLE hDev,
    XDMA_ADDR_SPACE_INFO *pAddrSpaceInfo);
/* Get configuration BAR number */
DWORD XDMA_ConfigBarNumGet(WDC_DEVICE_HANDLE hDev);

/* -----------------------------------------------
    Debugging and error handling
   ----------------------------------------------- */
/* Get last error */
const char *XDMA_GetLastErr(void);

#ifdef __cplusplus
}
#endif

#endif /* _XDMA_LIB_H_ */


