#ifndef _APP_STATE_H_
#define _APP_STATE_H_
#include "common.h"

#if HARDWARE_VERSION == HARDWARE_VERSION_1
#define POWER_ON()            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET)
#define POWER_OFF()           HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET)

#elif HARDWARE_VERSION == HARDWARE_VERSION_2
#define POWER_ON()            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET)
#define POWER_OFF()           HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET)

#endif

typedef enum 
{
    FLAG_NFC_NONE,
    FLAG_NFC_OFFLINE,    
    FLAG_NFC_ONLINE
}flag_nfc_en;


extern flag_nfc_en flag_nfc;

extern void app_state_timer(void);
extern void app_state_process(void);
extern void app_state_init(void);

#endif
