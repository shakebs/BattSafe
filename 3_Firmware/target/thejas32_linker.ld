/* ==========================================================================
 * thejas32_linker.ld — Official-format linker script for VSDSquadron ULTRA
 *
 * Based on the VSD Arduino BSP (link.lds) with exact same section layout.
 * SRAM: 256KB starting at 0x200000
 * ========================================================================== */

OUTPUT_ARCH( "riscv" )

ENTRY( _start )

MEMORY
{
  /* ROM bootloader (UART mode) loads code at 0x200000.
   * If using flasher_arduino.bin, change to 0x202000. */
  ram (wxa!ri) : ORIGIN = 0x200000, LENGTH = 256K
}

PHDRS
{
  ram PT_LOAD;
  ram_init PT_LOAD;
  ram PT_NULL;
}

SECTIONS
{
  __stack_size = 8K;
  __heap_size = 8K;

  /* --- .init section: must be FIRST (contains _start) --- */
  .init :
  {
    KEEP (*(SORT_NONE(.init)))
  } >ram AT>ram :ram

  .text :
  {
    *(.text.unlikely .text.unlikely.*)
    *(.text.startup .text.startup.*)
    *(.text .text.*)
    *(.gnu.linkonce.t.*)
  } >ram AT>ram :ram

  .fini :
  {
    KEEP (*(SORT_NONE(.fini)))
  } >ram AT>ram :ram

  PROVIDE (__etext = .);
  PROVIDE (_etext = .);
  PROVIDE (etext = .);

  .rodata :
  {
    *(.rdata)
    *(.rodata .rodata.*)
    *(.gnu.linkonce.r.*)
  } >ram AT>ram :ram

  . = ALIGN(4);

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >ram AT>ram :ram

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >ram AT>ram :ram

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
    KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >ram AT>ram :ram

  .ctors :
  {
    KEEP (*crtbegin.o(.ctors))
    KEEP (*crtbegin?.o(.ctors))
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
  } >ram AT>ram :ram

  .dtors :
  {
    KEEP (*crtbegin.o(.dtors))
    KEEP (*crtbegin?.o(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
  } >ram AT>ram :ram

  .lalign :
  {
    . = ALIGN(4);
    PROVIDE( _data_lma = . );
  } >ram AT>ram :ram

  .dalign :
  {
    . = ALIGN(4);
    PROVIDE( _data = . );
  } >ram AT>ram :ram_init

  .data :
  {
    *(.data .data.*)
    *(.gnu.linkonce.d.*)
  } >ram AT>ram :ram_init

  .srodata :
  {
    PROVIDE( _gp = . + 0x800 );
    *(.srodata.cst16)
    *(.srodata.cst8)
    *(.srodata.cst4)
    *(.srodata.cst2)
    *(.srodata .srodata.*)
  } >ram AT>ram :ram_init

  .sdata :
  {
    *(.sdata .sdata.*)
    *(.gnu.linkonce.s.*)
  } >ram AT>ram :ram_init

  . = ALIGN(4);
  PROVIDE( _edata = . );
  PROVIDE( edata = . );

  PROVIDE( _fbss = . );
  PROVIDE( __bss_start = . );
  .bss :
  {
    *(.sbss*)
    *(.gnu.linkonce.sb.*)
    *(.bss .bss.*)
    *(.gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(4);
  } >ram AT>ram :ram

  . = ALIGN(8);
  PROVIDE( _end = . );
  PROVIDE( end = . );

  .heap :
  {
    PROVIDE( _heap_end = (. + __heap_size));
  } >ram AT>ram :ram

  .stack :
  {
    PROVIDE( _sp = (. + __heap_size + __stack_size));
  } >ram AT>ram :ram
}
