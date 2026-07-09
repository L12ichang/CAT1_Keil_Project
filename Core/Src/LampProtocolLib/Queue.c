#include "Queue.h"
void createQueue(PQUEUE Q, int maxsize, uint8 *pBuf)
{
    Q->pBase = pBuf;
    Q->front = 0;
    Q->rear = 0;
    Q->maxsize = maxsize;
}

uint8 isQueueFull(PQUEUE Q) 
{
    if (Q->front == (Q->rear + 1) % Q->maxsize)    //判断循环链表是否满，留一个预留空间不用
        return 1;
    else
        return 0;
}

uint8 isQueueEmpty(PQUEUE Q)
{
    if (Q->front == Q->rear)    //判断是否为空
        return 1;
    else
        return 0;
}

uint8 enqueue(PQUEUE Q, uint8 val)
{
    if (isQueueFull(Q))
    {
        return 0;
    } else {
        Q->pBase[Q->rear] = val;
        
        Q->rear = (Q->rear + 1) % Q->maxsize;
        return 1;
    }
}

uint8 dequeue(PQUEUE Q, uint8 *val)
{
    if (isQueueEmpty(Q)) 
    {
        return 0;
    } 
    else 
    { 
        *val = Q->pBase[Q->front];
        Q->front = (Q->front + 1) % Q->maxsize;
        return 1;
    }
}

void flushQueue(PQUEUE Q)
{
    Q->front = 0;
    Q->rear = 0;
}

void deleteQueue(PQUEUE Q) 
{
    if (Q->pBase != 0)
    {
        Q->pBase = 0;
    }

    Q->pBase = 0;
    Q->front = 0;         //初始化参数
    Q->rear = 0;
    Q->maxsize = 0;
}
