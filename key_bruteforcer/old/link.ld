OUTPUT_ARCH(arm)

SECTIONS {
    /* Place code in DTCM */
    . = (0x20000000);

    /* Place init section first, so it's possible to just jump to the base address */
    .init : {
        *(.init*);
    }
    .text : {
        *(.text*);
    }
    .rodata : {
        *(.rodata*)
    }
    .data : {
        *(.data*)
    }

    /DISCARD/ : {
        *(*);
    }
}
