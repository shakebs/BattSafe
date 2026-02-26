/*
 * hal_platform.h — Platform Detection
 *
 * Detects whether we're compiling for:
 *   - HOST (your Mac) — uses mock implementations for testing
 *   - TARGET (VSDSquadron ULTRA / THEJAS32) — uses real hardware registers
 *
 * All HAL modules include this to decide which implementation to use.
 */

#ifndef HAL_PLATFORM_H
#define HAL_PLATFORM_H

/*
 * Define TARGET_THEJAS32 when compiling for the real board.
 * When NOT defined, we compile in HOST mode (mock/simulation).
 *
 * For the THEJAS32 toolchain, add -DTARGET_THEJAS32 to your compiler flags.
 * For Mac testing, don't define it — you get mock implementations.
 */
#ifndef TARGET_THEJAS32
#define HAL_HOST_MODE 1
#else
#define HAL_HOST_MODE 0
#endif

#include <stdbool.h>
#include <stdint.h>

/* Common status codes used by all HAL functions */
typedef enum {
  HAL_OK = 0,
  HAL_ERROR = -1,
  HAL_TIMEOUT = -2,
  HAL_BUSY = -3,
} hal_status_t;

#endif /* HAL_PLATFORM_H */
