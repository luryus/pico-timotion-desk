#ifndef __FLASH_HH
#define __FLASH_HH

#include <cstdint>

extern uint8_t PERSISTENT_FLASH_START[];

#define PERSISTENT_FLASH_OFFSET (PERSISTENT_FLASH_START - reinterpret_cast<uint8_t*>(XIP_BASE))

#endif