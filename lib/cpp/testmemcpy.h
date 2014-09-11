#ifndef _TESTMEMCPY_H_
#define _TESTMEMCPY_H_

#include "StdDmaIndication.h"
#include "DmaDebugRequestProxy.h"
#include "MMUConfigRequestProxy.h"
#include "MemcpyIndicationWrapper.h"
#include "MemcpyRequestProxy.h"

sem_t done_sem;
int srcAlloc;
int dstAlloc;
unsigned int *srcBuffer = 0;
unsigned int *dstBuffer = 0;
#ifndef BSIM
int numWords = 16 << 18;
#else
int numWords = 16 << 10;
#endif
size_t alloc_sz = numWords*sizeof(unsigned int);
bool finished = false;
bool memcmp_fail = false;
unsigned int memcmp_count = 0;

void dump(const char *prefix, char *buf, size_t len)
{
    fprintf(stderr, "%s ", prefix);
    for (int i = 0; i < len ; i++) {
	fprintf(stderr, "%02x", (unsigned char)buf[i]);
	if (i % 32 == 31)
	  fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

class MemcpyIndication : public MemcpyIndicationWrapper
{

public:
  MemcpyIndication(unsigned int id) : MemcpyIndicationWrapper(id){}

  virtual void started(){
    fprintf(stderr, "started\n");
  }
  virtual void done() {
    sem_post(&done_sem);
    fprintf(stderr, "done\n");
    finished = true;
    memcmp_fail = memcmp(srcBuffer, dstBuffer, numWords*sizeof(unsigned int));
  }
};


// we can use the data synchronization barrier instead of flushing the 
// cache only because the ps7 is configured to run in buffered-write mode
//
// an opc2 of '4' and CRm of 'c10' encodes "CP15DSB, Data Synchronization Barrier 
// operation". this is a legal instruction to execute in non-privileged mode (mdk)
//
// #define DATA_SYNC_BARRIER   __asm __volatile( "MCR p15, 0, %0, c7, c10, 4" ::  "r" (0) );

int runtest(int argc, const char **argv)
{
  if(sem_init(&done_sem, 1, 0)){
    fprintf(stderr, "failed to init done_sem\n");
    exit(1);
  }

  fprintf(stderr, "%s %s\n", __DATE__, __TIME__);

  MemcpyRequestProxy *device = new MemcpyRequestProxy(IfcNames_MemcpyRequest);
  MemcpyIndication *deviceIndication = new MemcpyIndication(IfcNames_MemcpyIndication);
  DmaDebugRequestProxy *hostDmaDebugRequest = new DmaDebugRequestProxy(IfcNames_HostDmaDebugRequest);
  MMUConfigRequestProxy *dmap = new MMUConfigRequestProxy(IfcNames_HostMMUConfigRequest);
  DmaManager *dma = new DmaManager(hostDmaDebugRequest, dmap);
  DmaDebugIndication *hostDmaDebugIndication = new DmaDebugIndication(dma, IfcNames_HostDmaDebugIndication);
  MMUConfigIndication *hostMMUConfigIndication = new MMUConfigIndication(dma, IfcNames_HostMMUConfigIndication);

  fprintf(stderr, "Main::allocating memory...\n");

  srcAlloc = portalAlloc(alloc_sz);
  dstAlloc = portalAlloc(alloc_sz);

  // for(int i = 0; i < srcAlloc->header.numEntries; i++)
  //   fprintf(stderr, "%lx %lx\n", srcAlloc->entries[i].dma_address, srcAlloc->entries[i].length);
  // for(int i = 0; i < dstAlloc->header.numEntries; i++)
  //   fprintf(stderr, "%lx %lx\n", dstAlloc->entries[i].dma_address, dstAlloc->entries[i].length);

  srcBuffer = (unsigned int *)portalMmap(srcAlloc, alloc_sz);
  dstBuffer = (unsigned int *)portalMmap(dstAlloc, alloc_sz);

  portalExec_start();

  for (int i = 0; i < numWords; i++){
    srcBuffer[i] = i;
    dstBuffer[i] = 0x5a5abeef;
  }

  portalDCacheFlushInval(srcAlloc, alloc_sz, srcBuffer);
  portalDCacheFlushInval(dstAlloc, alloc_sz, dstBuffer);
  fprintf(stderr, "Main::flush and invalidate complete\n");

  unsigned int ref_srcAlloc = dma->reference(srcAlloc);
  unsigned int ref_dstAlloc = dma->reference(dstAlloc);
  
  fprintf(stderr, "ref_srcAlloc=%d\n", ref_srcAlloc);
  fprintf(stderr, "ref_dstAlloc=%d\n", ref_dstAlloc);


  // unsigned int refs[2] = {ref_srcAlloc, ref_dstAlloc};
  // for(int j = 0; j < 2; j++){
  //   unsigned int ref = refs[j];
  //   for(int i = 0; i < numWords; i = i+(numWords/4)){
  //     dmap->addrRequest(ref, i*sizeof(unsigned int));
  //     sleep(1);
  //   }
  //   dmap->addrRequest(ref, (1<<16)*sizeof(unsigned int));
  //   sleep(1);
  // }

  fprintf(stderr, "Main::starting mempcy numWords:%d\n", numWords);
  int burstLen = 16;
#ifndef BSIM
  int iterCnt = 64;
#else
  int iterCnt = 2;
#endif
  portalTimerStart(0);
  device->startCopy(ref_dstAlloc, ref_srcAlloc, numWords, burstLen, iterCnt);
  sem_wait(&done_sem);
  uint64_t cycles = portalTimerLap(0);
  uint64_t read_beats = dma->show_mem_stats(ChannelType_Read);
  uint64_t write_beats = dma->show_mem_stats(ChannelType_Write);
  float read_util = (float)read_beats/(float)cycles;
  float write_util = (float)write_beats/(float)cycles;
  fprintf(stderr, "wr_beats: %"PRIx64"\n", write_beats);
  fprintf(stderr, "rd_beats: %"PRIx64"\n", read_beats);
  fprintf(stderr, "numWords: %x\n", numWords);
  fprintf(stderr, "  wr_est: %"PRIx64"\n", (write_beats*2)/iterCnt);
  fprintf(stderr, "  rd_est: %"PRIx64"\n", (read_beats*2)/iterCnt);
  fprintf(stderr, "memory read utilization (beats/cycle): %f\n", read_util);
  fprintf(stderr, "memory write utilization (beats/cycle): %f\n", write_util);
  
  MonkitFile("perf.monkit")
    .setHwCycles(cycles)
    .setReadBwUtil(read_util)
    .setWriteBwUtil(write_util)
    .writeFile();
}

int runtest_chunk(int argc, const char **argv)
{

  if(sem_init(&done_sem, 1, 0)){
    fprintf(stderr, "failed to init done_sem\n");
    exit(1);
  }

  MemcpyRequestProxy *device = new MemcpyRequestProxy(IfcNames_MemcpyRequest);
  MemcpyIndication *deviceIndication = new MemcpyIndication(IfcNames_MemcpyIndication);
  DmaDebugRequestProxy *hostDmaDebugRequest = new DmaDebugRequestProxy(IfcNames_HostDmaDebugRequest);
  MMUConfigRequestProxy *dmap = new MMUConfigRequestProxy(IfcNames_HostMMUConfigRequest);
  DmaManager *dma = new DmaManager(hostDmaDebugRequest, dmap);
  DmaDebugIndication *hostDmaDebugIndication = new DmaDebugIndication(dma, IfcNames_HostDmaDebugIndication);
  MMUConfigIndication *hostMMUConfigIndication = new MMUConfigIndication(dma, IfcNames_HostMMUConfigIndication);

  portalExec_start();

  fprintf(stderr, "Main::allocating memory...\n");

  fprintf(stderr, "XXX %s %d\n", __FUNCTION__, __LINE__);

  size_t dstBytes = alloc_sz;
  size_t srcBytes = dstBytes;

  fprintf(stderr, "XXX %s %d\n", __FUNCTION__, __LINE__);

  int dstAlloc = portalAlloc(dstBytes);
  int srcAlloc = portalAlloc(srcBytes);

  fprintf(stderr, "XXX %s %d\n", __FUNCTION__, __LINE__);

  int ref_dstAlloc = dma->reference(dstAlloc);
  int ref_srcAlloc = dma->reference(srcAlloc);

  fprintf(stderr, "XXX %s %d\n", __FUNCTION__, __LINE__);

  unsigned int *srcBuffer = (unsigned int *)portalMmap(srcAlloc, srcBytes);
  unsigned long loop = 0;
  sleep(1);
  
  while (loop < dstBytes) {

    fprintf(stderr, "LOOP %s %d\n", __FUNCTION__, __LINE__);

    int nw = srcBytes/sizeof(srcBuffer[0]);
    for (int i = 0; i < nw; i++) {
      srcBuffer[i] = loop/sizeof(srcBuffer[0])+i;
    }
    portalDCacheFlushInval(srcAlloc, srcBytes, srcBuffer);
    device->startCopy(ref_dstAlloc, ref_srcAlloc, nw, 16, 1);
    sem_wait(&done_sem);
    fprintf(stderr, "LOOP %s %d\n", __FUNCTION__, __LINE__);

    loop+=srcBytes;
  }

  fprintf(stderr, "XXX %s %d\n", __FUNCTION__, __LINE__);

}



#endif //_TESTMEMCPY_H_
