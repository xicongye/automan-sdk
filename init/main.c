#include <printk.h>
#include <driver/console.h>
#include <stdint.h>
#include <config.h>
#include <ee/irqflags.h>
#include <ee/init.h>
#include <io.h>
#include <driver/interrupt.h>

void start_kernel(void)
{
   setup_arch();

   printk("hello kernel!\n");

   for(;;);
}
