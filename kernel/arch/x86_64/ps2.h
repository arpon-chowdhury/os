#ifndef ON_X86_PS2_H
#define ON_X86_PS2_H
/* Both IRQ lines drain the shared output buffer, routing by AUX status. */
void x86_ps2_drain(void);
#endif
