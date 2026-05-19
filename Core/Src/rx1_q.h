#ifndef __RX1_Q_H__
#define __RX1_Q_H__
#include "common.h"



extern void rx1_queue_reset(void);
extern boolt rx1_queue_empty(void);
extern boolt rx1_queue_full(void);
extern boolt rx1_queue_in(u8 el);
extern boolt rx1_queue_out(u8* el);
extern int rx1_queue_size(void);

#endif
