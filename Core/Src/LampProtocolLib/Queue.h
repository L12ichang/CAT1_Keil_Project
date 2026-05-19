#ifndef QUEUE_H_
#define QUEUE_H_
#include "common.h"
typedef struct queue 
{
	uint8 *pBase;
	uint16 front;    //指向队列第一个元素
	uint16 rear;    //指向队列最后一个元素的下一个元素
	uint16 maxsize; //循环队列的最大存储空间
}QUEUE,*PQUEUE;

void createQueue(PQUEUE Q,int maxsize, uint8 *pBuf);
uint8 isQueueFull(PQUEUE Q);
uint8 isQueueEmpty(PQUEUE Q);
uint8 enqueue(PQUEUE Q, uint8 val);
uint8 dequeue(PQUEUE Q, uint8 *val);
void flushQueue(PQUEUE Q);
void deleteQueue(PQUEUE Q);
#endif
