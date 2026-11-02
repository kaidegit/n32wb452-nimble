/*
 * NimBLE syscfg for the N32WB452 firmware (host-only, FreeRTOS NPL,
 * external NZ8801 controller behind a custom SPI HCI transport).
 *
 * Self-contained: does not include upstream syscfg.h. All NimBLE defaults
 * are #ifndef-guarded, and this directory comes first in the include path.
 */
#ifndef H_MYNEWT_SYSCFG_
#define H_MYNEWT_SYSCFG_

#define MYNEWT_VAL(_name)              MYNEWT_VAL_ ## _name
#define MYNEWT_VAL_CHOICE(_name, _val) MYNEWT_VAL_ ## _name ## __ ## _val

/* ===== Platform ===== */
#define MYNEWT_VAL_BSP_SIMULATED               (0)
#define MYNEWT_VAL_MCU_NATIVE                  (0)
#define MYNEWT_VAL_MCU_NATIVE_USE_SIGNALS      (0)

/* ===== Transport: native host + custom (SPI) link layer ===== */
#define MYNEWT_VAL_BLE_TRANSPORT_HS__native    (1)
#define MYNEWT_VAL_BLE_TRANSPORT_HS__custom    (0)
#define MYNEWT_VAL_BLE_TRANSPORT_HS__uart      (0)
#define MYNEWT_VAL_BLE_TRANSPORT_HS__usb       (0)
#define MYNEWT_VAL_BLE_TRANSPORT_HS__cdc       (0)
#define MYNEWT_VAL_BLE_TRANSPORT_HS__dialog_cmac (0)
#define MYNEWT_VAL_BLE_TRANSPORT_HS__nrf5340   (0)
#define MYNEWT_VAL_BLE_TRANSPORT_HS            (1)

#define MYNEWT_VAL_BLE_TRANSPORT_LL__native    (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__custom    (1)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__socket    (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__emspi     (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__uart_ll   (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__dialog_cmac (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__nrf5340   (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__apollo3   (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL            (1)
#define MYNEWT_VAL_BLE_TRANSPORT               (1)

#define MYNEWT_VAL_BLE_TRANSPORT_EVT_COUNT              (4)
#define MYNEWT_VAL_BLE_TRANSPORT_EVT_DISCARDABLE_COUNT  (4)
#define MYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE               (70)
#define MYNEWT_VAL_BLE_TRANSPORT_ACL_COUNT              (8)
#define MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_HS_COUNT      (4)
#define MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT      (4)
#define MYNEWT_VAL_BLE_TRANSPORT_ACL_SIZE               (256)
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_COUNT              (0)
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_HS_COUNT      (0)
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_LL_COUNT      (0)
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_SIZE               (0)

/* ===== Host ===== */
#define MYNEWT_VAL_BLE_HOST                    (1)
#define MYNEWT_VAL_BLE_HS_AUTO_START           (1)
#define MYNEWT_VAL_BLE_HS_REQUIRE_OS           (1)
#define MYNEWT_VAL_BLE_HS_FLOW_CTRL            (0)
#define MYNEWT_VAL_BLE_HS_STOP_ON_SHUTDOWN     (1)

/* ===== Roles: peripheral + broadcaster ===== */
#define MYNEWT_VAL_BLE_ROLE_PERIPHERAL         (1)
#define MYNEWT_VAL_BLE_ROLE_CENTRAL            (0)
#define MYNEWT_VAL_BLE_ROLE_BROADCASTER        (1)
#define MYNEWT_VAL_BLE_ROLE_OBSERVER           (0)
#define MYNEWT_VAL_BLE_MAX_CONNECTIONS         (1)

/* ===== Security Manager (legacy pairing only) ===== */
#define MYNEWT_VAL_BLE_SM_LEGACY               (1)
#define MYNEWT_VAL_BLE_SM_SC                   (0)
#define MYNEWT_VAL_BLE_SM_IO_CAP               (BLE_HS_IO_NO_INPUT_OUTPUT)
#define MYNEWT_VAL_BLE_SM_BONDING              (0)
#define MYNEWT_VAL_BLE_SM_MITM                 (0)
#define MYNEWT_VAL_BLE_SM_MAX_PROCS            (1)
#define MYNEWT_VAL_BLE_SM_OOB_DATA_FLAG        (0)
#define MYNEWT_VAL_BLE_SM_OUR_KEY_DIST         (0)
#define MYNEWT_VAL_BLE_SM_THEIR_KEY_DIST       (0)
#define MYNEWT_VAL_BLE_SM_KEYPRESS             (0)
#define MYNEWT_VAL_BLE_SM_SC_DEBUG_KEYS        (0)
#define MYNEWT_VAL_BLE_SM_SC_ONLY              (0)
#define MYNEWT_VAL_BLE_SM_LVL                  (0)
#define MYNEWT_VAL_BLE_SM_CSIS_SIRK            (0)

/* ===== Memory pools ===== */
#define MYNEWT_VAL_MSYS_1_BLOCK_COUNT          (24)
#define MYNEWT_VAL_MSYS_1_BLOCK_SIZE           (300)
#define MYNEWT_VAL_MSYS_2_BLOCK_COUNT          (0)
#define MYNEWT_VAL_MSYS_2_BLOCK_SIZE           (0)

/* ===== ATT / GATT ===== */
#define MYNEWT_VAL_BLE_ATT_PREFERRED_MTU       (247)
#define MYNEWT_VAL_BLE_GATT_MAX_PROCS          (4)
#define MYNEWT_VAL_BLE_ATT_SVR_FIND_INFO       (1)
#define MYNEWT_VAL_BLE_ATT_SVR_FIND_TYPE       (1)
#define MYNEWT_VAL_BLE_ATT_SVR_READ_TYPE       (1)
#define MYNEWT_VAL_BLE_ATT_SVR_READ            (1)
#define MYNEWT_VAL_BLE_ATT_SVR_READ_BLOB       (1)
#define MYNEWT_VAL_BLE_ATT_SVR_READ_MULT       (1)
#define MYNEWT_VAL_BLE_ATT_SVR_READ_GROUP_TYPE (1)
#define MYNEWT_VAL_BLE_ATT_SVR_WRITE           (1)
#define MYNEWT_VAL_BLE_ATT_SVR_WRITE_NO_RSP    (1)
#define MYNEWT_VAL_BLE_ATT_SVR_SIGNED_WRITE    (0)
#define MYNEWT_VAL_BLE_ATT_SVR_QUEUED_WRITE    (1)
#define MYNEWT_VAL_BLE_ATT_SVR_QUEUED_WRITE_TMO (30000)
#define MYNEWT_VAL_BLE_ATT_SVR_INDICATE        (1)
#define MYNEWT_VAL_BLE_ATT_SVR_NOTIFY          (1)
#define MYNEWT_VAL_BLE_ATT_SVR_NOTIFY_MULTI    (0)
#define MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES (64)
#define MYNEWT_VAL_BLE_GATT_DISC_ALL_SVCS      (0)
#define MYNEWT_VAL_BLE_GATT_DISC_SVC_UUID      (0)
#define MYNEWT_VAL_BLE_GATT_FIND_INC_SVCS      (0)
#define MYNEWT_VAL_BLE_GATT_DISC_ALL_CHRS      (0)
#define MYNEWT_VAL_BLE_GATT_DISC_CHR_UUID      (0)
#define MYNEWT_VAL_BLE_GATT_DISC_ALL_DSCS      (0)
#define MYNEWT_VAL_BLE_GATT_READ               (0)
#define MYNEWT_VAL_BLE_GATT_READ_UUID          (0)
#define MYNEWT_VAL_BLE_GATT_READ_LONG          (0)
#define MYNEWT_VAL_BLE_GATT_READ_MULT          (0)
#define MYNEWT_VAL_BLE_GATT_READ_MULT_VAR      (0)
#define MYNEWT_VAL_BLE_GATT_WRITE_NO_RSP       (0)
#define MYNEWT_VAL_BLE_GATT_SIGNED_WRITE       (0)
#define MYNEWT_VAL_BLE_GATT_WRITE              (0)
#define MYNEWT_VAL_BLE_GATT_WRITE_LONG         (0)
#define MYNEWT_VAL_BLE_GATT_WRITE_RELIABLE     (0)
#define MYNEWT_VAL_BLE_GATT_NOTIFY             (1)
#define MYNEWT_VAL_BLE_GATT_INDICATE           (1)
#define MYNEWT_VAL_BLE_GATT_NOTIFY_MULTIPLE    (0)
#define MYNEWT_VAL_BLE_GATT_READ_MAX_ATTRS     (8)
#define MYNEWT_VAL_BLE_GATT_WRITE_MAX_ATTRS    (4)
#define MYNEWT_VAL_BLE_GATT_RESUME_RATE        (1000)

/* ===== GAP ===== */
#define MYNEWT_VAL_BLE_GAP_MAX_PENDING_CONN_PARAM_UPDATE (1)
#define MYNEWT_VAL_BLE_RPA_TIMEOUT             (300)
#define MYNEWT_VAL_BLE_WHITELIST               (1)

/* ===== L2CAP ===== */
#define MYNEWT_VAL_BLE_L2CAP_MAX_CHANS         (3*MYNEWT_VAL_BLE_MAX_CONNECTIONS)
#define MYNEWT_VAL_BLE_L2CAP_SIG_MAX_PROCS     (1)
#define MYNEWT_VAL_BLE_L2CAP_JOIN_RX_FRAGS     (1)
#define MYNEWT_VAL_BLE_L2CAP_RX_FRAG_TIMEOUT   (30000)
#define MYNEWT_VAL_BLE_L2CAP_COC_MAX_NUM       (0)
#define MYNEWT_VAL_BLE_L2CAP_COC_MPS           (MYNEWT_VAL_MSYS_1_BLOCK_SIZE-8)
#define MYNEWT_VAL_BLE_L2CAP_COC_SDU_BUFF_COUNT (1)
#define MYNEWT_VAL_BLE_L2CAP_ENHANCED_COC      (0)

/* ===== BLE version / disabled features ===== */
#define MYNEWT_VAL_BLE_VERSION                 (50)
#define MYNEWT_VAL_BLE_EXT_ADV                 (0)
#define MYNEWT_VAL_BLE_PERIODIC_ADV            (0)
#define MYNEWT_VAL_BLE_ISO                     (0)
#define MYNEWT_VAL_BLE_MESH                    (0)
#define MYNEWT_VAL_BLE_EATT_CHAN_NUM           (0)
#define MYNEWT_VAL_BLE_HCI_VS                  (0)
#define MYNEWT_VAL_BLE_PHY_2M                  (0)
#define MYNEWT_VAL_BLE_PHY_CODED               (0)
#define MYNEWT_VAL_BLE_POWER_CONTROL           (0)
#define MYNEWT_VAL_BLE_CONN_SUBRATING          (0)
#define MYNEWT_VAL_BLE_CHANNEL_SOUNDING        (0)
#define MYNEWT_VAL_BLE_PERIODIC_ADV_SYNC_TRANSFER (0)
#define MYNEWT_VAL_BLE_PERIODIC_ADV_SYNC_BIGINFO_REPORTS (0)
#define MYNEWT_VAL_BLE_MAX_PERIODIC_SYNCS     (0)
#define MYNEWT_VAL_BLE_EXT_ADV_MAX_SIZE        (31)
#define MYNEWT_VAL_BLE_MULTI_ADV_INSTANCES     (0)
#define MYNEWT_VAL_BLE_AUDIO                   (0)
#define MYNEWT_VAL_BLE_ISO_BROADCAST_SINK      (0)
#define MYNEWT_VAL_BLE_ISO_BROADCAST_SOURCE    (0)
#define MYNEWT_VAL_BLE_ISO_TEST                (0)
#define MYNEWT_VAL_BLE_ISO_MAX_BIGS            (0)
#define MYNEWT_VAL_BLE_ISO_MAX_BISES           (0)
#define MYNEWT_VAL_BLE_HCI_VS_OCF_OFFSET       (0)
#define MYNEWT_VAL_BLE_EATT_LOG_LVL            (1)
#define MYNEWT_VAL_BLE_EATT_LOG_MOD            (27)
#define MYNEWT_VAL_BLE_EATT_MTU                (128)

/* ===== Controller: external, not built-in ===== */
#ifndef NIMBLE_CFG_CONTROLLER
#define NIMBLE_CFG_CONTROLLER                  (0)
#endif

/* ===== Logging ===== */
#define MYNEWT_VAL_BLE_HS_LOG_LVL              (2)
#define MYNEWT_VAL_BLE_HS_LOG_MOD              (4)
#define MYNEWT_VAL_BLE_HS_DEBUG                (0)
#define MYNEWT_VAL_BLE_HS_PHONY_HCI_ACKS       (0)
#define MYNEWT_VAL_BLE_HS_EXT_ADV_LEGACY_INSTANCE (0)
#define MYNEWT_VAL_BLE_HS_SYSINIT_STAGE        (200)
#define MYNEWT_VAL_BLE_HS_STOP_ON_SHUTDOWN_TIMEOUT (2000)
#define MYNEWT_VAL_BLE_HS_GAP_UNHANDLED_HCI_EVENT (0)
#define MYNEWT_VAL_BLE_HS_FLOW_CTRL_ITVL       (1000)
#define MYNEWT_VAL_BLE_HS_FLOW_CTRL_THRESH     (2)
#define MYNEWT_VAL_BLE_HS_FLOW_CTRL_TX_ON_DISCONNECT (0)
#define MYNEWT_VAL_LOG_LEVEL                   (1)
#define MYNEWT_VAL_LOG_CONSOLE                 (1)
#define MYNEWT_VAL_DFLT_LOG_LVL                (1)
#define MYNEWT_VAL_DFLT_LOG_MOD                (0)
#define MYNEWT_VAL_LOG_GLOBAL_IDX              (1)
#define MYNEWT_VAL_MODLOG_CONSOLE_DFLT         (1)
#define MYNEWT_VAL_MODLOG_LOG_MACROS           (0)
#define MYNEWT_VAL_MODLOG_MAX_MAPPINGS         (16)
#define MYNEWT_VAL_MODLOG_MAX_PRINTF_LEN       (128)

/* ===== Monitor: off ===== */
#define MYNEWT_VAL_BLE_MONITOR_RTT             (0)
#define MYNEWT_VAL_BLE_MONITOR_UART            (0)
#define MYNEWT_VAL_BLE_MONITOR_CONSOLE_BUFFER_SIZE (128)
#define MYNEWT_VAL_BLE_MONITOR_RTT_BUFFERED    (1)
#define MYNEWT_VAL_BLE_MONITOR_RTT_BUFFER_NAME "btmonitor"
#define MYNEWT_VAL_BLE_MONITOR_RTT_BUFFER_SIZE (256)
#define MYNEWT_VAL_BLE_MONITOR_UART_BAUDRATE   (1000000)
#define MYNEWT_VAL_BLE_MONITOR_UART_BUFFER_SIZE (64)
#define MYNEWT_VAL_BLE_MONITOR_UART_DEV        "uart0"

/* ===== Services ===== */
#define MYNEWT_VAL_BLE_SVC_GAP_DEVICE_NAME     "N32-NimBLE"
#define MYNEWT_VAL_BLE_SVC_GAP_APPEARANCE      (0)
#define MYNEWT_VAL_BLE_SVC_GAP_APPEARANCE_WRITE_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_GAP_CENTRAL_ADDRESS_RESOLUTION (-1)
#define MYNEWT_VAL_BLE_SVC_GAP_DEVICE_NAME_MAX_LENGTH (31)
#define MYNEWT_VAL_BLE_SVC_GAP_DEVICE_NAME_WRITE_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_GAP_PPCP_MAX_CONN_INTERVAL (0)
#define MYNEWT_VAL_BLE_SVC_GAP_PPCP_MIN_CONN_INTERVAL (0)
#define MYNEWT_VAL_BLE_SVC_GAP_PPCP_SLAVE_LATENCY (0)
#define MYNEWT_VAL_BLE_SVC_GAP_PPCP_SUPERVISION_TMO (0)
#define MYNEWT_VAL_BLE_SVC_GAP_SYSINIT_STAGE   (301)
#define MYNEWT_VAL_BLE_SVC_GATT_SYSINIT_STAGE  (302)
#define MYNEWT_VAL_BLE_SVC_DIS_DEFAULT_READ_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_DIS_MODEL_NUMBER_DEFAULT "N32WB452"
#define MYNEWT_VAL_BLE_SVC_DIS_MODEL_NUMBER_READ_PERM (0)
#define MYNEWT_VAL_BLE_SVC_DIS_FIRMWARE_REVISION_DEFAULT NULL
#define MYNEWT_VAL_BLE_SVC_DIS_FIRMWARE_REVISION_READ_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_DIS_HARDWARE_REVISION_DEFAULT NULL
#define MYNEWT_VAL_BLE_SVC_DIS_HARDWARE_REVISION_READ_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_DIS_MANUFACTURER_NAME_DEFAULT NULL
#define MYNEWT_VAL_BLE_SVC_DIS_MANUFACTURER_NAME_READ_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_DIS_SERIAL_NUMBER_DEFAULT NULL
#define MYNEWT_VAL_BLE_SVC_DIS_SERIAL_NUMBER_READ_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_DIS_SOFTWARE_REVISION_DEFAULT NULL
#define MYNEWT_VAL_BLE_SVC_DIS_SOFTWARE_REVISION_READ_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_DIS_SYSTEM_ID_DEFAULT NULL
#define MYNEWT_VAL_BLE_SVC_DIS_SYSTEM_ID_READ_PERM (-1)
#define MYNEWT_VAL_BLE_SVC_DIS_SYSINIT_STAGE    (303)
#define MYNEWT_VAL_BLE_SVC_ANS_NEW_ALERT_CAT    (0)
#define MYNEWT_VAL_BLE_SVC_ANS_SYSINIT_STAGE    (303)
#define MYNEWT_VAL_BLE_SVC_ANS_UNR_ALERT_CAT    (0)
#define MYNEWT_VAL_BLE_SVC_BAS_BATTERY_LEVEL_NOTIFY_ENABLE (1)
#define MYNEWT_VAL_BLE_SVC_BAS_BATTERY_LEVEL_READ_PERM (0)
#define MYNEWT_VAL_BLE_SVC_BAS_SYSINIT_STAGE    (303)
#define MYNEWT_VAL_BLE_SVC_IAS_SYSINIT_STAGE    (303)
#define MYNEWT_VAL_BLE_SVC_IPSS_SYSINIT_STAGE   (303)
#define MYNEWT_VAL_BLE_SVC_LLS_SYSINIT_STAGE    (303)
#define MYNEWT_VAL_BLE_SVC_TPS_SYSINIT_STAGE    (303)

/* ===== Store ===== */
#define MYNEWT_VAL_BLE_STORE_MAX_BONDS         (3)
#define MYNEWT_VAL_BLE_STORE_MAX_CCCDS         (8)

/* ===== TinyCrypt ===== */
#define MYNEWT_VAL_TINYCRYPT_SYSINIT_STAGE     (200)
#define MYNEWT_VAL_TINYCRYPT_UECC_RNG_USE_TRNG (0)
#define MYNEWT_VAL_TINYCRYPT_UECC_RNG_TRNG_DEV_NAME "trng"

/* ===== OS ===== */
#define MYNEWT_VAL_OS_TICKS_PER_SEC            (1000)
#define MYNEWT_VAL_OS_SCHEDULING               (1)
#define MYNEWT_VAL_OS_ASSERT_CB                (0)
#define MYNEWT_VAL_OS_MEMPOOL_CHECK            (0)
#define MYNEWT_VAL_OS_MEMPOOL_GUARD            (0)
#define MYNEWT_VAL_OS_MEMPOOL_POISON           (0)
#define MYNEWT_VAL_OS_SYSVIEW                  (0)
#define MYNEWT_VAL_OS_CRASH_FILE_LINE          (1)
#define MYNEWT_VAL_OS_CTX_SW_STACK_CHECK       (0)
#define MYNEWT_VAL_OS_CTX_SW_STACK_GUARD       (4)
#define MYNEWT_VAL_OS_DEBUG_MODE               (0)
#define MYNEWT_VAL_OS_EVENTQ_DEBUG             (0)
#define MYNEWT_VAL_OS_EVENTQ_MONITOR           (0)
#define MYNEWT_VAL_OS_CRASH_LOG                (0)
#define MYNEWT_VAL_OS_CRASH_RESTORE_REGS       (0)
#define MYNEWT_VAL_OS_CRASH_STACKTRACE         (0)
#define MYNEWT_VAL_OS_COREDUMP                 (0)
#define MYNEWT_VAL_OS_COREDUMP_CB              (0)
#define MYNEWT_VAL_OS_CLI                      (0)
#define MYNEWT_VAL_OS_DEFAULT_IRQ_CB           (0)
#define MYNEWT_VAL_OS_TIME_DEBUG               (0)
#define MYNEWT_VAL_OS_WATCHDOG_MONITOR         (0)
#define MYNEWT_VAL_OS_CPUTIME_FREQ             (1000000)
#define MYNEWT_VAL_OS_CPUTIME_TIMER_NUM        (0)
#define MYNEWT_VAL_OS_IDLE_TICKLESS_MS_MAX     (600000)
#define MYNEWT_VAL_OS_IDLE_TICKLESS_MS_MIN     (1)
#define MYNEWT_VAL_OS_MAIN_STACK_SIZE          (1024)
#define MYNEWT_VAL_OS_MAIN_TASK_PRIO           (127)
#define MYNEWT_VAL_OS_MAIN_TASK_SANITY_ITVL_MS (0)
#define MYNEWT_VAL_OS_SYSINIT_STAGE            (0)
#define MYNEWT_VAL_OS_TASK_RUN_TIME_CPUTIME    (0)
#define MYNEWT_VAL_OS_SYSVIEW_TRACE_CALLOUT    (0)
#define MYNEWT_VAL_OS_SYSVIEW_TRACE_EVENTQ     (0)
#define MYNEWT_VAL_OS_SYSVIEW_TRACE_MBUF       (0)
#define MYNEWT_VAL_OS_SYSVIEW_TRACE_MEMPOOL    (0)
#define MYNEWT_VAL_OS_SYSVIEW_TRACE_MUTEX      (0)
#define MYNEWT_VAL_OS_SYSVIEW_TRACE_SEM        (0)
#define MYNEWT_VAL_MSYS_SANITY_TIMEOUT         (60000)
#define MYNEWT_VAL_MSYS_1_SANITY_MIN_COUNT     (0)
#define MYNEWT_VAL_MSYS_2_SANITY_MIN_COUNT     (0)

/* ===== Debug / sysinit ===== */
#define MYNEWT_VAL_DEBUG_PANIC_ENABLED         (1)
#define MYNEWT_VAL_SYSINIT_CONSTRAIN_INIT      (1)
#define MYNEWT_VAL_SYSINIT_PANIC_FILE_LINE     (1)
#define MYNEWT_VAL_SYSINIT_PANIC_MESSAGE       (1)
#define MYNEWT_VAL_SYSDOWN_CONSTRAIN_DOWN      (1)
#define MYNEWT_VAL_SANITY_INTERVAL             (15000)
#define MYNEWT_VAL_WATCHDOG_INTERVAL           (30000)

/* ===== Flash / newt / build ===== */
#define MYNEWT_VAL_FLASH_MAP_MAX_AREAS         (10)
#define MYNEWT_VAL_FLASH_MAP_SUPPORT_MFG       (0)
#define MYNEWT_VAL_NEWT_FEATURE_LOGCFG         (0)
#define MYNEWT_VAL_NEWT_FEATURE_SYSDOWN        (0)
#define MYNEWT_VAL_RWLOCK_DEBUG                (0)
#define MYNEWT_VAL_MCU_FLASH_MIN_WRITE_SIZE    (1)
#define MYNEWT_VAL_MCU_FLASH_STYLE_ST          (0)
#define MYNEWT_VAL_MCU_FLASH_STYLE_NORDIC      (0)
#define MYNEWT_VAL_HAL_FLASH_VERIFY_BUF_SZ     (16)
#define MYNEWT_VAL_HAL_FLASH_VERIFY_ERASES     (0)
#define MYNEWT_VAL_HAL_FLASH_VERIFY_WRITES     (0)
#define MYNEWT_VAL_HAL_ENABLE_SOFTWARE_BREAKPOINTS (0)
#define MYNEWT_VAL_HAL_FLASH_MAX_DEVICE_COUNT  (0)
#define MYNEWT_VAL_HAL_SBRK                    (0)
#define MYNEWT_VAL_HAL_SYSTEM_RESET_CB         (0)
#define MYNEWT_VAL_FLOAT_USER                  (0)

/* ===== Package markers ===== */
#define MYNEWT_PKG_apache_mynewt_nimble__nimble                    1
#define MYNEWT_PKG_apache_mynewt_nimble__nimble_host               1
#define MYNEWT_PKG_apache_mynewt_nimble__nimble_host_services_gap  1
#define MYNEWT_PKG_apache_mynewt_nimble__nimble_host_services_gatt 1
#define MYNEWT_PKG_apache_mynewt_nimble__nimble_transport          1
#define MYNEWT_PKG_apache_mynewt_core__crypto_tinycrypt            1

#define MYNEWT_VAL_APP_NAME   "n32wb452_ble"
#define MYNEWT_VAL_ARCH_NAME  "cortex_m4"
#define MYNEWT_VAL_BSP_NAME   "n32wb452"

#endif /* H_MYNEWT_SYSCFG_ */
