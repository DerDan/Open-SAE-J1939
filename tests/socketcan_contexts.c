#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include "Hardware/Hardware.h"
#include "Hardware/SocketCAN.h"

static struct can_frame frames[16];
static bool pending[16];
static unsigned int closed[16];
static int interfaces[16];
static int next_socket = 3;
static int failing_operation;

static void require(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(EXIT_FAILURE);
    }
}

int test_socket(int domain, int type, int protocol) {
    require(domain == PF_CAN && type == SOCK_RAW && protocol == CAN_RAW, "Incorrect CAN socket type");
    return failing_operation == 1 ? -1 : next_socket++;
}

int test_setsockopt(int descriptor, int level, int option, const void *value, socklen_t length) {
    (void)descriptor;
    (void)level;
    (void)option;
    (void)value;
    (void)length;
    return failing_operation == 2 ? -1 : 0;
}

int test_ioctl(int descriptor, unsigned long request, ...) {
    va_list arguments;
    struct ifreq *interface_request;
    (void)descriptor;
    require(request == SIOCGIFINDEX, "Incorrect interface ioctl");
    va_start(arguments, request);
    interface_request = va_arg(arguments, struct ifreq *);
    va_end(arguments);
    interface_request->ifr_ifindex = strcmp(interface_request->ifr_name, "testcan0") == 0 ? 10 : 20;
    return failing_operation == 3 ? -1 : 0;
}

int test_bind(int descriptor, const struct sockaddr *address, socklen_t length) {
    const struct sockaddr_can *can_address = (const struct sockaddr_can *)address;
    require(length == sizeof(*can_address) && can_address->can_family == AF_CAN, "Incorrect CAN bind address");
    interfaces[descriptor] = can_address->can_ifindex;
    return failing_operation == 4 ? -1 : 0;
}

int test_close(int descriptor) {
    require(descriptor >= 3 && descriptor < 16, "Closing an invalid socket");
    closed[descriptor]++;
    return 0;
}

ssize_t test_write(int descriptor, const void *buffer, size_t length) {
    require(length == sizeof(struct can_frame), "Incorrect CAN frame write size");
    memcpy(&frames[descriptor], buffer, length);
    pending[descriptor] = true;
    return failing_operation == 5 ? -1 : (ssize_t)length;
}

ssize_t test_read(int descriptor, void *buffer, size_t length) {
    if (!pending[descriptor] || failing_operation == 6) {
        return -1;
    }
    memcpy(buffer, &frames[descriptor], length);
    pending[descriptor] = false;
    return (ssize_t)length;
}

int test_usleep(useconds_t microseconds) {
    (void)microseconds;
    return 0;
}

int main(void) {
    CAN_Context first_context;
    CAN_Context second_context;
    CAN_Context failed_context;
    J1939 first_ecu = {0};
    J1939 second_ecu = {0};
    uint8_t first_data[8] = {1, 2, 3};
    uint8_t second_data[8] = {9, 8, 7};
    uint8_t received[8] = {0};
    uint32_t id = 0;
    bool is_new = true;
    int failed_descriptor;
    int operation;

    CAN_Context_Init(&first_context);
    CAN_Context_Init(&second_context);
    first_ecu.can_context = &first_context;
    second_ecu.can_context = &second_context;
    require(socketcan_setup(&first_context, "testcan0") == 0, "First SocketCAN setup failed");
    require(socketcan_setup(&second_context, "testcan1") == 0, "Second SocketCAN setup failed");
    require(first_context.can_socket != second_context.can_socket, "SocketCAN contexts share a descriptor");
    require(interfaces[first_context.can_socket] == 10 && interfaces[second_context.can_socket] == 20, "SocketCAN contexts bound to the wrong interfaces");
    require(socketcan_setup(&first_context, "testcan0") == 0 && next_socket == 5, "SocketCAN setup leaked an existing socket");
    require(CAN_Send_Message(&first_ecu, 0x18FEDA90, first_data) == STATUS_SEND_OK, "SocketCAN success was not mapped to STATUS_SEND_OK");
    require(!CAN_Read_Message(&second_ecu, &id, received), "SocketCAN read selected another context's descriptor");
    require(CAN_Send_Request(&second_ecu, 0x18FEDA90, second_data) == STATUS_SEND_OK, "SocketCAN request send failed");
    require(frames[first_context.can_socket].can_dlc == 8 && frames[second_context.can_socket].can_dlc == 3, "SocketCAN DLC did not follow the request type");
    require(CAN_Read_Message(&first_ecu, &id, received) && id == 0x18FEDA90 && memcmp(received, first_data, 8) == 0, "First SocketCAN data was corrupted");
    require(CAN_Read_Message(&second_ecu, &id, received) && id == 0x18FEDA90 && memcmp(received, second_data, 3) == 0, "Second SocketCAN data was corrupted");
    failing_operation = 5;
    require(CAN_Send_Message(&first_ecu, 0, first_data) == STATUS_SEND_ERROR, "SocketCAN write failure was not propagated");
    failing_operation = 0;
    failed_descriptor = first_context.can_socket;
    socketcan_close(&first_context);
    socketcan_close(&first_context);
    require(closed[failed_descriptor] == 1 && first_context.can_socket == -1 && !first_context.can_initialized, "SocketCAN cleanup did not release exactly one descriptor");
    require(second_context.can_initialized && closed[second_context.can_socket] == 0, "SocketCAN cleanup closed another context");
    require(CAN_Send_Message(&second_ecu, 1, second_data) == STATUS_SEND_OK, "Other SocketCAN context stopped after cleanup");
    socketcan_close(&second_context);
    for (operation = 1; operation <= 4; operation++) {
        CAN_Context_Init(&failed_context);
        failing_operation = operation;
        failed_descriptor = next_socket;
        require(socketcan_setup(&failed_context, "testcan0") == -1, "SocketCAN setup failure was ignored");
        require(!failed_context.can_initialized && failed_context.can_socket == -1, "SocketCAN setup failure retained a stale descriptor");
        if (operation != 1) {
            require(closed[failed_descriptor] == 1, "Failed SocketCAN setup leaked its socket");
        }
    }
    failing_operation = 0;
    require(socketcan_setup(NULL, "testcan0") == -1, "Null SocketCAN context was accepted");
    require(socketcan_setup(&failed_context, NULL) == -1, "Null interface was accepted");
    require(socketcan_setup(&failed_context, "interface-name-too-long") == -1, "Overlong interface name was accepted");
    require(socketcan_receive(NULL, &id, received, &is_new) == -1 && !is_new, "Invalid SocketCAN receive did not clear its flag");
    require(socketcan_transmit(&failed_context, 0, first_data, 9) == -1, "Invalid CAN DLC was accepted");
    socketcan_close(NULL);
    return 0;
}
