/* Linker script for a Core freestanding kernel. */
ENTRY(_start)
SECTIONS
{
  . = 1M;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
}
