/* Copyright (c) 2014 Quanta Research Cambridge, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __PORTALALLOC_H__
#define __PORTALALLOC_H__

typedef struct PortalAllocHeader {
    size_t size;
    int fd;
    int numEntries;
} PortalAllocHeader;

typedef struct DmaEntry {
    long dma_address;
    unsigned int length; // to match length field in scatterlist.h
} DmaEntry;

typedef struct PortalAlloc {
    PortalAllocHeader header;
    DmaEntry entries[0];
} PortalAlloc;

typedef struct PortalElementSize {
    int fd;
    int index;
} PortalElementSize;

#define PA_ALLOC              _IOWR('B', 10, PortalAlloc)
#define PA_DCACHE_FLUSH_INVAL _IOWR('B', 11, PortalAlloc)
#define PA_DMA_ADDRESSES      _IOWR('B', 13, PortalAlloc)
#define PA_MALLOC              _IOR('B', 14, unsigned long)
#define PA_ELEMENT_SIZE        _IOR('B', 15, PortalElementSize)

/**
 * struct pa_buffer - metadata for a particular buffer
 * @size:              size of the buffer
 * @lock:		protects the buffers cnt fields
 * @kmap_cnt:		number of times the buffer is mapped to the kernel
 * @vaddr:		the kenrel mapping if kmap_cnt is not zero
 * @sg_table:		the sg table for the buffer
 */
#ifdef __KERNEL__
struct pa_buffer {
    size_t          size;
    struct mutex    lock;
    int             kmap_cnt;
    void            *vaddr;
    struct sg_table *sg_table;
};
int portalmem_dmabuffer_create(unsigned long len);
#endif

#endif /* __PORTALALLOC_H__ */
