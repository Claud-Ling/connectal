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

#include <stdio.h>
#include "EchoRequest.h"
#include "EchoIndication.h"
#include "MMUConfigRequest.h"
#include "StdDmaIndication.h"
#include "dmaManager.h"

EchoRequestProxy *sRequestProxy;
static sem_t sem_heard2;

class EchoIndication : public EchoIndicationWrapper
{
public:
    virtual void heard(uint32_t v) {
        fprintf(stderr, "heard an s: %d\n", v);
	sRequestProxy->say2(v, 2*v);
    }
    virtual void heard2(uint32_t a, uint32_t b) {
        sem_post(&sem_heard2);
        //fprintf(stderr, "heard an s2: %d %d\n", a, b);
    }
    EchoIndication(unsigned int id, PortalItemFunctions *item) : EchoIndicationWrapper(id, item) {}
};

static void call_say(int v)
{
    printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, v);
    sRequestProxy->say(v);
    sem_wait(&sem_heard2);
}

static void call_say2(int v, int v2)
{
    sRequestProxy->say2(v, v2);
    sem_wait(&sem_heard2);
}

int allocateShared(DmaManager *dma, MMUConfigRequestProxy *dmap, uint32_t interfaceId, PortalInternal *p, uint32_t size)
{
    int fd = portalAlloc(size);
    p->map_base = (volatile unsigned int *)portalMmap(fd, size);
    p->map_base[SHARED_LIMIT] = size/sizeof(uint32_t);
    p->map_base[SHARED_WRITE] = SHARED_START;
    p->map_base[SHARED_READ] = SHARED_START;
    p->map_base[SHARED_START] = 0;
    unsigned int ref = dma->reference(fd);
    dmap->setInterface(interfaceId, ref);
    return fd;
}

int main(int argc, const char **argv)
{
    int alloc_sz = 64-4;
//1000;

    MMUConfigRequestProxy *dmap = new MMUConfigRequestProxy(IfcNames_MMUConfigRequest, &socketfuncInit);
    DmaManager *dma = new DmaManager(dmap);
    MMUConfigIndication *mIndication = new MMUConfigIndication(dma, IfcNames_MMUConfigIndication, &socketfuncInit);

    portalExec_start();

    EchoIndication *sIndication = new EchoIndication(IfcNames_EchoIndication, &sharedfunc);
    allocateShared(dma, dmap, IfcNames_EchoIndication, &sIndication->pint, alloc_sz);
    sRequestProxy = new EchoRequestProxy(IfcNames_EchoRequest, &sharedfunc);
    allocateShared(dma, dmap, IfcNames_EchoRequest, &sRequestProxy->pint, alloc_sz);

for (int i = 0; i < 100; i++) {
    int v = 42;
    fprintf(stderr, "Saying %d\n", v);
    call_say(v);
    call_say(v*5);
    call_say(v*17);
    call_say(v*93);
    call_say2(v, v*3);
}
    sRequestProxy->setLeds(9);

    return 0;
}
