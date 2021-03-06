/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Huang Xuxing
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software")
 * to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/* Linker script to configure memory regions. 
 * Need modifying for a specific board. 
 *   FLASH.ORIGIN: starting address of flash
 *   FLASH.LENGTH: length of flash
 *   SRAM.ORIGIN: starting address of SRAM
 *   SRAM.LENGTH: length of SRAM */
MEMORY
{
    FLASH  (rx)   : ORIGIN = 0x00000000, LENGTH = 256K
    SRAM   (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}

/* Linker script to place sections and symbol values. */
ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
	KEEP(*(isr_vector))
	*(.text*)
	KEEP(*(.init))
	KEEP(*(.fini))
	*(.rodata*))
    } > FLASH

    . = ALIGN(4);
    __etext = .;
    PROVIDE(etext = .);

    .data : AT (__etext)
    {
        __data_start__ = .;
	*(vector_table)
	*(.data*)
	. = ALIGN(4);
	__data_end__ = .;
    } > SRAM

    .bss :
    {
        . = ALIGN(4);
        __bss_start__ = .;
	*(.bss*)
	*(COMMON)
	. = ALIGN(4);
	__bss_end__ = .;
    } > SRAM

    .heap :
    {
	. = ALIGN(8);
        __end__ = .;
	PROVIDE(end = .);
	__HeapBase = .;
	. += CONFIG_HEAP_SIZE;
	__HeapLimit = .;
    } > SRAM

    .stack_dummy :
    {
	. = ALIGN(8);
	. += CONFIG_STACK_SIZE;
    } > SRAM

    __StackTop = ORIGIN(SRAM) + LENGTH(SRAM);
    __StackLimit = __StackTop - CONFIG_STACK_SIZE;

    PROVIDE(__stack = __StackTop);

    ASSERT(__StackLimit >= __HeapLimit, "SRAM with overlapping stack and heap");

    __memory_top__ = __HeapBase;
    __memory_limit__ = __HeapLimit;
}
