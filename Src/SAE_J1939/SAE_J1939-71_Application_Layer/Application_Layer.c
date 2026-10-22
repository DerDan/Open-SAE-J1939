#ifdef __cplusplus
extern "C" {
#endif

#include "Application_Layer.h"

void SAE_J1939_Set_Application_Callback_Function(
    J1939 *j1939, void (*Callback_Function)(J1939*, SAE_Application_Info)
) {
    if (j1939 != NULL) {
        j1939->Callback_Function_Application = Callback_Function;
    }
}

#ifdef __cplusplus
}
#endif
