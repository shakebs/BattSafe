#include <stdio.h>
#include "packet_format.h"
int main() {
    printf("telemetry_pack_frame_t: %zu bytes (expected %d)\n", sizeof(telemetry_pack_frame_t), PACKET_PACK_SIZE);
    printf("telemetry_module_frame_t: %zu bytes (expected %d)\n", sizeof(telemetry_module_frame_t), PACKET_MODULE_SIZE);
    return 0;
}
