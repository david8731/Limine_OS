#ifndef X86_64_GDT_H
#define X86_64_GDT_H

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18 /* this order is what syscall/sysret expects */
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

void gdt_init(void);

#endif
