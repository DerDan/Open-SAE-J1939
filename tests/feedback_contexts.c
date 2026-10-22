#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Hardware/Hardware.h"

static void require(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    CAN_Context first_context;
    CAN_Context second_context;
    J1939 first_ecu = {0};
    J1939 peer_ecu = {0};
    J1939 second_ecu = {0};
    uint8_t first_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t second_data[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    uint8_t received[8] = {0};
    uint32_t id = 0;
    unsigned int frame_index;

    memset(&first_context, 0xA5, sizeof(first_context));
    CAN_Context_Init(&first_context);
    CAN_Context_Init(&second_context);
    require(first_context.can_socket == -1 && !first_context.can_initialized, "Invalid initial SocketCAN state");
    require(first_context.Tag == 0 && first_context.Callback_Function_Send == NULL, "Context was not cleared");
    first_ecu.can_context = &first_context;
    peer_ecu.can_context = &first_context;
    second_ecu.can_context = &second_context;
    require(!CAN_Read_Message(&first_ecu, &id, received), "Uninitialized feedback frame");
    require(CAN_Send_Message(&first_ecu, 0x18FEDA90, first_data) == STATUS_SEND_OK, "First feedback send failed");
    require(!CAN_Read_Message(&second_ecu, &id, received), "Feedback frame leaked into another context");
    require(CAN_Send_Message(&second_ecu, 0x18FEDA90, second_data) == STATUS_SEND_OK, "Second feedback send failed");
    require(CAN_Read_Message(&peer_ecu, &id, received), "Shared context lost the frame");
    require(id == 0x18FEDA90 && memcmp(received, first_data, 8) == 0, "Wrong first feedback frame");
    require(CAN_Read_Message(&second_ecu, &id, received), "Second context lost the frame");
    require(id == 0x18FEDA90 && memcmp(received, second_data, 8) == 0, "Wrong second feedback frame");
    require(!CAN_Read_Message(&first_ecu, &id, received), "Feedback frame was consumed twice");
    require(CAN_Send_Request(&first_ecu, 0x18EAFF90, first_data) == STATUS_SEND_OK, "Request feedback send failed");
    memset(received, 0, sizeof(received));
    require(CAN_Read_Message(&peer_ecu, &id, received), "Request feedback frame lost");
    require(id == 0x18EAFF90 && memcmp(received, first_data, 3) == 0 && received[3] == 0, "Invalid request feedback length");
    for (frame_index = 0; frame_index < 300; frame_index++) {
        first_data[0] = (uint8_t)frame_index;
        require(CAN_Send_Message(&first_ecu, frame_index, first_data) == STATUS_SEND_OK, "Feedback ring send failed");
        require(CAN_Read_Message(&peer_ecu, &id, received), "Feedback ring wrap lost a frame");
        require(id == frame_index && received[0] == (uint8_t)frame_index, "Feedback ring wrap corrupted a frame");
    }
    require(!CAN_Read_Message(&second_ecu, &id, received), "Feedback ring wrap affected another bus");
    require(CAN_Send_Message(&first_ecu, 0, NULL) == STATUS_SEND_ERROR, "Null message data was accepted");
    require(CAN_Send_Request(&first_ecu, 0, NULL) == STATUS_SEND_ERROR, "Null request data was accepted");
    require(!CAN_Read_Message(&first_ecu, NULL, received), "Null identifier was accepted");
    require(!CAN_Read_Message(&first_ecu, &id, NULL), "Null receive data was accepted");
    return 0;
}
