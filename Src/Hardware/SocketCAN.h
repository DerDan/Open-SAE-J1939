#ifndef OPENSAE_SOCKETCAN_H
#define OPENSAE_SOCKETCAN_H

#include "../Open_SAE_J1939/Structs.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SOCKETCAN_IFNAME
#define SOCKETCAN_IFNAME "can0"
#endif

#ifndef SOCKETCAN_RCVTIMEOUT
/* Set the read timeout of the bus in milliseconds, 0 means no timeout and it will block on read operations */
#define SOCKETCAN_RCVTIMEOUT 1
#endif

int socketcan_receive(CAN_Context *context, uint32_t *ID, uint8_t data[], bool *is_new_message);
int socketcan_transmit(CAN_Context *context, uint32_t ID, const uint8_t data[], uint8_t DLC);
int socketcan_setup(CAN_Context *context, const char *ifname);
void socketcan_close(CAN_Context *context);

#ifdef __cplusplus
}
#endif

#endif
