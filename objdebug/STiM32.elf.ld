
/*
!!! Primary linker script file generated automatically !!!
This file contains the input and output files names
It also includes the secondary/main linker script that is specified in the Ride options (default or custom)
*/

SEARCH_DIR(".")
SEARCH_DIR("C:\Users\Igor\Documents\GitHub\STiM32\objdebug")
SEARCH_DIR("C:\Raisonance\Ride\Lib\ARM")
STARTUP("C:\Raisonance\Ride\Lib\ARM\CircleOS\Application_Startup.o")
INPUT("C:\Users\Igor\Documents\GitHub\STiM32\objdebug\STiM32.o")
GROUP(
"std_sbrk_thumb.lib"
"smallprintf_thumb.a"
"libc.a"
"libm.a"

)
INCLUDE "C:\Users\Igor\Documents\GitHub\STiM32\Circle_App_OP4.ld"
PROVIDE ( _Minimum_Stack_Size = 0x20 );
OUTPUT("C:\Users\Igor\Documents\GitHub\STiM32\objdebug\STiM32.elf")
