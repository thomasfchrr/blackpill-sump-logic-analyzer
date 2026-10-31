/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt and exception handlers.
  ******************************************************************************
  */
#include "main.h"
#include "stm32f4xx_it.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

/**
 * Unrecoverable fault. With a debugger attached, stop on the faulting context;
 * otherwise reset so that an unattended analyzer comes back instead of hanging.
 */
static void fault(void)
{
  if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
  {
    __BKPT(0);
  }
  NVIC_SystemReset();
}

/** Clock Security System: HSE died. Acknowledge and keep running on HSI (USB will drop). */
void NMI_Handler(void)
{
  HAL_RCC_NMI_IRQHandler();
}

void HardFault_Handler(void)     { fault(); }
void MemManage_Handler(void)     { fault(); }
void BusFault_Handler(void)      { fault(); }
void UsageFault_Handler(void)    { fault(); }
void SVC_Handler(void)           { }
void DebugMon_Handler(void)      { }
void PendSV_Handler(void)        { }

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void OTG_FS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}
