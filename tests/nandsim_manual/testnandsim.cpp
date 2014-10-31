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

#include "StdDmaIndication.h"
#include "DmaDebugRequest.h"
#include "MMUConfigRequest.h"
#include "NandSimIndication.h"
#include "NandSimRequest.h"

int srcAlloc, nandAlloc;
unsigned int *srcBuffer = 0;
size_t numBytes = 1 << 12;
size_t nandBytes = 1 << 24;

class NandSimIndication : public NandSimIndicationWrapper
{
public:
  unsigned int rDataCnt;
  virtual void readDone(uint32_t v){
    fprintf(stderr, "NandSim::readDone v=%x\n", v);
    sem_post(&sem);
  }
  virtual void writeDone(uint32_t v){
    fprintf(stderr, "NandSim::writeDone v=%x\n", v);
    sem_post(&sem);
  }
  virtual void eraseDone(uint32_t v){
    fprintf(stderr, "NandSim::eraseDone v=%x\n", v);
    sem_post(&sem);
  }
  virtual void configureNandDone(){
    fprintf(stderr, "NandSim::configureNandDone\n");
    sem_post(&sem);
  }

  NandSimIndication(int id) : NandSimIndicationWrapper(id) {
    sem_init(&sem, 0, 0);
  }
  void wait() {
    fprintf(stderr, "NandSim::wait for semaphore\n");
    sem_wait(&sem);
  }
private:
  sem_t sem;
};

int main(int argc, const char **argv)
{
  unsigned int srcGen = 0;
  NandSimRequestProxy *device = 0;
  NandSimIndication *deviceIndication = 0;

  fprintf(stderr, "Main::%s %s\n", __DATE__, __TIME__);

  device = new NandSimRequestProxy(IfcNames_NandSimRequest);
  deviceIndication = new NandSimIndication(IfcNames_NandSimIndication);
  DmaDebugRequestProxy *hostDmaDebugRequest = new DmaDebugRequestProxy(IfcNames_HostDmaDebugRequest);
  MMUConfigRequestProxy *dmap = new MMUConfigRequestProxy(IfcNames_HostMMUConfigRequest);
  DmaManager *dma = new DmaManager(hostDmaDebugRequest, dmap);
  DmaDebugIndication *hostDmaDebugIndication = new DmaDebugIndication(dma, IfcNames_HostDmaDebugIndication);
  MMUConfigIndication *hostMMUConfigIndication = new MMUConfigIndication(dma, IfcNames_HostMMUConfigIndication);

  fprintf(stderr, "Main::allocating memory...\n");

  srcAlloc = portalAlloc(numBytes);
  srcBuffer = (unsigned int *)portalMmap(srcAlloc, numBytes);
  fprintf(stderr, "fd=%d, srcBuffer=%p\n", srcAlloc, srcBuffer);

  portalExec_start();

  for (int i = 0; i < numBytes/sizeof(srcBuffer[0]); i++)
    srcBuffer[i] = srcGen++;
    
  portalDCacheFlushInval(srcAlloc, numBytes, srcBuffer);
  fprintf(stderr, "Main::flush and invalidate complete\n");
  sleep(1);

  unsigned int ref_srcAlloc = dma->reference(srcAlloc);

  nandAlloc = portalAlloc(nandBytes);
  int ref_nandAlloc = dma->reference(nandAlloc);
  fprintf(stderr, "NAND alloc fd=%d ref=%d\n", nandAlloc, ref_nandAlloc);
  device->configureNand(ref_nandAlloc, nandBytes);
  deviceIndication->wait();

  fprintf(stderr, "Main::starting write ref=%d, len=%08zx\n", ref_srcAlloc, numBytes);
  device->startWrite(ref_srcAlloc, 0, 0, numBytes, 16);
  deviceIndication->wait();

  fprintf(stderr, "Main::starting read %08zx\n", numBytes);
  device->startRead(ref_srcAlloc, 0, 0, numBytes, 16);
  deviceIndication->wait();

  fprintf(stderr, "Main::starting erase %08zx\n", numBytes);
  device->startErase(0, numBytes);
  deviceIndication->wait();

  fprintf(stderr, "Main::starting read %08zx\n", numBytes);
  device->startRead(ref_srcAlloc, 0, 0, numBytes, 16);
  deviceIndication->wait();
  return 0;
}
