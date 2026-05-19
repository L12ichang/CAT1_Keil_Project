#ifndef __u32_Q_H__
#define __u32_Q_H__
#include "common.h"

typedef struct 
{
    u32 sn;
    u8 index;
    u32 dat;
}log_type_st;
extern log_type_st log123;

extern void u32_queue_reset(void);
extern boolt u32_queue_empty(void);
extern boolt u32_queue_full(void);
extern boolt u32_queue_in(log_type_st* el);
extern boolt u32_queue_out(log_type_st* el);
extern void log_u32(u8 index, u32 dat);

#endif
