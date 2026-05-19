#include "u32_q.h"

#define QUEUE_ELEMENT_TOTAL     10// 2000
typedef struct 
{
  log_type_st element[QUEUE_ELEMENT_TOTAL]; 
  u8 number;    /* 队列里元素的数量 */  
  u8 front;     /* 指向要删除的地址 */
}queue_t; 
log_type_st log123;

static queue_t _queue;
void u32_queue_reset(void)
{
  _queue.number = 0; 
  _queue.front = QUEUE_ELEMENT_TOTAL-1; 
} 


boolt u32_queue_empty(void)
{
  if (_queue.number == 0)
  {
    return (TRUE);
  } 
  return (FALSE);
} 


boolt u32_queue_full(void)
{
  if (_queue.number == QUEUE_ELEMENT_TOTAL)
  {
    return (TRUE);
  } 
  return (FALSE);
} 

boolt u32_queue_in(log_type_st* el)
{
    if (u32_queue_full()==FALSE)
    {
      ++_queue.number;
      _queue.element[(_queue.front+_queue.number)%QUEUE_ELEMENT_TOTAL] = *el;
    } 
    else
    {
        _queue.front = (_queue.front + 1) % QUEUE_ELEMENT_TOTAL; 
        _queue.element[(_queue.front+_queue.number)%QUEUE_ELEMENT_TOTAL] = *el;
    }
    return (FALSE);  
} 


boolt u32_queue_out(log_type_st* el)
{
  DISABLE_INTERRUPT();
  if (u32_queue_empty()==FALSE)
  {
    --_queue.number;
    _queue.front = (_queue.front + 1) % QUEUE_ELEMENT_TOTAL; 
    *el = _queue.element[_queue.front];
    ENABLE_INTERRUPT();
    return (TRUE);
  } 
  ENABLE_INTERRUPT();
  return (FALSE);
} 
static u32 sn=0;
void log_u32(u8 index, u32 dat)
{
    log_type_st log;
    ++sn;
    log.sn=sn;
    log.index=index;
    log.dat = dat;
    u32_queue_in(&log);
}

