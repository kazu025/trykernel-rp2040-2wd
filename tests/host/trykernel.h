#ifndef HOST_TRYKERNEL_H
#define HOST_TRYKERNEL_H
#include "typedef.h"
#include "config.h"
#include "error.h"
#include "apidef.h"
#define TIMER_PERIOD 10
extern UINT host_primask;
extern BOOL host_interrupt;
#define DI(s) ((s) = host_primask, host_primask = 1)
#define EI(s) (host_primask = (s))
static inline BOOL is_interrupt_context(void) { return host_interrupt; }
static inline void out_w(UW addr, UW value) { (void)addr; (void)value; }
#endif
