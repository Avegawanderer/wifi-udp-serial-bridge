/******************************************************************************
	Thread-safe FIFO module

	Data can be put in one thread and read in another without using
	critical sections (same for interrupts).

	If, however, data is put into the same xFifo from different threads,
	then critical sections are required.

******************************************************************************/
#ifndef __XFIFO_H__
#define __XFIFO_H__

#include <stdint.h>

typedef struct {
	uint8_t *data;
	uint32_t elementSize;
	uint32_t size;
	uint32_t headIndex;
	uint32_t tailIndex;
	uint32_t countWr;			// operations with this type must be atomic!
	uint32_t countRd;
} xFifo_t;


#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

	void xFifo_Create(xFifo_t *f, uint32_t elementSize, uint32_t fifoSize);
	void xFifo_CreateStatic(xFifo_t *f, uint32_t elementSize, uint8_t *dataBuffer, uint32_t fifoSize);
	uint32_t xFifo_Put(xFifo_t *f, void *data, uint32_t count);
    uint32_t xFifo_PutToTail(xFifo_t *f, void *data, uint32_t count);
    void *xFifo_GetInsertPtr(xFifo_t *f);
    uint32_t xFifo_AcceptInsert(xFifo_t *f);
	uint32_t xFifo_Get(xFifo_t *f, void *data, uint32_t count);
    void *xFifo_GetPeekPtr(xFifo_t *f);
	uint32_t xFifo_Peek(xFifo_t *f, void *data);
    void xFifo_AcceptPeek(xFifo_t *f);
    uint32_t xFifo_PeekAt(xFifo_t *f, void *data, uint32_t elementOffset);
	void xFifo_Clear(xFifo_t *f);
	uint32_t xFifo_DataAvaliable(xFifo_t *f);
	uint32_t xFifo_FreeSpace(xFifo_t *f);



#ifdef __cplusplus
}
#endif // __cplusplus

#endif //__XFIFO_H__

