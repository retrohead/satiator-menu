/*  Copyright (c) 2015 James Laird-Wah
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iapetus.h>
#include "satiator.h"
#include <string.h>
#include "fade.h"

// Reset everything we can before loading the menu.
void sysinit(void) {
   int i;
   interrupt_set_level_mask(0xF);
   for (i = 0; i < 0x80; i++)
      bios_set_sh2_interrupt(i, 0);
   for (i = 0x40; i < 0x60; i++)
      bios_set_scu_interrupt(i, 0);

   // Make sure all interrupts have been called
   bios_change_scu_interrupt_mask(0, 0);
   bios_change_scu_interrupt_mask(0xFFFFFFFF, 0xFFFFFFFF);

   vdp_init(RES_320x224);
   per_init();

   // If DSP is running, stop it
   if (dsp_is_exec())
       dsp_stop();

   if (interrupt_get_level_mask() > 0x7)
      interrupt_set_level_mask(0x7);
}

extern char _load_start, _load_end, _bss_end, _free_ram_end;

void start(void) __attribute__((section(".start")));
void start(void) {
    // the BIOS only reads 0x1000 bytes when we begin. read the
    // lot and zero BSS
    int nsec = ((&_load_end-&_load_start-1) / 0x800) + 1;
    bios_get_mpeg_rom(2, nsec, (u32)&_load_start);
    memset(&_load_end, 0, &_free_ram_end - &_load_end);

    // move stack pointer to LoRAM because HiRAM will get clobbered if a load fails
    asm volatile (
        "mov %0, r15"
        : : "r" (&_free_ram_end)
    );

    fadeout(0x10);
    sysinit();

    // enable Satiator API
    s_mode(S_MODE_USBFS);

    main_menu();    // does not return
}
