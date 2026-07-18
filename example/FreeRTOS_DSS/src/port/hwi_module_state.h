/*
 * hwi_module_state.h -- struct layout for
 * ti_sysbios_family_c64p_Hwi_Module_State, the ONE piece of context
 * Hwi_disp_always.s64P's own assembly reads directly (its `.bss` field,
 * used to reload B14/STATIC_BASE on every interrupt entry -- see that
 * file's `ldw *a9, b14`). Hwi_disp_always.asm's `.cdecls` pulls this header
 * in (instead of SYS/BIOS's own generated package/internal/Hwi.xdc.h,
 * which this project doesn't have/want the rest of) purely so its `.tag`
 * directive can resolve `_ti_sysbios_family_c64p_Hwi_Module__state__V.bss`
 * to the right byte offset automatically, without hand-computing it.
 *
 * Field order/types match TI's own PDK FreeRTOS C66x port's static
 * initializer for this exact struct (ti/kernel/freertos/portable/TI_CGT/
 * c66/port_Hwi_c66.c): ierMask, intNum, taskSP, isrStack, vectorTableBase,
 * bss, scw.
 */
#ifndef HWI_MODULE_STATE_H
#define HWI_MODULE_STATE_H

#include <stdint.h>

typedef struct ti_sysbios_family_c64p_Hwi_Module_State {
    uint16_t ierMask;
    int32_t  intNum;
    char    *taskSP;
    char    *isrStack;
    void    *vectorTableBase;
    void    *bss;
    int32_t  scw;
} ti_sysbios_family_c64p_Hwi_Module_State;

/* Hwi_disp_always.asm's own `.cdecls` of this header is what makes its
 * `.tag`-based field access work at all -- TI's assembler needs an extern
 * C declaration to generate the underlying `.ref`, not just the struct
 * type itself (confirmed the hard way: without this line, cl6x's
 * assembler rejects the `.tag` directive with "Can't tag an undefined
 * symbol", since the real definition/initializer lives in hwi_dispatch.c,
 * a separate translation unit the assembler can't see). */
extern ti_sysbios_family_c64p_Hwi_Module_State ti_sysbios_family_c64p_Hwi_Module__state__V;

#endif
