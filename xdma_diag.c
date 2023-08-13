/* Jungo Connectivity Confidential. Copyright (c) 2023 Jungo Connectivity Ltd.  https://www.jungo.com */

/****************************************************************************
*  File: xdma_diag.c
*
*  Sample user-mode diagnostics application for accessing Xilinx PCI Express
*  cards with XDMA support, using the WinDriver WDC API.
*
*  Note: This code sample is provided AS-IS and as a guiding sample only.
*****************************************************************************/

#include "xdma_diag_transfer.h"

/*************************************************************
  General definitions
 *************************************************************/
/* Error messages display */
int XDMA_printf(char *fmt, ...)
#if defined(LINUX)
    __attribute__((format(printf, 1, 2)))
#endif
;

#define XDMA_ERR XDMA_printf

/* --------------------------------------------------
    XDMA configuration registers information
   -------------------------------------------------- */
/* Configuration registers information array */
static const WDC_REG gXDMA_CfgRegs[] = {
    { WDC_AD_CFG_SPACE, PCI_VID, WDC_SIZE_16, WDC_READ_WRITE, "VID",
        "Vendor ID" },
    { WDC_AD_CFG_SPACE, PCI_DID, WDC_SIZE_16, WDC_READ_WRITE, "DID",
        "Device ID" },
    { WDC_AD_CFG_SPACE, PCI_CR, WDC_SIZE_16, WDC_READ_WRITE, "CMD",
        "Command" },
    { WDC_AD_CFG_SPACE, PCI_SR, WDC_SIZE_16, WDC_READ_WRITE, "STS", "Status" },
    { WDC_AD_CFG_SPACE, PCI_REV, WDC_SIZE_32, WDC_READ_WRITE, "RID_CLCD",
        "Revision ID & Class Code" },
    { WDC_AD_CFG_SPACE, PCI_CCSC, WDC_SIZE_8, WDC_READ_WRITE, "SCC",
        "Sub Class Code" },
    { WDC_AD_CFG_SPACE, PCI_CCBC, WDC_SIZE_8, WDC_READ_WRITE, "BCC",
        "Base Class Code" },
    { WDC_AD_CFG_SPACE, PCI_CLSR, WDC_SIZE_8, WDC_READ_WRITE, "CALN",
        "Cache Line Size" },
    { WDC_AD_CFG_SPACE, PCI_LTR, WDC_SIZE_8, WDC_READ_WRITE, "LAT",
        "Latency Timer" },
    { WDC_AD_CFG_SPACE, PCI_HDR, WDC_SIZE_8, WDC_READ_WRITE, "HDR",
        "Header Type" },
    { WDC_AD_CFG_SPACE, PCI_BISTR, WDC_SIZE_8, WDC_READ_WRITE, "BIST",
        "Built-in Self Test" },
    { WDC_AD_CFG_SPACE, PCI_BAR0, WDC_SIZE_32, WDC_READ_WRITE, "BADDR0",
        "Base Address 0" },
    { WDC_AD_CFG_SPACE, PCI_BAR1, WDC_SIZE_32, WDC_READ_WRITE, "BADDR1",
        "Base Address 1" },
    { WDC_AD_CFG_SPACE, PCI_BAR2, WDC_SIZE_32, WDC_READ_WRITE, "BADDR2",
        "Base Address 2" },
    { WDC_AD_CFG_SPACE, PCI_BAR3, WDC_SIZE_32, WDC_READ_WRITE, "BADDR3",
        "Base Address 3" },
    { WDC_AD_CFG_SPACE, PCI_BAR4, WDC_SIZE_32, WDC_READ_WRITE, "BADDR4",
        "Base Address 4" },
    { WDC_AD_CFG_SPACE, PCI_BAR5, WDC_SIZE_32, WDC_READ_WRITE, "BADDR5",
        "Base Address 5" },
    { WDC_AD_CFG_SPACE, PCI_CIS, WDC_SIZE_32, WDC_READ_WRITE, "CIS",
        "CardBus CIS Pointer" },
    { WDC_AD_CFG_SPACE, PCI_SVID, WDC_SIZE_16, WDC_READ_WRITE, "SVID",
        "Sub-system Vendor ID" },
    { WDC_AD_CFG_SPACE, PCI_SDID, WDC_SIZE_16, WDC_READ_WRITE, "SDID",
        "Sub-system Device ID" },
    { WDC_AD_CFG_SPACE, PCI_EROM, WDC_SIZE_32, WDC_READ_WRITE, "EROM",
        "Expansion ROM Base Address" },
    { WDC_AD_CFG_SPACE, PCI_CAP, WDC_SIZE_8, WDC_READ_WRITE, "NEW_CAP",
        "New Capabilities Pointer" },
    { WDC_AD_CFG_SPACE, PCI_ILR, WDC_SIZE_32, WDC_READ_WRITE, "INTLN",
        "Interrupt Line" },
    { WDC_AD_CFG_SPACE, PCI_IPR, WDC_SIZE_32, WDC_READ_WRITE, "INTPIN",
        "Interrupt Pin" },
    { WDC_AD_CFG_SPACE, PCI_MGR, WDC_SIZE_32, WDC_READ_WRITE, "MINGNT",
        "Minimum Required Burst Period" },
    { WDC_AD_CFG_SPACE, PCI_MLR, WDC_SIZE_32, WDC_READ_WRITE, "MAXLAT",
        "Maximum Latency" },
};

#define XDMA_CFG_REGS_NUM (sizeof(gXDMA_CfgRegs)/sizeof(WDC_REG))

/* -----------------------------------------------
    XDMA config block registers information
   ----------------------------------------------- */
/* Config block registers information array.
 * Note: The address space will be set after opening the device */
static WDC_REG gXDMA_ConfigRegs[] = {
    { (DWORD)-1, XDMA_CONFIG_BLOCK_IDENTIFIER_OFFSET, WDC_SIZE_32, WDC_READ,
        "Identifier", "" },
    { (DWORD)-1, XDMA_CONFIG_BLOCK_BUSDEV_OFFSET, WDC_SIZE_16, WDC_READ,
        "BusDev", "" },
    { (DWORD)-1, XDMA_CONFIG_BLOCK_PCIE_MAX_PAYLOAD_SIZE_OFFSET, WDC_SIZE_8,
        WDC_READ, "PCIE Max Payload Size", "" },
    { (DWORD)-1, XDMA_CONFIG_BLOCK_PCIE_MAX_READ_REQUEST_SIZE_OFFSET,
        WDC_SIZE_8, WDC_READ, "PCIE Max Read Request Size", "" },
    { (DWORD)-1, XDMA_CONFIG_BLOCK_SYSTEM_ID_OFFSET, WDC_SIZE_16, WDC_READ,
        "System ID", "" },
    { (DWORD)-1, XDMA_CONFIG_BLOCK_MSI_ENABLE_OFFSET, WDC_SIZE_8, WDC_READ,
        "MSI Enable", "" },
    { (DWORD)-1, XDMA_CONFIG_BLOCK_PCIE_DATA_WIDTH_OFFSET, WDC_SIZE_8, WDC_READ,
        "PCIE Data Width", "" },
    { (DWORD)-1, XDMA_CONFIG_PCIE_CONTROL_OFFSET, WDC_SIZE_8, WDC_READ_WRITE,
        "PCIE Control", "" },
    { (DWORD)-1, XDMA_CONFIG_AXI_USER_MAX_PAYLOAD_SIZE_OFFSET, WDC_SIZE_8,
        WDC_READ_WRITE, "AXI User Max Payload Size", "" },
    { (DWORD)-1, XDMA_CONFIG_AXI_USER_MAX_READ_REQUSEST_SIZE_OFFSET, WDC_SIZE_8,
        WDC_READ_WRITE, "AXI User Max Read Request Size", "" },
    { (DWORD)-1, XDMA_CONFIG_WRITE_FLUSH_TIMEOUT_OFFSET, WDC_SIZE_8,
        WDC_READ_WRITE, "Write Flush Timeout", "" },
};

#define XDMA_CONFIG_REGS_NUM (sizeof(gXDMA_ConfigRegs)/sizeof(WDC_REG))

/*************************************************************
  Static functions prototypes
 *************************************************************/
/* -----------------------------------------------
    Main diagnostics menu
   ----------------------------------------------- */
static DIAG_MENU_OPTION *MenuMainInit(WDC_DEVICE_HANDLE *phDev);

/* -----------------------------------------------
   Device Open
   ----------------------------------------------- */
static void MenuDeviceOpenInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev);

/* -----------------------------------------------
    Read/write memory and I/O addresses
   ----------------------------------------------- */
static void MenuReadWriteAddrInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev);

/* -----------------------------------------------
    Read/write the configuration space
   ----------------------------------------------- */
static void MenuCfgInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev);

/* -----------------------------------------------
    Read/write the run-time regs
   ----------------------------------------------- */
static void MenuRwRegsInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev);

/* -----------------------------------------------
    Direct Memory Access (DMA)
   ---------------------------------------------- */
static void MenuDmaInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev);

/* ----------------------------------------------------
    Plug-and-play and power management events handling
   ---------------------------------------------------- */
static void MenuEventsInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev);


/*************************************************************
  Functions implementation
 *************************************************************/
static DWORD XDMA_Init(WDC_DEVICE_HANDLE *phDev)
{
    DWORD i, dwConfigBarNum, dwStatus = WD_STATUS_SUCCESS;

    /* Initialize the XDMA library */
    dwStatus = XDMA_LibInit(NULL);
    if (WD_STATUS_SUCCESS != dwStatus)
    {
        XDMA_ERR("xdma_diag: Failed to initialize the XDMA library: %s",
            XDMA_GetLastErr());
        return dwStatus;
    }

    /* Find and open a XDMA device (by default ID) */
    *phDev = XDMA_DeviceOpen(XDMA_DEFAULT_VENDOR_ID, XDMA_DEFAULT_DEVICE_ID);

    /* Get the configuration BAR number */
    dwConfigBarNum = XDMA_ConfigBarNumGet(*phDev);
    for (i = 0; i < XDMA_CONFIG_REGS_NUM; i++)
        gXDMA_ConfigRegs[i].dwAddrSpace = dwConfigBarNum;

    return dwStatus;
}

int main(void)
{
    WDC_DEVICE_HANDLE hDev = NULL;
    DIAG_MENU_OPTION *pMenuRoot;
    DWORD dwStatus;

    printf("\n");
    printf("XDMA diagnostic utility.\n");
    printf("Application accesses hardware using " WD_PROD_NAME ".\n");

    dwStatus = XDMA_Init(&hDev);
    if (dwStatus)
        return dwStatus;

    pMenuRoot = MenuMainInit(&hDev);

    /* Busy loop that runs the menu tree created above and communicating
        with the user */
    return DIAG_MenuRun(pMenuRoot);
}

/* -----------------------------------------------
    Main diagnostics menu
   ----------------------------------------------- */
static DWORD MenuMainExitCb(PVOID pCbCtx)
{
    WDC_DEVICE_HANDLE hDev = *(WDC_DEVICE_HANDLE *)pCbCtx;
    DWORD dwStatus = 0;

    /* Perform necessary cleanup before exiting the program: */
    /* Close the device handle */
    if (hDev && !XDMA_DeviceClose(hDev))
        XDMA_ERR("xdma_diag: Failed closing XDMA device: %s",
            XDMA_GetLastErr());

    /* Uninitialize libraries */
    dwStatus = XDMA_LibUninit();
    if (WD_STATUS_SUCCESS != dwStatus)
    {
        XDMA_ERR("xdma_diag: Failed to uninitialize the XDMA library: %s",
            XDMA_GetLastErr());
    }

    return dwStatus;
}

static DIAG_MENU_OPTION *MenuMainInit(WDC_DEVICE_HANDLE *phDev)
{
    static DIAG_MENU_OPTION menuRoot = { 0 };

    strcpy(menuRoot.cTitleName, "XDMA main menu");
    menuRoot.cbExit = MenuMainExitCb;

    menuRoot.pCbCtx = phDev;

    MenuCommonScanBusInit(&menuRoot);
    MenuDeviceOpenInit(&menuRoot, phDev);
    MenuReadWriteAddrInit(&menuRoot, phDev);
    MenuCfgInit(&menuRoot, phDev);
    MenuRwRegsInit(&menuRoot, phDev);
    MenuDmaInit(&menuRoot, phDev);
    MenuEventsInit(&menuRoot, phDev);

    return &menuRoot;
}

/* -----------------------------------------------
   Device Open
   ----------------------------------------------- */
static DWORD MenuDeviceOpenCb(PVOID pCbCtx)
{
    WDC_DEVICE_HANDLE *phDev = (WDC_DEVICE_HANDLE *)pCbCtx;

    if (*phDev && !XDMA_DeviceClose(*phDev))
    {
        XDMA_ERR("xdma_diag: Failed closing XDMA device: %s",
            XDMA_GetLastErr());
    }

    *phDev = XDMA_DeviceOpen(0, 0);

    return WD_STATUS_SUCCESS;
}

static void MenuDeviceOpenInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev)
{
    static DIAG_MENU_OPTION menuDeviceOpen = { 0 };

    strcpy(menuDeviceOpen.cOptionName, "Find and open a XDMA device");
    menuDeviceOpen.cbEntry = MenuDeviceOpenCb;

    DIAG_MenuSetCtxAndParentForMenus(&menuDeviceOpen, 1, phDev,
        pParentMenu);
}

/* -----------------------------------------------
    Read/write memory and I/O addresses
   ----------------------------------------------- */
static void MenuReadWriteAddrInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev)
{
    static MENU_CTX_READ_WRITE_ADDR rwAddrMenusCtx;

    rwAddrMenusCtx.phDev = phDev;
    rwAddrMenusCtx.fBlock = FALSE;
    rwAddrMenusCtx.mode = WDC_MODE_32;
    rwAddrMenusCtx.dwAddrSpace = ACTIVE_ADDR_SPACE_NEEDS_INIT;

    MenuCommonRwAddrInit(pParentMenu, &rwAddrMenusCtx);
}

/* -----------------------------------------------
    Read/write the configuration space
   ----------------------------------------------- */
static void MenuCfgInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev)
{
    static MENU_CTX_CFG cfgCtx;

    BZERO(cfgCtx);
    cfgCtx.phDev = phDev;
    cfgCtx.pCfgRegs = gXDMA_CfgRegs;
    cfgCtx.dwCfgRegsNum = XDMA_CFG_REGS_NUM;

    MenuCommonCfgInit(pParentMenu, &cfgCtx);
}

/* -----------------------------------------------
    Read/write the run-time registers
   ----------------------------------------------- */
static void MenuRwRegsInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev)
{
    static MENU_CTX_RW_REGS regsMenusCtx;

    BZERO(regsMenusCtx);

    regsMenusCtx.phDev = phDev;
    regsMenusCtx.pRegsArr = gXDMA_ConfigRegs;
    regsMenusCtx.dwRegsNum = XDMA_CONFIG_REGS_NUM;
    regsMenusCtx.fIsConfig = TRUE;
    strcpy(regsMenusCtx.sModuleName, "XDMA");

    MenuCommonRwRegsInit(pParentMenu, &regsMenusCtx);
}

/* ----------------------------------------------------
    Plug-and-play and power management events handling
   ---------------------------------------------------- */

/* Diagnostics plug-and-play and power management events handler routine */
static void DiagEventHandler(WDC_DEVICE_HANDLE hDev, DWORD dwAction)
{
    /* TODO: You can modify this function in order to implement your own
             diagnostics events handler routine. */

    printf("\nReceived event notification (device handle 0x%p): ", hDev);
    switch (dwAction)
    {
    case WD_INSERT:
        printf("WD_INSERT\n");
        break;
    case WD_REMOVE:
        printf("WD_REMOVE\n");
        break;
    case WD_POWER_CHANGED_D0:
        printf("WD_POWER_CHANGED_D0\n");
        break;
    case WD_POWER_CHANGED_D1:
        printf("WD_POWER_CHANGED_D1\n");
        break;
    case WD_POWER_CHANGED_D2:
        printf("WD_POWER_CHANGED_D2\n");
        break;
    case WD_POWER_CHANGED_D3:
        printf("WD_POWER_CHANGED_D3\n");
        break;
    case WD_POWER_SYSTEM_WORKING:
        printf("WD_POWER_SYSTEM_WORKING\n");
        break;
    case WD_POWER_SYSTEM_SLEEPING1:
        printf("WD_POWER_SYSTEM_SLEEPING1\n");
        break;
    case WD_POWER_SYSTEM_SLEEPING2:
        printf("WD_POWER_SYSTEM_SLEEPING2\n");
        break;
    case WD_POWER_SYSTEM_SLEEPING3:
        printf("WD_POWER_SYSTEM_SLEEPING3\n");
        break;
    case WD_POWER_SYSTEM_HIBERNATE:
        printf("WD_POWER_SYSTEM_HIBERNATE\n");
        break;
    case WD_POWER_SYSTEM_SHUTDOWN:
        printf("WD_POWER_SYSTEM_SHUTDOWN\n");
        break;
    default:
        printf("0x%x\n", dwAction);
        break;
    }
}

static DWORD MenuEventsRegisterOptionCb(PVOID pCbCtx)
{
    MENU_CTX_EVENTS *pEventsMenusCtx = (MENU_CTX_EVENTS *)pCbCtx;
    DWORD dwStatus = XDMA_EventRegister(*(pEventsMenusCtx->phDev),
        (XDMA_EVENT_HANDLER)pEventsMenusCtx->DiagEventHandler);

    if (WD_STATUS_SUCCESS == dwStatus)
    {
        printf("Events registered\n");
        pEventsMenusCtx->fRegistered = TRUE;
    }
    else
    {
        XDMA_ERR("Failed to register events. Last error [%s]\n",
            XDMA_GetLastErr());
    }

    return dwStatus;
}

static DWORD MenuEventsUnregisterOptionCb(PVOID pCbCtx)
{
    MENU_CTX_EVENTS *pEventsMenusCtx = (MENU_CTX_EVENTS *)pCbCtx;
    DWORD dwStatus = XDMA_EventUnregister(*(pEventsMenusCtx->phDev));

    if (WD_STATUS_SUCCESS == dwStatus)
    {
        printf("Events unregistered\n");
        pEventsMenusCtx->fRegistered = FALSE;
    }
    else
    {
        XDMA_ERR("Failed to unregister events. Last error [%s]\n",
            XDMA_GetLastErr());
    }

    return dwStatus;
}

static DWORD MenuEventsCb(PVOID pCbCtx)
{
    MENU_CTX_EVENTS *pEventsMenusCtx = (MENU_CTX_EVENTS *)pCbCtx;
    pEventsMenusCtx->fRegistered = XDMA_EventIsRegistered(
        *pEventsMenusCtx->phDev);

#ifdef WIN32
    if (!pEventsMenusCtx->fRegistered)
    {
        printf("NOTICE: An INF must be installed for your device in order to \n"
            "        call your user-mode event handler.\n"
            "        You can generate an INF file using the DriverWizard.\n");
    }
#endif

    return WD_STATUS_SUCCESS;
}

static void MenuEventsInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev)
{
    static MENU_EVENTS_CALLBACKS eventsMenuCbs = { 0 };
    static MENU_CTX_EVENTS eventsMenusCtx = { 0 };

    eventsMenuCbs.eventsMenuEntryCb = MenuEventsCb;
    eventsMenuCbs.eventsEnableCb = MenuEventsRegisterOptionCb;
    eventsMenuCbs.eventsDisableCb = MenuEventsUnregisterOptionCb;

    eventsMenusCtx.phDev = phDev;
    eventsMenusCtx.DiagEventHandler = (DIAG_EVENT_HANDLER)DiagEventHandler;

    MenuCommonEventsInit(pParentMenu, &eventsMenusCtx, &eventsMenuCbs);
}

/* -----------------------------------------------
    Direct Memory Access (DMA)
   ---------------------------------------------- */
typedef struct {
    WDC_DEVICE_HANDLE *phDev;
    XDMA_DMA_HANDLE hDma;
    BOOL fPolling;
    BOOL fIsTransaction;
    DWORD dwPerfOption;
} MENU_CTX_DMA;

static BOOL MenuDmaIsDeviceNull(DIAG_MENU_OPTION *pMenu)
{
    return *(((MENU_CTX_DMA *)pMenu->pCbCtx)->phDev) == NULL;
}

static BOOL MenuDmaCompletionMethodGetInput(BOOL *pfPolling)
{
    DWORD option;

    printf("\nSelect DMA completion method:");
    printf("\n-----------------------------\n");
    printf("1. Interrupts\n");
    printf("2. Polling\n");
    printf("%d. Cancel\n", DIAG_EXIT_MENU);

    if ((DIAG_INPUT_SUCCESS != DIAG_GetMenuOption(&option, 2)) ||
        (DIAG_EXIT_MENU == option))
    {
        return FALSE;
    }

    *pfPolling = (1 == option) ? FALSE : TRUE;
    return TRUE;
}

/* -----------------------------------------------
    DMA Performance
   ---------------------------------------------- */

/* DMA user input menu */
static BOOL MenuDmaPerformanceGetInput(BOOL *pfPolling, DWORD *pdwBytes,
    DWORD *pdwSeconds)
{
    DIAG_INPUT_RESULT inputResult;

    if (!MenuDmaCompletionMethodGetInput(pfPolling))
        return FALSE;

    inputResult = DIAG_InputDWORD(pdwBytes, "\nEnter single transfer buffer size"
        " in KBs", FALSE, 0, 0);
    switch (inputResult)
    {
    case DIAG_INPUT_SUCCESS:
        break;
    case DIAG_INPUT_FAIL:
        XDMA_ERR("\nInvalid transfer buffer size\n");
        return FALSE;
    case DIAG_INPUT_CANCEL:
        return FALSE;
    }

    *pdwBytes *= 1024;

    inputResult = DIAG_InputDWORD(pdwSeconds, "\nEnter test duration in seconds",
        FALSE, 0, 0);
    switch (inputResult)
    {
    case DIAG_INPUT_SUCCESS:
        break;
    case DIAG_INPUT_FAIL:
        XDMA_ERR("\nInvalid test duration\n");
        return FALSE;
    case DIAG_INPUT_CANCEL:
        return FALSE;
    }

    printf("\n");

    return TRUE;
}

static DWORD DmaPerformance(WDC_DEVICE_HANDLE hDev, BOOL fIsTransaction,
    DWORD dwPerfOption)
{
    DWORD dwBytes, dwSeconds;
    BOOL fPolling;

    if (!MenuDmaPerformanceGetInput(&fPolling, &dwBytes, &dwSeconds))
        return WD_INVALID_PARAMETER;

    XDMA_DIAG_DmaPerformance(hDev,
        dwPerfOption, dwBytes, fPolling, dwSeconds,
        fIsTransaction);

    return WD_STATUS_SUCCESS;
}
static DWORD MenuDmaHostToDevPerformanceOptionCb(PVOID pCbCtx)
{
    MENU_CTX_DMA *pDmaCtx = ((MENU_CTX_DMA *)pCbCtx);

    return DmaPerformance(*(pDmaCtx->phDev), pDmaCtx->fIsTransaction,
        MENU_DMA_PERF_TO_DEV);
}

static DWORD MenuDmaDevToHostPerformanceOptionCb(PVOID pCbCtx)
{
    MENU_CTX_DMA *pDmaCtx = ((MENU_CTX_DMA *)pCbCtx);

    return DmaPerformance(*(pDmaCtx->phDev), pDmaCtx->fIsTransaction,
        MENU_DMA_PERF_FROM_DEV);
}

static DWORD MenuDmaBiDirPerformanceOptionCb(PVOID pCbCtx)
{
    MENU_CTX_DMA *pDmaCtx = ((MENU_CTX_DMA *)pCbCtx);

    return DmaPerformance(*(pDmaCtx->phDev), pDmaCtx->fIsTransaction,
        MENU_DMA_PERF_BIDIR);
}

static void MenuDmaPerformanceInit(DIAG_MENU_OPTION *pParentMenu,
    MENU_CTX_DMA *pDmaCtx)
{
    static DIAG_MENU_OPTION hostToDevicePerformanceMenu = { 0 };
    static DIAG_MENU_OPTION deviceToHostPerformanceMenu = { 0 };
    static DIAG_MENU_OPTION simultaneouslyPerformanceMenu = { 0 };
    static DIAG_MENU_OPTION options[3] = { 0 };

    strcpy(hostToDevicePerformanceMenu.cOptionName, "DMA host-to-device "
        "performance");
    hostToDevicePerformanceMenu.cbEntry = MenuDmaHostToDevPerformanceOptionCb;

    strcpy(deviceToHostPerformanceMenu.cOptionName, "DMA device-to-host "
        "performance");
    deviceToHostPerformanceMenu.cbEntry = MenuDmaDevToHostPerformanceOptionCb;

    strcpy(simultaneouslyPerformanceMenu.cOptionName, "DMA host-to-device and "
        "device-to-host performance running simultaneously");
    simultaneouslyPerformanceMenu.cbEntry = MenuDmaBiDirPerformanceOptionCb;

    options[0] = hostToDevicePerformanceMenu;
    options[1] = deviceToHostPerformanceMenu;
    options[2] = simultaneouslyPerformanceMenu;

    DIAG_MenuSetCtxAndParentForMenus(options, OPTIONS_SIZE(options),
        pDmaCtx, pParentMenu);
}

/* -----------------------------------------------
    DMA Transfers
   ---------------------------------------------- */

static BOOL MenuDmaIsDmaHandleNotNull(DIAG_MENU_OPTION *pMenu)
{
    return ((MENU_CTX_DMA *)pMenu->pCbCtx)->hDma != NULL;
}

static BOOL MenuDmaIsDmaHandleNull(DIAG_MENU_OPTION *pMenu)
{
    return ((MENU_CTX_DMA *)pMenu->pCbCtx)->hDma == NULL;
}

/* DMA user input menu */
static BOOL MenuDmaTransferGetInput(DWORD *pdwChannel, BOOL *pfToDevice,
    UINT32 *pu32Pattern, DWORD *pdwNumPackets, UINT64 *pu64FPGAOffset,
    BOOL *pfPolling)
{
    DWORD option;

    if (!MenuDmaCompletionMethodGetInput(pfPolling))
        return FALSE;

    /* Get DMA direction and set the DMA options accordingly */
    printf("\nSelect DMA direction:");
    printf("\n---------------------\n");
    printf("1. From device\n");
    printf("2. To device\n");
    printf("%d. Cancel\n", DIAG_EXIT_MENU);

    if ((DIAG_INPUT_SUCCESS != DIAG_GetMenuOption(&option, 2)) ||
        (DIAG_EXIT_MENU == option))
    {
        return FALSE;
    }

    *pfToDevice = (1 == option) ? FALSE : TRUE;

    /* Get DMA buffer pattern for host to device transfer */
    if (DIAG_INPUT_SUCCESS != DIAG_InputDWORD(pdwChannel,
        "\nSelect DMA channel (0 - 3)", FALSE, 0, 3))
    {
        return FALSE;
    }

    if (*pfToDevice)
    {
        /* Get DMA buffer pattern for host to device transfer */
        if (DIAG_INPUT_SUCCESS != DIAG_InputUINT32(pu32Pattern,
            "\nEnter DMA data pattern as 32 bit packet", TRUE, 0, 0))
        {
            return FALSE;
        }
    }

    /* Get data pattern */
    if (DIAG_INPUT_SUCCESS != DIAG_InputDWORD(pdwNumPackets,
        "\nEnter number of packets to transfer (32 bit packets)", FALSE, 0, 0))
    {
        return FALSE;
    }

    if (*pdwNumPackets == 0)
    {
        XDMA_ERR("Illegal number of packets\n");
        return FALSE;
    }

    if (DIAG_INPUT_SUCCESS != DIAG_InputUINT64(pu64FPGAOffset,
        "\nEnter FPGA offset for transfer", TRUE, 0, 0))
    {
        return FALSE;
    }


    printf("\n");

    return TRUE;
}

static DWORD MenuDmaCloseOptionCb(PVOID pCbCtx)
{
    MENU_CTX_DMA *pDmaCtx = (MENU_CTX_DMA *)pCbCtx;

    if (pDmaCtx->hDma)
    {
        XDMA_DIAG_DmaClose(*(pDmaCtx->phDev), pDmaCtx->hDma);
        pDmaCtx->hDma = NULL;
        printf("Closed DMA handle\n");
    }

    return WD_STATUS_SUCCESS;
}

/* -----------------------------------------------
    DMA Signle Transfer
   ---------------------------------------------- */
static DWORD MenuDmaSingleTransferOpenOptionCb(PVOID pCbCtx)
{
    MENU_CTX_DMA *pDmaCtx = (MENU_CTX_DMA *)pCbCtx;
    BOOL fPolling, fToDevice;
    DWORD dwNumPackets, dwChannel;
    UINT32 u32Pattern;
    UINT64 u64FPGAOffset;


    if (!MenuDmaTransferGetInput(&dwChannel, &fToDevice,
        &u32Pattern, &dwNumPackets, &u64FPGAOffset, &fPolling))
        return WD_INVALID_PARAMETER;

    pDmaCtx->hDma = XDMA_DIAG_DmaOpen(*(pDmaCtx->phDev), fPolling, dwChannel,
        fToDevice, u32Pattern, dwNumPackets, u64FPGAOffset,
        FALSE);
    if (!(pDmaCtx->hDma))
        XDMA_ERR("Failed opening DMA handle\n");

    return WD_STATUS_SUCCESS;
}

static void MenuDmaSingleTransferInit(DIAG_MENU_OPTION *pParentMenu,
    MENU_CTX_DMA *pMenuDmaTransferCtx)
{
    static DIAG_MENU_OPTION openDmaMenu = { 0 };
    static DIAG_MENU_OPTION closeDmaMenu = { 0 };
    static DIAG_MENU_OPTION options[2] = { 0 };

    strcpy(openDmaMenu.cOptionName, "Open DMA");
    openDmaMenu.cbEntry = MenuDmaSingleTransferOpenOptionCb;
    openDmaMenu.cbIsHidden = MenuDmaIsDmaHandleNotNull;

    strcpy(closeDmaMenu.cOptionName, "Close DMA");
    closeDmaMenu.cbEntry = MenuDmaCloseOptionCb;
    closeDmaMenu.cbIsHidden = MenuDmaIsDmaHandleNull;

    options[0] = openDmaMenu;
    options[1] = closeDmaMenu;

    DIAG_MenuSetCtxAndParentForMenus(options, OPTIONS_SIZE(options),
        pMenuDmaTransferCtx, pParentMenu);
}

static void MenuDmaSingleTransferSubMenusInit(DIAG_MENU_OPTION *pParentMenu,
    MENU_CTX_DMA *pMenuDmaCtx)
{
    static DIAG_MENU_OPTION menuDmaSingleTransfer = { 0 };
    static DIAG_MENU_OPTION menuDmaPerformance = { 0 };

    strcpy(menuDmaSingleTransfer.cOptionName, "Perform DMA transfer");
    strcpy(menuDmaSingleTransfer.cTitleName, "Open/close Direct Memory Access "
        "(DMA)");
    menuDmaSingleTransfer.cbExit = MenuDmaCloseOptionCb;

    strcpy(menuDmaPerformance.cOptionName, "Measure DMA performance");
    strcpy(menuDmaPerformance.cTitleName, "DMA performance");

    MenuDmaSingleTransferInit(&menuDmaSingleTransfer, pMenuDmaCtx);
    DIAG_MenuSetCtxAndParentForMenus(&menuDmaSingleTransfer, 1,
        pMenuDmaCtx, pParentMenu);

    MenuDmaPerformanceInit(&menuDmaPerformance, pMenuDmaCtx);
    DIAG_MenuSetCtxAndParentForMenus(&menuDmaPerformance, 1, pMenuDmaCtx,
        pParentMenu);
}
/* -----------------------------------------------
    DMA Transaction
   ---------------------------------------------- */

static DWORD MenuDmaTransactionInitOptionCb(PVOID pCbCtx)
{
    MENU_CTX_DMA *pMenuDmaTransferCtx = (MENU_CTX_DMA *)pCbCtx;
    BOOL fToDevice;
    DWORD dwNumPackets, dwChannel;
    UINT32 u32Pattern;
    UINT64 u64FPGAOffset;

    if (!MenuDmaTransferGetInput(&dwChannel, &fToDevice,
        &u32Pattern, &dwNumPackets, &u64FPGAOffset,
        &pMenuDmaTransferCtx->fPolling))
    {
        goto Exit;
    }

    pMenuDmaTransferCtx->hDma = XDMA_DIAG_DmaOpen(
        *(pMenuDmaTransferCtx->phDev),
        pMenuDmaTransferCtx->fPolling, dwChannel,
        fToDevice, u32Pattern, dwNumPackets, u64FPGAOffset, TRUE);
    if (!(pMenuDmaTransferCtx->hDma))
        XDMA_ERR("Failed opening DMA handle\n");

Exit:
    return WD_STATUS_SUCCESS;
}

static DWORD MenuDmaTransactionExecuteOptionCb(PVOID pCbCtx)
{
    MENU_CTX_DMA *pMenuDmaTransferCtx = (MENU_CTX_DMA *)pCbCtx;

    XDMA_DIAG_DmaTransactionExecute(pMenuDmaTransferCtx->hDma,
        pMenuDmaTransferCtx->fPolling);

    return WD_STATUS_SUCCESS;
}

static DWORD MenuDmaTransactionReleaseOptionCb(PVOID pCbCtx)
{
    XDMA_DmaTransactionRelease(((MENU_CTX_DMA *)pCbCtx)->hDma);

    return WD_STATUS_SUCCESS;
}

static DWORD MenuDmaTransactionShowBufferOptionCb(PVOID pCbCtx)
{
    XDMA_DIAG_DumpDmaBuffer(((MENU_CTX_DMA *)pCbCtx)->hDma);

    return WD_STATUS_SUCCESS;
}

static void MenuDmaTransactionInit(DIAG_MENU_OPTION *pParentMenu,
    MENU_CTX_DMA *pMenuDmaTransferCtx)
{
    static DIAG_MENU_OPTION initTransactionDmaMenu = { 0 };
    static DIAG_MENU_OPTION releaseTransactionDmaMenu = { 0 };
    static DIAG_MENU_OPTION executeTransactionDmaMenu = { 0 };
    static DIAG_MENU_OPTION displayBufferMenu = { 0 };
    static DIAG_MENU_OPTION uninitTransactionDmaMenu = { 0 };
    static DIAG_MENU_OPTION options[5] = { 0 };

    strcpy(initTransactionDmaMenu.cOptionName, "Initialize transaction DMA");
    initTransactionDmaMenu.cbEntry = MenuDmaTransactionInitOptionCb;
    initTransactionDmaMenu.cbIsHidden = MenuDmaIsDmaHandleNotNull;

    strcpy(executeTransactionDmaMenu.cOptionName, "Execute transaction");
    executeTransactionDmaMenu.cbEntry = MenuDmaTransactionExecuteOptionCb;
    executeTransactionDmaMenu.cbIsHidden = MenuDmaIsDmaHandleNull;

    strcpy(releaseTransactionDmaMenu.cOptionName, "Release transaction");
    releaseTransactionDmaMenu.cbEntry = MenuDmaTransactionReleaseOptionCb;
    releaseTransactionDmaMenu.cbIsHidden = MenuDmaIsDmaHandleNull;

    strcpy(displayBufferMenu.cOptionName, "Display transferred buffer "
        "content");
    displayBufferMenu.cbEntry = MenuDmaTransactionShowBufferOptionCb;
    displayBufferMenu.cbIsHidden = MenuDmaIsDmaHandleNull;

    strcpy(uninitTransactionDmaMenu.cOptionName, "Uninitialize DMA "
        "transaction");
    uninitTransactionDmaMenu.cbEntry = MenuDmaCloseOptionCb;
    uninitTransactionDmaMenu.cbIsHidden = MenuDmaIsDmaHandleNull;

    options[0] = initTransactionDmaMenu;
    options[1] = executeTransactionDmaMenu;
    options[2] = releaseTransactionDmaMenu;
    options[3] = displayBufferMenu;
    options[4] = uninitTransactionDmaMenu;

    DIAG_MenuSetCtxAndParentForMenus(options, OPTIONS_SIZE(options),
        pMenuDmaTransferCtx, pParentMenu);
}

static void MenuDmaTransactionSubMenusInit(DIAG_MENU_OPTION *pParentMenu,
    MENU_CTX_DMA *pMenuDmaCtx)
{
    static DIAG_MENU_OPTION menuDmaTransaction = { 0 };
    static DIAG_MENU_OPTION menuDmaPerformance = { 0 };

    strcpy(menuDmaTransaction.cOptionName, "Perform DMA transfer");
    strcpy(menuDmaTransaction.cTitleName, "Initialize/Uninitialize Direct "
        "Memory Access (DMA) transaction");
    menuDmaTransaction.cbExit = MenuDmaCloseOptionCb;

    strcpy(menuDmaPerformance.cOptionName, "Measure DMA performance");
    strcpy(menuDmaPerformance.cTitleName, "DMA performance");

    MenuDmaTransactionInit(&menuDmaTransaction, pMenuDmaCtx);
    DIAG_MenuSetCtxAndParentForMenus(&menuDmaTransaction, 1,
        pMenuDmaCtx, pParentMenu);

    MenuDmaPerformanceInit(&menuDmaPerformance, pMenuDmaCtx);
    DIAG_MenuSetCtxAndParentForMenus(&menuDmaPerformance, 1, pMenuDmaCtx,
        pParentMenu);
}

static DWORD MenuDmaNoneTransactionOptionCb(PVOID pCbCtx)
{
    ((MENU_CTX_DMA *)pCbCtx)->fIsTransaction = FALSE;

    return WD_STATUS_SUCCESS;
}

static DWORD MenuDmaTransactionOptionCb(PVOID pCbCtx)
{
    ((MENU_CTX_DMA *)pCbCtx)->fIsTransaction = TRUE;

    return WD_STATUS_SUCCESS;
}

static void MenuDmaInit(DIAG_MENU_OPTION *pParentMenu,
    WDC_DEVICE_HANDLE *phDev)
{
    static DIAG_MENU_OPTION dmaSingleTransferOption = { 0 };
    static DIAG_MENU_OPTION dmaTransactionOption = { 0 };
    static MENU_CTX_DMA menuDmaCtx = { 0 };

    strcpy(dmaSingleTransferOption.cOptionName, "Direct Memory Access (DMA)");
    strcpy(dmaSingleTransferOption.cTitleName, "XDMA DMA menu");
    dmaSingleTransferOption.cbIsHidden = MenuDmaIsDeviceNull;
    dmaSingleTransferOption.cbEntry = MenuDmaNoneTransactionOptionCb;

    strcpy(dmaTransactionOption.cOptionName, "Direct Memory Access (DMA) transaction");
    strcpy(dmaTransactionOption.cTitleName, "XDMA DMA menu");
    dmaTransactionOption.cbIsHidden = MenuDmaIsDeviceNull;
    dmaTransactionOption.cbEntry = MenuDmaTransactionOptionCb;

    menuDmaCtx.phDev = phDev;

    /* Single transfer branch */
    MenuDmaSingleTransferSubMenusInit(&dmaSingleTransferOption, &menuDmaCtx);
    DIAG_MenuSetCtxAndParentForMenus(&dmaSingleTransferOption, 1, &menuDmaCtx,
        pParentMenu);

    /* Transaction branch */
    MenuDmaTransactionSubMenusInit(&dmaTransactionOption, &menuDmaCtx);
    DIAG_MenuSetCtxAndParentForMenus(&dmaTransactionOption, 1, &menuDmaCtx,
        pParentMenu);

}


int XDMA_printf(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    return 0;
}


