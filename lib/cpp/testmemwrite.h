

#ifndef _TESTMEMWRITE_H_
#define _TESTMEMWRITE_H_

#include "sock_utils.h"
#include "StdDmaIndication.h"
#include "DmaDebugRequestProxy.h"
#include "SGListConfigRequestProxy.h"
#include "MemwriteIndicationWrapper.h"
#include "MemwriteRequestProxy.h"
#include "dmaManager.h"


sem_t test_sem;
#ifndef BSIM
int numWords = 0x1240000/4; // make sure to allocate at least one entry of each size
#else
int numWords = 0x124000/4;
#endif
size_t test_sz  = numWords*sizeof(unsigned int);
size_t alloc_sz = test_sz;

int burstLen = 16;
#ifndef BSIM
int iterCnt = 128;
#else
int iterCnt = 2;
#endif


class MemwriteIndication : public MemwriteIndicationWrapper
{
public:
  MemwriteIndication(int id) : MemwriteIndicationWrapper(id){}

  virtual void started(uint32_t words){
    fprintf(stderr, "Memwrite::started: words=%x\n", words);
  }
  virtual void writeDone ( uint32_t srcGen ){
    fprintf(stderr, "Memwrite::writeDone (%08x)\n", srcGen);
    sem_post(&test_sem);
  }
  virtual void reportStateDbg(uint32_t streamWrCnt, uint32_t srcGen){
    fprintf(stderr, "Memwrite::reportStateDbg: streamWrCnt=%08x srcGen=%d\n", streamWrCnt, srcGen);
  }  

};

MemwriteRequestProxy *device = 0;
SGListConfigRequestProxy *dmap = 0;

MemwriteIndication *deviceIndication = 0;

int dstAlloc;
unsigned int *dstBuffer = 0;

void child(int rd_sock)
{
  int fd;
  bool mismatch = false;
  fprintf(stderr, "[%s:%d] child waiting for fd\n", __FUNCTION__, __LINE__);
  sock_fd_read(rd_sock, &fd);
  fprintf(stderr, "[%s:%d] child got fd %d\n", __FUNCTION__, __LINE__, fd);

  unsigned int *dstBuffer = (unsigned int *)portalMmap(fd, alloc_sz);
  fprintf(stderr, "child::dstBuffer = %p\n", dstBuffer);

  unsigned int sg = 0;
  for (int i = 0; i < numWords; i++){
    mismatch |= (dstBuffer[i] != sg++);
    //fprintf(stderr, "%08x, %08x\n", dstBuffer[i], sg-1);
  }
  fprintf(stderr, "child::writeDone mismatch=%d\n", mismatch);
  munmap(dstBuffer, alloc_sz);
  close(fd);
  exit(mismatch);
}

void parent(int rd_sock, int wr_sock)
{
  
  if(sem_init(&test_sem, 1, 0)){
    fprintf(stderr, "error: failed to init test_sem\n");
    exit(1);
  }

  fprintf(stderr, "parent::%s %s\n", __DATE__, __TIME__);

  device = new MemwriteRequestProxy(IfcNames_MemwriteRequest);
  deviceIndication = new MemwriteIndication(IfcNames_MemwriteIndication);
  DmaDebugRequestProxy *hostmemDmaDebugRequest = new DmaDebugRequestProxy(IfcNames_HostmemDmaDebugRequest);
  SGListConfigRequestProxy *dmap = new SGListConfigRequestProxy(IfcNames_HostmemSGListConfigRequest);
  DmaManager *dma = new DmaManager(hostmemDmaDebugRequest, dmap);
  DmaDebugIndication *hostmemDmaDebugIndication = new DmaDebugIndication(dma, IfcNames_HostmemDmaDebugIndication);
  SGListConfigIndication *hostmemSGListConfigIndication = new SGListConfigIndication(dma, IfcNames_HostmemSGListConfigIndication);
  
  fprintf(stderr, "parent::allocating memory...\n");
  dstAlloc = portalAlloc(alloc_sz);
  dstBuffer = (unsigned int *)portalMmap(dstAlloc, alloc_sz);
  
  portalExec_start();
  
  unsigned int ref_dstAlloc = dma->reference(dstAlloc);
  
  for (int i = 0; i < numWords; i++){
    dstBuffer[i] = 0xDEADBEEF;
  }
  
  portalDCacheFlushInval(dstAlloc, alloc_sz, dstBuffer);
  fprintf(stderr, "parent::flush and invalidate complete\n");

  // for(int i = 0; i < dstAlloc->header.numEntries; i++)
  //   fprintf(stderr, "%lx %lx\n", dstAlloc->entries[i].dma_address, dstAlloc->entries[i].length);

  // sleep(1);
  // dmap->addrRequest(ref_dstAlloc, 2*sizeof(unsigned int));
  // sleep(1);

  bool orig_test = true;

  if (orig_test){
    fprintf(stderr, "parent::starting write %08x\n", numWords);
    portalTimerStart(0);
    //portalTrace_start();
    device->startWrite(ref_dstAlloc, 0, numWords, burstLen, iterCnt);
    sem_wait(&test_sem);
    //portalTrace_stop();
    uint64_t cycles = portalTimerLap(0);
    uint64_t beats = dma->show_mem_stats(ChannelType_Write);
    float write_util = (float)beats/(float)cycles;
    fprintf(stderr, "   beats: %"PRIx64"\n", beats);
    fprintf(stderr, "numWords: %x\n", numWords);
    fprintf(stderr, "     est: %"PRIx64"\n", (beats*2)/iterCnt);
    fprintf(stderr, "memory write utilization (beats/cycle): %f\n", write_util);
    
    MonkitFile("perf.monkit")
      .setHwCycles(cycles)
      .setWriteBwUtil(write_util)
      .writeFile();

  } else {
    fprintf(stderr, "parent::new_test read %08x\n", numWords);
    int chunk = numWords >> 4;
    for(int i = 0; i < numWords; i+=chunk){
      device->startWrite(ref_dstAlloc, i, chunk, burstLen, 1);
      sem_wait(&test_sem);
    }
  }

  fprintf(stderr, "[%s:%d] send fd to child %d\n", __FUNCTION__, __LINE__, (int)dstAlloc);
  sock_fd_write(wr_sock, (int)dstAlloc);
  munmap(dstBuffer, alloc_sz);
  close(dstAlloc);
}

#endif // _TESTMEMWRITE_H_
