/*
 * SocketCAN_Transmit_Receive.c
 *
 *  Created on: 24 june 2025
 *      Author: Rickard Hallerbäck
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>

#include "SocketCAN.h"

// Function to initialize SocketCAN
int socketcan_setup(CAN_Context *context, const char *ifname) {
  struct ifreq ifr;
  struct sockaddr_can addr;
  struct timeval tv = {.tv_sec = SOCKETCAN_RCVTIMEOUT / 1000,
                       .tv_usec = (SOCKETCAN_RCVTIMEOUT % 1000) * 1000};

  if (context == NULL || ifname == NULL || strlen(ifname) >= sizeof(ifr.ifr_name)) {
    return -1;
  }

  if (context->can_initialized) {
    return 0; // Already initialized
  }

  // Create the socket
  context->can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (context->can_socket < 0) {
    perror("Error while opening CAN socket");
    return -1;
  }

  if (SOCKETCAN_RCVTIMEOUT > 0) {
    // Setting a SOCKETCAN_RCVTIMEOUT-second read timeout
    if (setsockopt(context->can_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
      perror("setsockopt(SO_RCVTIMEO)");
      socketcan_close(context);
      return -1;
    }
  }

  // Disable reception of own messages
  int disable_loopback = 0;
  if (setsockopt(context->can_socket, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                 &disable_loopback, sizeof(disable_loopback)) < 0) {
    perror("Error disabling loopback");
    socketcan_close(context);
    return -1;
  }

  // Specify the CAN interface
  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, ifname);
  if (ioctl(context->can_socket, SIOCGIFINDEX, &ifr) < 0) {
    fprintf(stderr, "Error getting interface index for '%s'", ifname);
    socketcan_close(context);
    return -1;
  }

  // Bind the socket to the CAN interface
  memset(&addr, 0, sizeof(addr));
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(context->can_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "Error binding CAN socket for '%s'", ifname);
    socketcan_close(context);
    return -1;
  }

  context->can_initialized = true;
  return 0;
}

void socketcan_close(CAN_Context *context) {
  if (context == NULL) {
    return;
  }
  if (context->can_socket >= 0) {
    close(context->can_socket);
  }
  context->can_socket = -1;
  context->can_initialized = false;
}

static bool socketcan_is_initialized(const CAN_Context *context) {
  return context->can_initialized && context->can_socket >= 0;
}

// Transmit a CAN message
int socketcan_transmit(CAN_Context *context, uint32_t ID, const uint8_t data[], uint8_t DLC) {
  if (context == NULL || data == NULL || DLC > 8) {
    return -1;
  }
  if (!socketcan_is_initialized(context)) {
    if (socketcan_setup(context, SOCKETCAN_IFNAME) < 0) {
      return -1;
    }
  }

  if (!context->can_initialized || context->can_socket < 0) {
    return -1;
  }

  struct can_frame frame;
  memset(&frame, 0, sizeof(frame));

  frame.can_id = ID | CAN_EFF_FLAG;
  frame.can_dlc = DLC;

  memcpy(frame.data, data, DLC > 8 ? 8 : DLC);

  int bytes_sent = write(context->can_socket, &frame, sizeof(struct can_frame));

  return (bytes_sent == sizeof(frame)) ? 0 : -1;
}

// Receive a CAN message (non-blocking recommended)
int socketcan_receive(CAN_Context *context, uint32_t *ID, uint8_t data[], bool *is_new_message) {
  if (is_new_message == NULL) {
    return -1;
  }
  *is_new_message = false;
  if (context == NULL || ID == NULL || data == NULL) {
    return -1;
  }
  if (!socketcan_is_initialized(context)) {
    if (socketcan_setup(context, SOCKETCAN_IFNAME) < 0) {
      return -1;
    }
  }

  if (!context->can_initialized || context->can_socket < 0) {
    *is_new_message = false;
    return -1;
  }

  struct can_frame frame;
  ssize_t bytes_read = read(context->can_socket, &frame, sizeof(struct can_frame));

  if (bytes_read < (ssize_t)sizeof(struct can_frame)) {
    *is_new_message = false;
    return -1;
  }

  // Extract the ID, masking out flags
  if (frame.can_id & CAN_EFF_FLAG) {
    *ID = frame.can_id & CAN_EFF_MASK; // 29-bit ID
  } else {
    *ID = frame.can_id & CAN_SFF_MASK; // 11-bit ID
  }

  // Copy up to 8 bytes of data
  uint8_t len = (frame.can_dlc <= 8) ? frame.can_dlc : 8;
  memcpy(data, frame.data, len);

  *is_new_message = true;
  return 0;
}
