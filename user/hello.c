__attribute__((noinline)) void _start(void){
 for(;;){ __asm__ volatile("pause"); }
}
