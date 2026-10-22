#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Open_SAE_J1939/Open_SAE_J1939.h"

typedef struct {
    CAN_Context context;
    J1939 *instance;
    uint32_t id;
    uint8_t data[8];
    bool pending;
    unsigned int application_calls;
} TestDriver;

static void require(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(EXIT_FAILURE);
    }
}

static TestDriver *get_driver(J1939 *j1939) {
    TestDriver *driver = (TestDriver *)j1939->can_context;
    require(driver->instance == j1939, "Const callback received the wrong instance");
    return driver;
}

static void send_frame(J1939 *j1939, uint32_t id, uint8_t dlc, const uint8_t data[]) {
    TestDriver *driver = get_driver(j1939);
    require(dlc <= sizeof(driver->data), "Invalid transmit length");
    driver->id = id;
    memset(driver->data, 0, sizeof(driver->data));
    memcpy(driver->data, data, dlc);
    driver->pending = true;
}

static void read_frame(J1939 *j1939, uint32_t *id, uint8_t data[], bool *is_new) {
    TestDriver *driver = get_driver(j1939);
    *is_new = driver->pending;
    if (*is_new) {
        *id = driver->id;
        memcpy(data, driver->data, sizeof(driver->data));
        driver->pending = false;
    }
}

static void traffic(J1939 *j1939, uint32_t id, uint8_t dlc, const uint8_t data[], bool is_transmit) {
    TestDriver *driver = get_driver(j1939);
    (void)is_transmit;
    require(id == driver->id && memcmp(data, driver->data, dlc) == 0, "Const traffic view changed the frame");
}

static void application(J1939 *j1939, SAE_Application_Info info) {
    const uint8_t request[3] = {0xDA, 0xFE, 0};
    TestDriver *driver = get_driver(j1939);
    require(info.type == IDENTIFICATION_SOFTWARE, "Unexpected application view");
    require(info.software_identification == &j1939->from_other_ecu_identifications.software_identification, "Wrong application view owner");
    require(info.software_identification->identifications[0] == 'v', "Incorrect read-only application data");
    driver->application_calls++;
    require(CAN_Send_Request(j1939, 0x18EAA290, request) == STATUS_SEND_OK, "Application callback could not send a const request");
}

static void verify_const_views(void) {
    const Software_identification software = {0};
    const ECU_identification ecu = {0};
    const Component_identification component = {0};
    const Proprietary_A proprietary_a = {0};
    const Proprietary_B proprietary_b = {0};
    SAE_Application_Info info;
    info.type = IDENTIFICATION_SOFTWARE;
    info.software_identification = &software;
    require(info.software_identification == &software, "Software view rejected const storage");
    info.type = IDENTIFICATION_ECU;
    info.ecu_identification = &ecu;
    require(info.ecu_identification == &ecu, "ECU view rejected const storage");
    info.type = IDENTIFICATION_COMPONENT;
    info.component_identification = &component;
    require(info.component_identification == &component, "Component view rejected const storage");
    info.type = PROPRIETARY_A;
    info.proprietary_a = &proprietary_a;
    require(info.proprietary_a == &proprietary_a, "Proprietary A view rejected const storage");
    info.type = PROPRIETARY_B;
    info.proprietary_b = &proprietary_b;
    require(info.proprietary_b == &proprietary_b, "Proprietary B view rejected const storage");
}

int main(void) {
    TestDriver driver = {0};
    J1939 j1939 = {0};
    const J1939 *read_only_instance = &j1939;
    const uint8_t payload[8] = {1, 'v', 2, 3, 4, 5, 6, 7};
    const uint8_t request[3] = {0xDA, 0xFE, 0};
    const char file_name[] = "const_buffer.bin";
    uint8_t received[8] = {0};
    uint32_t id = 0;

    verify_const_views();
    CAN_Context_Init(&driver.context);
    driver.instance = &j1939;
    j1939.can_context = &driver.context;
    CAN_Set_Callback_Functions(&driver.context, send_frame, read_frame, traffic, NULL);
    require(CAN_Send_Message(&j1939, 0x18FEDA90, payload) == STATUS_SEND_OK, "Const message send failed");
    require(CAN_Read_Message(&j1939, &id, received), "Mutable receive buffer was not filled");
    require(id == 0x18FEDA90 && memcmp(received, payload, 8) == 0, "Const message payload was corrupted");
    require(CAN_Send_Request(&j1939, 0x18EAA290, request) == STATUS_SEND_OK, "Const request send failed");
    require(CAN_Read_Message(&j1939, &id, received), "Const request was not received");
    require(memcmp(received, request, 3) == 0 && received[3] == 0, "Const request payload was corrupted");
    SAE_J1939_Set_Application_Callback_Function(&j1939, application);
    SAE_J1939_Read_Response_Request_Software_Identification(&j1939, 0xA2, payload);
    require(driver.application_calls == 1, "Read-only decoder did not invoke its callback");
    require(SAE_J1939_Send_Binary_Data_Transfer_DM16(&j1939, 0xA2, 3, request) == STATUS_SEND_OK, "Const DM16 payload was rejected");
    require(Save_Struct(payload, sizeof(payload), file_name), "Const save data or filename was rejected");
    require(Load_Struct(received, sizeof(received), file_name), "Const load filename was rejected");
    require(memcmp(received, payload, sizeof(payload)) == 0, "Saved const payload was corrupted");
    require(remove(file_name) == 0, "Failed to remove const buffer test file");
    require(Open_SAE_J1939_Closedown_ECU(read_only_instance), "Saving a const ECU instance failed");
    require(remove(INFORMATION_THIS_ECU) == 0, "Failed to remove ECU information test file");
    return 0;
}
