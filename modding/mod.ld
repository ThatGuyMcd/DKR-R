/* SDK-only code never requires a game ROM or embeds game assets. */
RAMBASE = 0x81000000;
MEMORY { extram (ARWX) : ORIGIN = RAMBASE, LENGTH = 4M }
SECTIONS {
    /DISCARD/ : { *(.got) *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) }
}
