ENTRY(Reset_Handler)

MEMORY
{
  /* C8T6 的公开下限是 64 KiB Flash / 20 KiB SRAM；最后 1 KiB 留给 session journal。 */
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 63K
  SESSION (r)     : ORIGIN = 0x0800FC00, LENGTH = 1K
  RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text*)
    *(.rodata*)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  /DISCARD/ :
  {
    *(.comment*)
    *(.note*)
    *(.eh_frame*)
  }

  ASSERT(_etext <= ORIGIN(FLASH) + LENGTH(FLASH), "firmware overlaps session journal")
  ASSERT(_ebss <= ORIGIN(RAM) + LENGTH(RAM), "firmware exceeds 20 KiB SRAM")
}

