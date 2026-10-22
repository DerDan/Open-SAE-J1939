#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Open_SAE_J1939/Open_SAE_J1939.h"

typedef struct {
    uint32_t id;
    uint8_t data[8];
} Frame;

typedef struct {
    CAN_Context context;
    J1939 requester;
    J1939 responder;
    J1939 *expected_instance;
    Frame frames[64];
    unsigned int sent;
    unsigned int received;
    unsigned int traffic_sent;
    unsigned int traffic_received;
    unsigned int delays;
    unsigned int application_calls[5];
    unsigned int responder_notifications;
} TestBus;

static void require(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(EXIT_FAILURE);
    }
}

static TestBus *get_bus(J1939 *j1939) {
    TestBus *bus = (TestBus *)j1939->can_context;
    require(j1939 == bus->expected_instance, "Callback received the wrong ECU instance");
    require(bus->context.Tag == 1 || bus->context.Tag == 2, "Callback lost the bus tag");
    return bus;
}

static void send_frame(J1939 *j1939, uint32_t id, uint8_t dlc, const uint8_t data[]) {
    TestBus *bus = get_bus(j1939);
    require(bus->sent < 64 && dlc <= 8, "Test frame queue overflow");
    bus->frames[bus->sent].id = id;
    memset(bus->frames[bus->sent].data, 0, 8);
    memcpy(bus->frames[bus->sent].data, data, dlc);
    bus->sent++;
}

static void second_send_frame(J1939 *j1939, uint32_t id, uint8_t dlc, const uint8_t data[]) {
    require(j1939->can_context->Tag == 2, "CAN callback registration leaked between buses");
    send_frame(j1939, id, dlc, data);
}

static void read_frame(J1939 *j1939, uint32_t *id, uint8_t data[], bool *is_new) {
    TestBus *bus = get_bus(j1939);
    *is_new = bus->received < bus->sent;
    if (*is_new) {
        *id = bus->frames[bus->received].id;
        memcpy(data, bus->frames[bus->received].data, 8);
        bus->received++;
    }
}

static void traffic(J1939 *j1939, uint32_t id, uint8_t dlc, const uint8_t data[], bool is_transmit) {
    TestBus *bus = get_bus(j1939);
    unsigned int frame_index = is_transmit ? bus->sent - 1 : bus->received - 1;
    require(id == bus->frames[frame_index].id, "Traffic callback received the wrong identifier");
    require(memcmp(data, bus->frames[frame_index].data, dlc) == 0, "Traffic callback received the wrong data");
    if (is_transmit) {
        bus->traffic_sent++;
    } else {
        bus->traffic_received++;
    }
}

static void delay_ms(J1939 *j1939, uint8_t milliseconds) {
    TestBus *bus = get_bus(j1939);
    (void)milliseconds;
    bus->delays++;
}

static void application(J1939 *j1939, SAE_Application_Info info) {
    TestBus *bus = get_bus(j1939);
    require(j1939 == &bus->requester, "Application callback received the sender instance");
    switch (info.type) {
    case IDENTIFICATION_SOFTWARE:
        require(info.software_identification == &j1939->from_other_ecu_identifications.software_identification, "Wrong software identification owner");
        break;
    case IDENTIFICATION_ECU:
        require(info.ecu_identification == &j1939->from_other_ecu_identifications.ecu_identification, "Wrong ECU identification owner");
        break;
    case IDENTIFICATION_COMPONENT:
        require(info.component_identification == &j1939->from_other_ecu_identifications.component_identification, "Wrong component identification owner");
        break;
    case PROPRIETARY_A:
        require(info.proprietary_a == &j1939->from_other_ecu_proprietary.proprietary_A, "Wrong Proprietary A owner");
        break;
    case PROPRIETARY_B:
        require(info.proprietary_b == &j1939->from_other_ecu_proprietary.proprietary_B[0], "Wrong Proprietary B owner");
        break;
    default:
        require(false, "Unexpected application notification");
    }
    bus->application_calls[info.type]++;
}

static void second_application(J1939 *j1939, SAE_Application_Info info) {
    require(j1939->can_context->Tag == 2, "Application callback registration leaked between instances");
    application(j1939, info);
}

static void responder_application(J1939 *j1939, SAE_Application_Info info) {
    TestBus *bus = get_bus(j1939);
    require(j1939 == &bus->responder && info.type == PROPRIETARY_A, "Wrong callback for an ECU sharing the context");
    require(info.proprietary_a == &j1939->from_other_ecu_proprietary.proprietary_A, "Wrong shared-context data owner");
    bus->responder_notifications++;
}

static void initialize_bus(TestBus *bus, uint32_t tag, const char *software) {
    memset(bus, 0, sizeof(*bus));
    CAN_Context_Init(&bus->context);
    bus->context.Tag = tag;
    bus->requester.can_context = &bus->context;
    bus->responder.can_context = &bus->context;
    bus->requester.information_this_ECU.this_ECU_address = 0x90;
    bus->responder.information_this_ECU.this_ECU_address = 0xA2;
    bus->responder.information_this_ECU.this_identifications.software_identification.number_of_fields = 15;
    memcpy(bus->responder.information_this_ECU.this_identifications.software_identification.identifications, software, 15);
    CAN_Set_Callback_Functions(&bus->context, tag == 1 ? send_frame : second_send_frame, read_frame, traffic, delay_ms);
    SAE_J1939_Set_Application_Callback_Function(&bus->requester, tag == 1 ? application : second_application);
}

static void start_transfer(TestBus *bus) {
    bus->expected_instance = &bus->requester;
    require(SAE_J1939_Send_Request_Software_Identification(&bus->requester, 0xA2) == STATUS_SEND_OK, "Software request failed");
}

static void listen(TestBus *bus, J1939 *j1939) {
    bus->expected_instance = j1939;
    require(Open_SAE_J1939_Listen_For_Messages(j1939), "Expected a pending transport frame");
}

static void verify_transfer(TestBus *bus, const char *software) {
    require(bus->requester.from_other_ecu_identifications.software_identification.number_of_fields == 15, "Incorrect software identification length");
    require(memcmp(bus->requester.from_other_ecu_identifications.software_identification.identifications, software, 15) == 0, "Transport data crossed CAN contexts");
    require(bus->application_calls[IDENTIFICATION_SOFTWARE] == 1, "Software callback count is incorrect");
    require(bus->responder.from_other_ecu_tp_cm.total_number_of_bytes_received == 16, "Incorrect EOM byte total");
    require(bus->responder.from_other_ecu_tp_cm.total_number_of_packages_received == 3, "Incorrect EOM packet total");
    require(bus->sent == bus->received, "Transport left an unread frame");
    require(bus->traffic_sent == bus->sent && bus->traffic_received == bus->received, "Traffic notifications were lost");
}

static void verify_application_types(TestBus *bus) {
    uint8_t data[8] = {0};
    unsigned int info_type;
    J1939 *j1939 = &bus->requester;
    SAE_J1939_Set_Application_Callback_Function(&bus->responder, responder_application);
    bus->expected_instance = j1939;
    j1939->from_other_ecu_identifications.ecu_identification.length_of_each_field = 1;
    j1939->from_other_ecu_identifications.component_identification.length_of_each_field = 1;
    j1939->from_other_ecu_proprietary.proprietary_A.total_bytes = 8;
    j1939->from_other_ecu_proprietary.proprietary_B[0].pgn = 0xFF00;
    j1939->from_other_ecu_proprietary.proprietary_B[0].total_bytes = 8;
    SAE_J1939_Read_Response_Request_ECU_Identification(j1939, 0xA2, data);
    SAE_J1939_Read_Response_Request_Component_Identification(j1939, 0xA2, data);
    SAE_J1939_Read_Response_Request_Proprietary_A(j1939, 0xA2, data);
    SAE_J1939_Read_Response_Request_Proprietary_B(j1939, 0xA2, 0xFF00, data);
    for (info_type = 0; info_type < 5; info_type++) {
        require(bus->application_calls[info_type] == 1, "Application notification did not reach its instance");
    }
    SAE_J1939_Set_Application_Callback_Function(j1939, NULL);
    SAE_J1939_Read_Response_Request_Proprietary_A(j1939, 0xA2, data);
    require(bus->application_calls[PROPRIETARY_A] == 1, "Application callback was not unregistered");
    require(bus->responder.Callback_Function_Application == responder_application, "Application registration affected an ECU sharing the context");
    bus->expected_instance = &bus->responder;
    bus->responder.from_other_ecu_proprietary.proprietary_A.total_bytes = 8;
    SAE_J1939_Read_Response_Request_Proprietary_A(&bus->responder, 0x90, data);
    require(bus->responder_notifications == 1, "An ECU sharing the context lost its application callback");
}

int main(void) {
    TestBus first_bus;
    TestBus second_bus;
    struct TP_CM other_transport;
    struct TP_DT other_data;
    unsigned int packet;
    uint8_t data[8] = {0};
    uint32_t id = 0;
    J1939 detached = {0};

    CAN_Context_Init(NULL);
    CAN_Set_Callback_Functions(NULL, NULL, NULL, NULL, NULL);
    SAE_J1939_Set_Application_Callback_Function(NULL, NULL);
    require(CAN_Send_Message(NULL, 0, data) == STATUS_SEND_ERROR, "Null ECU send was accepted");
    require(CAN_Send_Request(&detached, 0, data) == STATUS_SEND_ERROR, "Detached ECU send was accepted");
    require(!CAN_Read_Message(&detached, &id, data), "Detached ECU read was accepted");
    CAN_Delay(&detached, 1);

    initialize_bus(&first_bus, 1, "First CAN bus!");
    initialize_bus(&second_bus, 2, "Other CAN bus!");
    start_transfer(&first_bus);
    start_transfer(&second_bus);
    require(first_bus.frames[0].id == second_bus.frames[0].id, "Test buses must use identical CAN identifiers");
    listen(&first_bus, &first_bus.responder);
    listen(&second_bus, &second_bus.responder);
    listen(&second_bus, &second_bus.requester);
    listen(&first_bus, &first_bus.requester);
    for (packet = 0; packet < 3; packet++) {
        listen(&first_bus, &first_bus.responder);
        listen(&second_bus, &second_bus.responder);
        listen(&second_bus, &second_bus.requester);
        listen(&first_bus, &first_bus.requester);
    }
    listen(&second_bus, &second_bus.responder);
    listen(&first_bus, &first_bus.responder);
    verify_transfer(&first_bus, "First CAN bus!");
    verify_transfer(&second_bus, "Other CAN bus!");
    verify_application_types(&first_bus);
    verify_application_types(&second_bus);

    first_bus.expected_instance = &first_bus.responder;
    second_bus.expected_instance = &second_bus.responder;
    require(SAE_J1939_Response_Request_Software_Identification(&first_bus.responder, 0xFF) == STATUS_SEND_OK, "First BAM send failed");
    require(first_bus.delays == 3 && second_bus.delays == 0, "BAM delays crossed CAN contexts");
    require(SAE_J1939_Response_Request_Software_Identification(&second_bus.responder, 0xFF) == STATUS_SEND_OK, "Second BAM send failed");
    require(second_bus.delays == 3, "Second BAM delay callback was not invoked");

    initialize_bus(&first_bus, 1, "First CAN bus!");
    initialize_bus(&second_bus, 2, "Other CAN bus!");
    start_transfer(&first_bus);
    start_transfer(&second_bus);
    listen(&first_bus, &first_bus.responder);
    listen(&second_bus, &second_bus.responder);
    listen(&first_bus, &first_bus.requester);
    listen(&second_bus, &second_bus.requester);
    memcpy(&other_transport, &second_bus.requester.from_other_ecu_tp_cm, sizeof(other_transport));
    memcpy(&other_data, &second_bus.requester.from_other_ecu_tp_dt, sizeof(other_data));
    first_bus.expected_instance = &first_bus.responder;
    first_bus.responder.this_ecu_tp_cm.control_byte = CONTROL_BYTE_TP_CM_ABORT;
    first_bus.responder.this_ecu_tp_cm.connecting_abort_reason = 3;
    first_bus.responder.this_ecu_tp_cm.PGN_of_the_packeted_message = PGN_SOFTWARE_IDENTIFICATION;
    require(SAE_J1939_Send_Transport_Protocol_Connection_Management(&first_bus.responder, 0x90) == STATUS_SEND_OK, "Abort send failed");
    listen(&first_bus, &first_bus.responder);
    listen(&first_bus, &first_bus.requester);
    require(first_bus.requester.from_other_ecu_tp_cm.connecting_abort_reason == 3, "Abort did not reach its bus");
    require(memcmp(&other_transport, &second_bus.requester.from_other_ecu_tp_cm, sizeof(other_transport)) == 0, "Abort changed another bus's transport state");
    require(memcmp(&other_data, &second_bus.requester.from_other_ecu_tp_dt, sizeof(other_data)) == 0, "Abort changed another bus's data buffer");
    for (packet = 0; packet < 3; packet++) {
        listen(&second_bus, &second_bus.responder);
        listen(&second_bus, &second_bus.requester);
    }
    listen(&second_bus, &second_bus.responder);
    verify_transfer(&second_bus, "Other CAN bus!");

    CAN_Set_Callback_Functions(&first_bus.context, NULL, NULL, NULL, NULL);
    require(CAN_Send_Message(&first_bus.requester, 0, data) == STATUS_SEND_ERROR, "Missing send callback was accepted");
    require(!CAN_Read_Message(&first_bus.requester, &id, data), "Missing read callback produced a frame");
    CAN_Delay(&first_bus.requester, 1);
    return 0;
}
