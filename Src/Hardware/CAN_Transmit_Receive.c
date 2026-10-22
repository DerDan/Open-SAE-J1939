/*
 * CAN_Transmit_Receive.c
 *
 *  Created on: 11 juli 2021
 *      Author: Daniel Mårtensson
 */

 /* Layer */
#include "Hardware.h"

/* Platform independent library headers for CAN */
#if OPENSAE_J1939_TARGET_PLATFORM == STM32
#include "main.h"
#elif OPENSAE_J1939_TARGET_PLATFORM == ARDUINO
#elif OPENSAE_J1939_TARGET_PLATFORM == PIC
#elif OPENSAE_J1939_TARGET_PLATFORM == AVR
#elif OPENSAE_J1939_TARGET_PLATFORM == QT_USB
#include "CAN_to_USB/can_to_usb.h"
#elif OPENSAE_J1939_TARGET_PLATFORM == INTERNAL_CALLBACK
/* Nothing here because else statement should not be running */
#elif OPENSAE_J1939_TARGET_PLATFORM == SOCKETCAN
#include <unistd.h>
#include "SocketCAN.h"
#else
/* Internal functions */
static ENUM_J1939_STATUS_CODES Internal_Transmit(CAN_Context *context, uint32_t ID, uint8_t data[], uint8_t DLC) {
	context->internal_ID[context->buffer_index_transmit] = ID;
	context->internal_DLC[context->buffer_index_transmit] = DLC;
	uint8_t i;
	for (i = 0; i < 8; i++) {
		if (i < DLC) {
			context->internal_data[context->buffer_index_transmit * 8 + i] = data[i];
		}
		else {
			context->internal_data[context->buffer_index_transmit * 8 + i] = 0x0;
		}
	}
	context->internal_new_message[context->buffer_index_transmit] = true;
	context->buffer_index_transmit++;									/* When this is 256, then it will be come 0 again */
	return STATUS_SEND_OK;
}

static void Internal_Receive(CAN_Context *context, uint32_t* ID, uint8_t data[], bool* is_new_message) {
	/* Do a quick check if we are going to read message that have no data */
	if (context->internal_new_message[context->buffer_index_receive] == false) {
		*is_new_message = false;
		return;
	}

	*ID = context->internal_ID[context->buffer_index_receive];
	uint8_t i;
	for (i = 0; i < 8; i++) {
		if (i < context->internal_DLC[context->buffer_index_receive]) {
			data[i] = context->internal_data[context->buffer_index_receive * 8 + i];
		}
	}
	*is_new_message = context->internal_new_message[context->buffer_index_receive];
	/* Reset */
	context->internal_new_message[context->buffer_index_receive] = false;
	context->internal_DLC[context->buffer_index_receive] = 0;
	context->buffer_index_receive++;										/* When this is 256, then it will be come 0 again */
}
#endif

ENUM_J1939_STATUS_CODES CAN_Send_Message(J1939 *j1939, uint32_t ID, uint8_t data[]) {
	ENUM_J1939_STATUS_CODES status = STATUS_SEND_BUSY;
	CAN_Context *context;
	if (j1939 == NULL || j1939->can_context == NULL || data == NULL) {
		return STATUS_SEND_ERROR;
	}
	context = j1939->can_context;
	if (context->Callback_Function_Send != NULL) {
		context->Callback_Function_Send(j1939, ID, 8, data);
		status = STATUS_SEND_OK;
	} else {
#if OPENSAE_J1939_TARGET_PLATFORM == STM32
		CAN_TxHeaderTypeDef TxHeader;
		TxHeader.DLC = 8;											/* Here we are sending 8 bytes */
		TxHeader.RTR = CAN_RTR_DATA;								/* Data frame */
		TxHeader.IDE = CAN_ID_EXT;									/* We want to send an extended ID */
		TxHeader.TransmitGlobalTime = DISABLE;
		TxHeader.ExtId = ID;
		TxHeader.StdId = 0x00; 										/* Not used */
		status = STM32_PLC_CAN_Transmit(j1939, data, &TxHeader);
#elif OPENSAE_J1939_TARGET_PLATFORM == ARDUINO
		/* Implement your CAN send 8 bytes message function for the Arduino platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == PIC
		/* Implement your CAN send 8 bytes message function for the PIC platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == AVR
		/* Implement your CAN send 8 bytes message function for the AVR platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == QT_USB
		status = QT_USB_Transmit(j1939, ID, data, 8);
#elif OPENSAE_J1939_TARGET_PLATFORM == SOCKETCAN
		status = (socketcan_transmit(context, ID, data, 8) == 0 ? STATUS_SEND_OK : STATUS_SEND_ERROR);
#elif OPENSAE_J1939_TARGET_PLATFORM == INTERNAL_CALLBACK
		status = STATUS_SEND_ERROR;
#else
		/* If no processor are used, use internal feedback for debugging */
		status = Internal_Transmit(context, ID, data, 8);
#endif
	}

	/* Display traffic */
	if (context->Callback_Function_Traffic != NULL) {
		context->Callback_Function_Traffic(j1939, ID, 8, data, true); /* ID, 8 bytes of data, data array, TX = true */
	}

	return status;
}

/* Send a PGN request
 * PGN: 0x00EA00 (59904)
 */
ENUM_J1939_STATUS_CODES CAN_Send_Request(J1939 *j1939, uint32_t ID, uint8_t PGN[]) {
	ENUM_J1939_STATUS_CODES status = STATUS_SEND_BUSY;
	CAN_Context *context;
	if (j1939 == NULL || j1939->can_context == NULL || PGN == NULL) {
		return STATUS_SEND_ERROR;
	}
	context = j1939->can_context;
	if (context->Callback_Function_Send != NULL) {
		context->Callback_Function_Send(j1939, ID, 3, PGN);
		status = STATUS_SEND_OK;
	} else {
#if OPENSAE_J1939_TARGET_PLATFORM == STM32
		CAN_TxHeaderTypeDef TxHeader;
		TxHeader.DLC = 3;											/* Here we are only sending 3 bytes */
		TxHeader.RTR = CAN_RTR_DATA;								/* Data frame */
		TxHeader.IDE = CAN_ID_EXT;									/* We want to send an extended ID */
		TxHeader.TransmitGlobalTime = DISABLE;
		TxHeader.ExtId = ID;
		TxHeader.StdId = 0x00; 										/* Not used */
		status = STM32_PLC_CAN_Transmit(j1939, PGN, &TxHeader);
#elif OPENSAE_J1939_TARGET_PLATFORM == ARDUINO
		/* Implement your CAN send 3 bytes message function for the Arduino platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == PIC
		/* Implement your CAN send 3 bytes message function for the PIC platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == AVR
		/* Implement your CAN send 3 bytes message function for the AVR platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == QT_USB
		status = QT_USB_Transmit(j1939, ID, PGN, 3);                       /* PGN is always 3 bytes */
#elif OPENSAE_J1939_TARGET_PLATFORM == SOCKETCAN
		status = (socketcan_transmit(context, ID, PGN, 3) == 0 ? STATUS_SEND_OK : STATUS_SEND_ERROR);
#elif OPENSAE_J1939_TARGET_PLATFORM == INTERNAL_CALLBACK
		status = STATUS_SEND_ERROR;
#else
		/* If no processor are used, use internal feedback for debugging */
		status = Internal_Transmit(context, ID, PGN, 3);
#endif
	}

	/* Display traffic */
	if (context->Callback_Function_Traffic != NULL) {
		context->Callback_Function_Traffic(j1939, ID, 3, PGN, true); /* ID, 3 bytes of data, PGN array, TX = true */
	}

	return status;
}

/* Read the current CAN-bus message. Returning false if the message has been read before, else true */
bool CAN_Read_Message(J1939 *j1939, uint32_t* ID, uint8_t data[]) {
	bool is_new_message = false;
	CAN_Context *context;
	if (j1939 == NULL || j1939->can_context == NULL || ID == NULL || data == NULL) {
		return false;
	}
	context = j1939->can_context;
	if (context->Callback_Function_Read != NULL) {
		context->Callback_Function_Read(j1939, ID, data, &is_new_message);
	} else {
#if OPENSAE_J1939_TARGET_PLATFORM == STM32
		STM32_PLC_CAN_Get_ID_Data(j1939, ID, data, &is_new_message);
#elif OPENSAE_J1939_TARGET_PLATFORM == ARDUINO
		/* Implement your CAN function to get ID, data[] and the flag is_new_message here for the Arduino platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == PIC
		/* Implement your CAN function to get ID, data[] and the flag is_new_message here for the PIC platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == AVR
		/* Implement your CAN function to get ID, data[] and the flag is_new_message here for the AVR platform */
#elif OPENSAE_J1939_TARGET_PLATFORM == QT_USB
		QT_USB_Get_ID_Data(j1939, ID, data, &is_new_message);
#elif OPENSAE_J1939_TARGET_PLATFORM == SOCKETCAN
		socketcan_receive(context, ID, data, &is_new_message);
#elif OPENSAE_J1939_TARGET_PLATFORM == INTERNAL_CALLBACK

#else
		/* If no processor are used, use internal feedback for debugging */
		Internal_Receive(context, ID, data, &is_new_message);
#endif
	}

	/* Display traffic */
	if (context->Callback_Function_Traffic != NULL && is_new_message) {
		context->Callback_Function_Traffic(j1939, *ID, 8, data, false); /* ID, 8 bytes of data, data array, TX = false */
	}

	return is_new_message;
}

void CAN_Context_Init(CAN_Context *context) {
	if (context != NULL) {
		memset(context, 0, sizeof(*context));
		context->can_socket = -1;
	}
}

void CAN_Set_Callback_Functions(CAN_Context *context,
	void (*Callback_Function_Send_)(J1939*, uint32_t, uint8_t, uint8_t[]),
	void (*Callback_Function_Read_)(J1939*, uint32_t*, uint8_t[], bool*),
	void (*Callback_Function_Traffic_)(J1939*, uint32_t, uint8_t, uint8_t[], bool),
	void (*Callback_Function_Delay_ms_)(J1939*, uint8_t)
) {
	if (context == NULL) {
		return;
	}
	context->Callback_Function_Send = Callback_Function_Send_;
	context->Callback_Function_Read = Callback_Function_Read_;
	context->Callback_Function_Traffic = Callback_Function_Traffic_;
	context->Callback_Function_Delay_ms = Callback_Function_Delay_ms_;
}

void CAN_Delay(J1939 *j1939, uint8_t milliseconds) {
	if (j1939 == NULL || j1939->can_context == NULL) {
		return;
	}
	if (j1939->can_context->Callback_Function_Delay_ms != NULL) {
		j1939->can_context->Callback_Function_Delay_ms(j1939, milliseconds);
		return;
	}
#if OPENSAE_J1939_TARGET_PLATFORM == SOCKETCAN
	usleep(milliseconds * 1000);
#else
	(void)milliseconds;
#endif
}
