/*
 * Hardware.h
 *
 *  Created on: 6 nov. 2021
 *      Author: Daniel Mårtensson
 */

#ifndef HARDWARE_HARDWARE_H_
#define HARDWARE_HARDWARE_H_

/* Select your target platform here */
#define NO_PLATFORM 0
#define STM32 1
#define ARDUINO 2
#define PIC 3
#define AVR 4
#define QT_USB 5
#define INTERNAL_CALLBACK 6
#define SOCKETCAN 7
#ifndef OPENSAE_J1939_TARGET_PLATFORM
#define OPENSAE_J1939_TARGET_PLATFORM NO_PLATFORM
#endif

/* C Standard library */
#include "../Open_SAE_J1939/Structs.h"

/* Enums */
#include "../SAE_J1939/SAE_J1939_Enums/Enum_DM14_DM15.h"
#include "../SAE_J1939/SAE_J1939_Enums/Enum_Send_Status.h"

#ifdef __cplusplus
extern "C" {
#endif

void CAN_Context_Init(CAN_Context *context);
ENUM_J1939_STATUS_CODES CAN_Send_Message(J1939 *j1939, uint32_t ID, const uint8_t data[]);
ENUM_J1939_STATUS_CODES CAN_Send_Request(J1939 *j1939, uint32_t ID, const uint8_t PGN[]);
bool CAN_Read_Message(J1939 *j1939, uint32_t *ID, uint8_t data[]);
void CAN_Delay(J1939 *j1939, uint8_t milliseconds);
void CAN_Set_Callback_Functions(CAN_Context *context, void (*Callback_Function_Send_)(J1939*, uint32_t, uint8_t, const uint8_t[]), void (*Callback_Function_Read_)(J1939*, uint32_t*, uint8_t[], bool*), void (*Callback_Function_Traffic_)(J1939*, uint32_t, uint8_t, const uint8_t[], bool), void (*Callback_Function_Delay_ms_)(J1939*, uint8_t));
void FLASH_EEPROM_RAM_Memory(uint16_t *number_of_requested_bytes, uint8_t pointer_type, uint8_t *command, uint32_t *pointer, uint8_t *pointer_extension, uint16_t *key, uint8_t raw_binary_data[]);
bool Save_Struct(const uint8_t data[], uint32_t data_length, const char file_name[]);
bool Load_Struct(uint8_t data[], uint32_t data_length, const char file_name[]);

#ifdef __cplusplus
}
#endif

#endif /* HARDWARE_HARDWARE_H_ */
