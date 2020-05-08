#include "board_init.h"

#include <stdlib.h>

#include "tx_api.h"

#include "stm32l475e_iot01.h"
#include "stm32l475e_iot01_accelero.h"
#include "stm32l475e_iot01_psensor.h"
#include "stm32l475e_iot01_gyro.h"
#include "stm32l475e_iot01_hsensor.h"
#include "stm32l475e_iot01_tsensor.h"
#include "stm32l475e_iot01_magneto.h"

#include "azure_config.h"
#include "wifi.h"

/* Exported variables ---------------------------------------------------------*/
UART_HandleTypeDef UartHandle;
RTC_HandleTypeDef RtcHandle;

TIM_HandleTypeDef TimCCHandle;
extern SPI_HandleTypeDef hspi;

volatile uint32_t ButtonPressed = 0;
volatile uint32_t SendData = 0;

static uint32_t t_TIM_CC1_Pulse;

/* Local defines -------------------------------------------------------------*/

//2kHz/0.5 For Sensors Data data@0.5Hz
#define DEFAULT_TIM_CC1_PULSE 4000

/* Defines related to Clock configuration */
#define RTC_ASYNCH_PREDIV 0x7F  /* LSE as RTC clock */
#define RTC_SYNCH_PREDIV 0x00FF /* LSE as RTC clock */

#define AZURE_PRINTF(...) printf(__VA_ARGS__)

#define WIFI_CONNECT_MAX_ATTEMPT_COUNT 3

#define CFG_HW_UART1_BAUDRATE 115200
#define CFG_HW_UART1_WORDLENGTH UART_WORDLENGTH_8B
#define CFG_HW_UART1_STOPBITS UART_STOPBITS_1
#define CFG_HW_UART1_PARITY UART_PARITY_NONE
#define CFG_HW_UART1_HWFLOWCTL UART_HWCONTROL_NONE
#define CFG_HW_UART1_MODE UART_MODE_TX_RX
#define CFG_HW_UART1_ADVFEATUREINIT UART_ADVFEATURE_NO_INIT

/* Local function prototypes --------------------------------------------------*/
static void STM32_Error_Handler(void);
static void Init_MEM1_Sensors(void);
static void SystemClock_Config(void);
static void InitTimers(void);
static void InitRTC(void);
void SPI3_IRQHandler(void);
static void UART_Console_Init(void);
static bool net_if_init();

/**
  * @brief  Function for Initializing the platform
  * @retval int Ok/Error (0/1)
  */
bool board_init(void)
{
    /* Init Platform */
    HAL_Init();

    /* Configure the System clock */
    SystemClock_Config();

    /* initialize Real Time Clock */
    InitRTC();

    UART_Console_Init();
    AZURE_PRINTF("UART Initialized\r\n");

    AZURE_PRINTF("\r\nSTMicroelectronics\r\n"
                 "\tB-L475E-IOT01 IoT node Discovery board"
                 "\r\n");

    //AZURE_PRINTF("\tAzure SDK Version %s\r\n", IOTHUB_SDK_VERSION);

    AZURE_PRINTF("\t(HAL %ld.%ld.%ld_%ld)\r\n"
                 "\tCompiled %s %s\r\n",
                 HAL_GetHalVersion() >> 24,
                 (HAL_GetHalVersion() >> 16) & 0xFF,
                 (HAL_GetHalVersion() >> 8) & 0xFF,
                 HAL_GetHalVersion() & 0xFF,
                 __DATE__, __TIME__);

    /* Initialize button */
    BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

    /* Initialize LED */
    BSP_LED_Init(LED2);

    /* Discovery and Intialize all the Target's Features */
    Init_MEM1_Sensors();

    /* initialize timers */
    InitTimers();
    AZURE_PRINTF("Init Application's Timers\r\n");

    /* Initialize the WIFI module */
    if (!net_if_init(NULL))
    {
        return false;
    }

    return 0;
}

/**
  * @brief  Function for initializing the WIFI module
  * @retval None
  */
static bool net_if_init()
{
    uint8_t macAddress[6];
    char moduleinfo[ES_WIFI_MAX_SSID_NAME_SIZE];
    WIFI_Ecn_t security_mode;

    switch (wifi_mode)
    {
    case None:
        security_mode = WIFI_ECN_OPEN;
        break;
    case WEP:
        security_mode = WIFI_ECN_WEP;
        break;
    case WPA_Personal:
    default:
        security_mode = WIFI_ECN_WPA2_PSK;
        break;
    };

    int32_t wifiConnectCounter = 1;
    WIFI_Status_t result;

    if (WIFI_Init() != WIFI_STATUS_OK)
    {
        AZURE_PRINTF("Failed to initialize WIFI module\r\n");
        return false;
    }
    else
    {
        AZURE_PRINTF("WIFI module initialized\r\n");
    }

    /* Retrieve the WiFi module mac address to confirm that it is detected and communicating. */
    WIFI_GetModuleName(moduleinfo);
    AZURE_PRINTF("Module initialized successfully: %s\r\n", moduleinfo);

    WIFI_GetModuleID(moduleinfo);
    AZURE_PRINTF("\t%s\r\n", moduleinfo);

    WIFI_GetModuleFwRevision(moduleinfo);
    AZURE_PRINTF("\t%s\r\n", moduleinfo);

    if (WIFI_GetMAC_Address((uint8_t *)macAddress) != WIFI_STATUS_OK)
    {
        AZURE_PRINTF("Failed to get MAC address\r\n");
        return false;
    }
    else
    {
        AZURE_PRINTF("WiFi MAC Address is: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
                     macAddress[0], macAddress[1], macAddress[2],
                     macAddress[3], macAddress[4], macAddress[5]);
    }

    /* Connect to the specified SSID. */
    AZURE_PRINTF("Connecting WIFI; SSID=%s\r\n", wifi_ssid);
    while ((result = WIFI_Connect(wifi_ssid, wifi_password, security_mode)) != WIFI_STATUS_OK)
    {
        AZURE_PRINTF("WiFi us not able connect to AP, attempt = %ld\r\n", wifiConnectCounter++);
        HAL_Delay(1000);
        /* Max number of attempts */
        if (wifiConnectCounter == WIFI_CONNECT_MAX_ATTEMPT_COUNT)
        {
            AZURE_PRINTF("ERROR: WiFi is not able to connected to the AP %s, with error %d\r\n", wifi_ssid, result);
            return false;
        }
    }
    AZURE_PRINTF("WiFi connected to the AP %s\r\n", wifi_ssid);

    uint8_t IP_Addr[4];
    if (WIFI_GetIP_Address(IP_Addr) == WIFI_STATUS_OK)
    {
        AZURE_PRINTF("\tES-WIFI IP Address: %d.%d.%d.%d\r\n",
                     IP_Addr[0], IP_Addr[1], IP_Addr[2], IP_Addr[3]);
    }

    uint8_t Gateway_Addr[4];
    if (WIFI_GetGateway_Address(Gateway_Addr) == WIFI_STATUS_OK)
    {
        AZURE_PRINTF("\tES-WIFI Gateway Address: %d.%d.%d.%d\r\n",
                     Gateway_Addr[0], Gateway_Addr[1], Gateway_Addr[2], Gateway_Addr[3]);
    }

    uint8_t DNS1_Addr[4];
    uint8_t DNS2_Addr[4];
    if (WIFI_GetDNS_Address(DNS1_Addr, DNS2_Addr) == WIFI_STATUS_OK)
    {
        AZURE_PRINTF("\tES-WIFI DNS1 Address: %d.%d.%d.%d\r\n",
                     DNS1_Addr[0], DNS1_Addr[1], DNS1_Addr[2], DNS1_Addr[3]);

        AZURE_PRINTF("\tES-WIFI DNS2 Address: %d.%d.%d.%d\r\n",
                     DNS2_Addr[0], DNS2_Addr[1], DNS2_Addr[2], DNS2_Addr[3]);
    }

    /* Until is ok */
    return true;
}

/** @brief Initialize all the MEMS1 sensors
 * @param None
 * @retval None
 */
static void Init_MEM1_Sensors(void)
{
    /* Accelero */
    if (ACCELERO_OK != BSP_ACCELERO_Init())
    {
        AZURE_PRINTF("Err Accelero Sensor\r\n");
    }
    else
    {
        AZURE_PRINTF("Ok Accelero Sensor\r\n");
    }

    /* Gyro */
    if (GYRO_OK != BSP_GYRO_Init())
    {
        AZURE_PRINTF("Err Gyroscope Sensor\r\n");
    }
    else
    {
        AZURE_PRINTF("Ok Gyroscope Sensor\r\n");
    }

    /* Mag */
    if (MAGNETO_OK != BSP_MAGNETO_Init())
    {
        AZURE_PRINTF("Err Magneto Sensor\r\n");
    }
    else
    {
        AZURE_PRINTF("Ok Magneto Sensor\r\n");
    }

    /* Humidity */
    if (HSENSOR_OK != BSP_HSENSOR_Init())
    {
        AZURE_PRINTF("Err Humidity Sensor\r\n");
    }
    else
    {
        AZURE_PRINTF("Ok Humidity Sensor\r\n");
    }

    /* Temperature */
    if (TSENSOR_OK != BSP_TSENSOR_Init())
    {
        AZURE_PRINTF("Err Temperature Sensor\r\n");
    }
    else
    {
        AZURE_PRINTF("Ok Temperature Sensor\r\n");
    }

    /* Pressure */
    if (PSENSOR_OK != BSP_PSENSOR_Init())
    {
        AZURE_PRINTF("Err Pressure Sensor\r\n");
    }
    else
    {
        AZURE_PRINTF("Ok Pressure Sensor\r\n");
    }
}

/**
  * @brief  System Clock Configuration
  *         The system Clock is configured as follow :
  *            System Clock source            = PLL (MSI)
  *            SYSCLK(Hz)                     = 80000000
  *            HCLK(Hz)                       = 80000000
  *            AHB Prescaler                  = 1
  *            APB1 Prescaler                 = 1
  *            APB2 Prescaler                 = 1
  *            MSI Frequency(Hz)              = 48000000
  *            PLL_M                          = 6
  *            PLL_N                          = 20
  *            PLL_R                          = 2
  *            PLL_P                          = 7
  *            PLL_Q                          = 2
  *            Flash Latency(WS)              = 4
  * @param  None
  * @retval None
  */
static void SystemClock_Config(void)
{

    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_PeriphCLKInitTypeDef PeriphClkInit;

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_11;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM = 6;
    RCC_OscInitStruct.PLL.PLLN = 20;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        /* Initialization Error */
        STM32_Error_Handler();
    }

    /* Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2
  clocks dividers */
    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        /* Initialization Error */
        STM32_Error_Handler();
    }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC | RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_USART3 | RCC_PERIPHCLK_I2C2 | RCC_PERIPHCLK_RNG;
    PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
    PeriphClkInit.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
    PeriphClkInit.I2c2ClockSelection = RCC_I2C2CLKSOURCE_PCLK1;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    PeriphClkInit.RngClockSelection = RCC_RNGCLKSOURCE_MSI;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        /* Initialization Error */
        STM32_Error_Handler();
    }

    __HAL_RCC_PWR_CLK_ENABLE();

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
    {
        /* Initialization Error */
        STM32_Error_Handler();
    }

    /* Enable MSI PLL mode */
    HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief  Output Compare callback in non blocking mode
  * @param  htim : TIM OC handle
  * @retval None
  */
void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    uint32_t uhCapture = 0;

    /* TIM1_CH1 toggling with frequency = 2Hz */
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
        uhCapture = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        /* Set the Capture Compare Register value */
        __HAL_TIM_SET_COMPARE(&TimCCHandle, TIM_CHANNEL_1, (uhCapture + t_TIM_CC1_Pulse));
        SendData = 1;
        BSP_LED_Toggle(LED2);
    }
}

/**
* @brief  Function for initializing timers for sending the Telemetry data to IoT hub
 * @param  None
 * @retval None
 */
static void InitTimers(void)
{
    uint32_t uwPrescalerValue;

    /* Timer Output Compare Configuration Structure declaration */
    TIM_OC_InitTypeDef sConfig;

    /* Compute the prescaler value to have TIM3 counter clock equal to 2 KHz */
    uwPrescalerValue = (uint32_t)((SystemCoreClock / 2000) - 1);

    /* Set TIM1 instance (Motion)*/
    TimCCHandle.Instance = TIM1;
    TimCCHandle.Init.Period = 65535;
    TimCCHandle.Init.Prescaler = uwPrescalerValue;
    TimCCHandle.Init.ClockDivision = 0;
    TimCCHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    if (HAL_TIM_OC_Init(&TimCCHandle) != HAL_OK)
    {
        /* Initialization Error */
        STM32_Error_Handler();
    }

    /* Configure the Output Compare channels */
    /* Common configuration for all channels */
    sConfig.OCMode = TIM_OCMODE_TOGGLE;
    sConfig.OCPolarity = TIM_OCPOLARITY_LOW;

    /* Output Compare Toggle Mode configuration: Channel1 */
    sConfig.Pulse = DEFAULT_TIM_CC1_PULSE;
    if (HAL_TIM_OC_ConfigChannel(&TimCCHandle, &sConfig, TIM_CHANNEL_1) != HAL_OK)
    {
        /* Configuration Error */
        STM32_Error_Handler();
    }
}

/**
* @brief  Function for initializing the Real Time Clock
 * @param  None
 * @retval None
 */
static void InitRTC(void)
{
    /* Configure RTC prescaler and RTC data registers */
    /* RTC configured as follow:
      - Hour Format    = Format 24
      - Asynch Prediv  = Value according to source clock
      - Synch Prediv   = Value according to source clock
      - OutPut         = Output Disable
      - OutPutPolarity = High Polarity
      - OutPutType     = Open Drain
  */

    RtcHandle.Instance = RTC;
    RtcHandle.Init.HourFormat = RTC_HOURFORMAT_24;
    RtcHandle.Init.AsynchPrediv = RTC_ASYNCH_PREDIV;
    RtcHandle.Init.SynchPrediv = RTC_SYNCH_PREDIV;
    RtcHandle.Init.OutPut = RTC_OUTPUT_DISABLE;
    RtcHandle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    RtcHandle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&RtcHandle) != HAL_OK)
    {
        STM32_Error_Handler();
    }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  None
  * @retval None
  */
void STM32_Error_Handler(void)
{
    AZURE_PRINTF("FATAL: STM32 Error Handler\r\n");

    /* User may add here some code to deal with this error */
    while (1)
    {
    }
}

/**
 * @brief  EXTI line detection callback.
 * @param  uint16_t GPIO_Pin Specifies the pins connected EXTI line
 * @retval None
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    switch (GPIO_Pin)
    {
    case USER_BUTTON_PIN:
        ButtonPressed = 1;
        break;
    case GPIO_PIN_1:
        SPI_WIFI_ISR();
        break;
    }
}

// WiFi interrupt handle
void SPI3_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&hspi);
}

/**
 * @brief  Configures UART interface
 * @param  None
 * @retval None
 */
static void UART_Console_Init(void)
{
    UartHandle.Instance = USART1;
    UartHandle.Init.BaudRate = CFG_HW_UART1_BAUDRATE;
    UartHandle.Init.WordLength = CFG_HW_UART1_WORDLENGTH;
    UartHandle.Init.StopBits = CFG_HW_UART1_STOPBITS;
    UartHandle.Init.Parity = CFG_HW_UART1_PARITY;
    UartHandle.Init.Mode = CFG_HW_UART1_MODE;
    UartHandle.Init.HwFlowCtl = CFG_HW_UART1_HWFLOWCTL;
    UartHandle.AdvancedInit.AdvFeatureInit = CFG_HW_UART1_ADVFEATUREINIT;
    BSP_COM_Init(COM1, &UartHandle);
}

/**
  * @brief This function provides accurate delay (in milliseconds) based
  *        on variable incremented.
  * @note This is a user implementation using WFI state
  * @param Delay specifies the delay time length, in milliseconds.
  * @retval None
  */
void HAL_Delay(__IO uint32_t Delay)
{
    uint32_t tickstart = 0;
    tickstart = HAL_GetTick();
    while ((HAL_GetTick() - tickstart) < Delay)
    {
        __WFI();
    }
}

uint32_t HAL_GetTick(void)
{
    return tx_time_get() * (100 / TX_TIMER_TICKS_PER_SECOND);
}