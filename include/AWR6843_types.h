#ifndef AWR6843_TYPES_H
#define AWR6843_TYPES_H

#if defined(__CC_ARM)
#pragma push
#pragma anon_unions
#elif defined(__ICCARM__)
#pragma language = extended
#elif defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc11-extensions"
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
/* anonymous unions are enabled by default */
#elif defined(__TMS470__)

/* anonymous unions are enabled by default */
#elif defined(__TASKING__)
#pragma warning 586
#elif defined(__CSMC__)

/* anonymous unions are enabled by default */
#endif

#if !(defined(__ASSEMBLY__) || defined(__IAR_SYSTEMS_ASM__))
#include <stdint.h>
#ifndef __cplusplus
typedef volatile const uint32_t RoReg;   /**< Read only 32-bit register (volatile const unsigned int) */
typedef volatile const uint16_t RoReg16; /**< Read only 16-bit register (volatile const unsigned int) */
typedef volatile const uint8_t RoReg8;   /**< Read only  8-bit register (volatile const unsigned int) */
#else
typedef volatile uint32_t RoReg;   /**< Read only 32-bit register (volatile const unsigned int) */
typedef volatile uint16_t RoReg16; /**< Read only 16-bit register (volatile const unsigned int) */
typedef volatile uint8_t RoReg8;   /**< Read only  8-bit register (volatile const unsigned int) */
#endif
typedef volatile uint32_t WoReg;   /**< Write only 32-bit register (volatile unsigned int) */
typedef volatile uint16_t WoReg16; /**< Write only 16-bit register (volatile unsigned int) */
typedef volatile uint8_t WoReg8;   /**< Write only  8-bit register (volatile unsigned int) */
typedef volatile uint32_t RwReg;   /**< Read-Write 32-bit register (volatile unsigned int) */
typedef volatile uint16_t RwReg16; /**< Read-Write 16-bit register (volatile unsigned int) */
typedef volatile uint8_t RwReg8;   /**< Read-Write  8-bit register (volatile unsigned int) */
#endif

#endif /* AWR6843_TYPES_H */
