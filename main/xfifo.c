/******************************************************************************
	Thread-safe FIFO module

	Data can be put in one thread and read in another without using
	critical sections (same for interrupts).


******************************************************************************/

#include "xfifo.h"
#include <stdlib.h>

static void xFifo_CopyElement(uint8_t *src, uint8_t *dst, uint32_t size)
{
	while(size--)
	{
		*dst++ = *src++;
	}
}

#define xFifo_NextIndex_M(f, index)     ((index == (f->size - 1)) ? 0 : index + 1)
#define xFifo_PrevIndex_M(f, index)     ((index == 0) ? (f->size - 1) : index - 1)
#define xFifo_IncHeadIndex_M(f)         (f->headIndex = xFifo_NextIndex_M(f, f->headIndex))
#define xFifo_DecHeadIndex_M(f)         (f->headIndex = xFifo_PrevIndex_M(f, f->headIndex))
#define xFifo_IncTailIndex_M(f)         (f->tailIndex = xFifo_NextIndex_M(f, f->tailIndex))
#define xFifo_DecTailIndex_M(f)         (f->tailIndex = xFifo_PrevIndex_M(f, f->tailIndex))
#define xFifo_IsFull_M(f)               ((f->countWr - f->countRd) >= f->size)
#define xFifo_IsNotFull_M(f)            ((f->countWr - f->countRd) < f->size)
#define xFifo_IsEmpty_M(f)              (f->countWr == f->countRd)
#define xFifo_IsNotEmpty_M(f)           (f->countWr != f->countRd)


//---------------------------------------------------------------------------//
// Create xFifo
// Storage buffer is allocated dynamically
//
//	Arguments:
//		f - pointer to a xFifo_t structure to fill
//		elementSize - size of an element, bytes
//		fifoSize - length of the xFifo
//	Return:
//		none
//---------------------------------------------------------------------------//
void xFifo_Create(xFifo_t *f, uint32_t elementSize, uint32_t fifoSize)
{
	f->data = (uint8_t *)malloc(elementSize * fifoSize);
    if (!f->data) {}
	f->size = fifoSize;
	f->elementSize = elementSize;
	f->headIndex = 0;
	f->tailIndex = 0;
	f->countRd = 0;
	f->countWr = 0;
}


//---------------------------------------------------------------------------//
// Create xFifo
// External storage buffer is provided
//
//	Arguments:
//		f - pointer to a xFifo_t structure
//		elementSize - size of an element, bytes
//		dataBuffer - byte array with size = elementSize * fifoSize;
//		fifoSize - length of the xFifo
//	Return:
//		none
//---------------------------------------------------------------------------//
void xFifo_CreateStatic(xFifo_t *f, uint32_t elementSize, uint8_t *dataBuffer, uint32_t fifoSize)
{
	f->data = dataBuffer;
	f->size = fifoSize;
	f->elementSize = elementSize;
	f->headIndex = 0;
	f->tailIndex = 0;
	f->countRd = 0;
	f->countWr = 0;
}


//---------------------------------------------------------------------------//
// Put data into xFifo (to the front, normal)
// Data put by this function is last to be read from the FIFO
//
//	Arguments:
//		f - pointer to a xFifo_t structure to fill
//		data - pointer to element(s)
//		count - number of elements to put
//	Return:
//		number of elements actually put into xFifo
//---------------------------------------------------------------------------//
uint32_t xFifo_Put(xFifo_t *f, void *data, uint32_t count)
{
	uint32_t elementsPut = 0;
	uint8_t *inPtr = (uint8_t *)data;
	uint32_t storageIndex;
	while( (elementsPut < count) && (xFifo_IsNotFull_M(f)) )
	{
		storageIndex = f->headIndex * f->elementSize;
		xFifo_CopyElement(inPtr, &f->data[storageIndex], f->elementSize);
		inPtr += f->elementSize;
        xFifo_IncHeadIndex_M(f);
		f->countWr++;
		elementsPut++;
	}
	return elementsPut;
}


//---------------------------------------------------------------------------//
// Put data into xFifo (to the tail, reversed)
// Data put by this function is first to be read from the FIFO
//
//	Arguments:
//		f - pointer to a xFifo_t structure to fill
//		data - pointer to element(s)
//		count - number of elements to put
//	Return:
//		number of elements actually put into xFifo
//---------------------------------------------------------------------------//
uint32_t xFifo_PutToTail(xFifo_t *f, void *data, uint32_t count)
{
	uint32_t elementsPut = 0;
	uint8_t *inPtr = (uint8_t *)data;
	uint32_t storageIndex;
	while( (elementsPut < count) && (xFifo_IsNotFull_M(f)) )
	{
        xFifo_DecTailIndex_M(f);
		storageIndex = f->tailIndex * f->elementSize;
		xFifo_CopyElement(inPtr, &f->data[storageIndex], f->elementSize);
		inPtr += f->elementSize;
		f->countWr++;
		elementsPut++;
	}
	return elementsPut;
}


//---------------------------------------------------------------------------//
// Return pointer to next free data element in the xFifo
// May be used to avoid unnecessary copy
// Note: there should be single place using Insert() functions
//
//	Arguments:
//		f - pointer to a xFifo_t structure to fill
//	Return:
//		pointer to next free element in a FIFO
//---------------------------------------------------------------------------//
void *xFifo_GetInsertPtr(xFifo_t *f)
{
    uint32_t storageIndex;
    if (xFifo_IsNotFull_M(f))
    {
        storageIndex = f->headIndex * f->elementSize;
        return &f->data[storageIndex];
    }
    else
    {
        return 0;
    }
}


//---------------------------------------------------------------------------//
// Move pointers of xFifo by 1 element assuming next data element is already filled
// Note: there should be single place using Insert() functions
//
//	Arguments:
//		f - pointer to a xFifo_t structure to fill
//	Return:
//		number of elements actually put into xFifo
//---------------------------------------------------------------------------//
uint32_t xFifo_AcceptInsert(xFifo_t *f)
{
    uint32_t elementsPut = 0;
    if (xFifo_IsNotFull_M(f))
    {
        xFifo_IncHeadIndex_M(f);
        f->countWr++;
        elementsPut++;
    }
    return elementsPut;
}


//---------------------------------------------------------------------------//
// Get data from xFifo
//
//	Arguments:
//		f - pointer to a xFifo_t structure
//		data - pointer to element(s)
//		count - number of elements to get
//	Return:
//		number of elements actually got from xFifo
//---------------------------------------------------------------------------//
uint32_t xFifo_Get(xFifo_t *f, void *data, uint32_t count)
{
	uint32_t elementsGot = 0;
	uint8_t *outPtr = (uint8_t *)data;
	uint32_t storageIndex;
	while ((elementsGot < count) && (xFifo_IsNotEmpty_M(f)))
	{
		storageIndex = f->tailIndex * f->elementSize;
        if (outPtr)
        {
            xFifo_CopyElement(&f->data[storageIndex], outPtr, f->elementSize);
            outPtr += f->elementSize;
        }
        xFifo_IncTailIndex_M(f);
		f->countRd++;
		elementsGot++;
	}
	return elementsGot;
}


//---------------------------------------------------------------------------//
// Return pointer to next avaliable data element in the xFifo
// May be used to avoid unnecessary copy
// Note: there should be single place using Peek() functions
//
//	Arguments:
//		f - pointer to a xFifo_t structure to peek
//	Return:
//		pointer to next avaliable element in a FIFO
//---------------------------------------------------------------------------//
void *xFifo_GetPeekPtr(xFifo_t *f)
{
    uint32_t storageIndex;
    if (xFifo_IsNotEmpty_M(f))
    {
        storageIndex = f->tailIndex * f->elementSize;
        return &f->data[storageIndex];
    }
    else
    {
        return 0;
    }
}


//---------------------------------------------------------------------------//
// Peek data from xFifo (rd counter is not affected)
// Note: there should be single place using Peek() functions
//
//  Arguments:
//      f - pointer to a xFifo_t structure
//      data - pointer to element(s)
//  Return:
//      number of elements actually got from xFifo
//---------------------------------------------------------------------------//
uint32_t xFifo_Peek(xFifo_t *f, void *data)
{
    uint32_t elementsGot = 0;
    uint8_t *outPtr = (uint8_t *)data;
    uint32_t storageIndex;
    if (xFifo_IsNotEmpty_M(f))
    {
        storageIndex = f->tailIndex * f->elementSize;
        xFifo_CopyElement(&f->data[storageIndex], outPtr, f->elementSize);
        elementsGot++;
    }
    return elementsGot;
}


//---------------------------------------------------------------------------//
// Peek data from xFifo at specified position (rd counter is not affected)
// Note: there should be single place using Peek() functions
//
//  Arguments:
//      f - pointer to a xFifo_t structure
//      data - pointer to element(s)
//      elementIndex - index of element to peek, 0 = tail
//  Return:
//      Zero if element cannot be peeked, 1 otherwise
//---------------------------------------------------------------------------//
uint32_t xFifo_PeekAt(xFifo_t *f, void *data, uint32_t elementIndex)
{
    uint32_t elementsGot = 0;
    uint8_t *outPtr = (uint8_t *)data;
    uint32_t storageIndex;
    uint32_t countRd = f->countRd;
    uint32_t tailIndex = f->tailIndex;
    while (f->countWr != countRd)
    {
        if (elementIndex == elementsGot)
        {
            storageIndex = tailIndex * f->elementSize;
            xFifo_CopyElement(&f->data[storageIndex], outPtr, f->elementSize);
            return 1;
        }
        tailIndex = xFifo_NextIndex_M(f, tailIndex);
		countRd++;
		elementsGot++;
    }
    return 0;
}


//---------------------------------------------------------------------------//
// Accept peek data from xFifo only rd counter is affected
// Note: there should be single place using Peek() functions
//
//  Arguments:
//      f - pointer to a xFifo_t structure
//  Return:
//      none
//---------------------------------------------------------------------------//
void xFifo_AcceptPeek(xFifo_t *f)
{
    if (xFifo_IsNotEmpty_M(f))
    {
        xFifo_IncTailIndex_M(f);
        f->countRd++;
    }
}


//---------------------------------------------------------------------------//
// Clear xFifo
//
//	Arguments:
//		f - pointer to a xFifo_t structure to fill
//	Return:
//		none
//---------------------------------------------------------------------------//
void xFifo_Clear(xFifo_t *f)
{
	f->countRd = f->countWr;
	f->tailIndex = f->headIndex;
}


//---------------------------------------------------------------------------//
// Get number of elements in xFifo
//
//	Arguments:
//		f - pointer to a xFifo_t structure to fill
//	Return:
//		number of avaliable elements in xFifo
//---------------------------------------------------------------------------//
uint32_t xFifo_DataAvaliable(xFifo_t *f)
{
    return f->countWr - f->countRd;
}


//---------------------------------------------------------------------------//
// Check if xFifo is full
//
//	Arguments:
//		f - pointer to a xFifo_t structure to fill
//	Return:
//		number of free elements in xFifo
//---------------------------------------------------------------------------//
uint32_t xFifo_FreeSpace(xFifo_t *f)
{
	return f->size - (f->countWr - f->countRd);
}







