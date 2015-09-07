// Copyright (c) 2015 The Connectal Project

// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
import Vector    :: *;
import BRAMCore  :: *;
import BlueCheck :: *;
import FShow     :: *;
import StmtFSM   :: *;
import Clocks    :: *;
import GetPut    :: *;
import FIFO      :: *;
import RegFile   :: *;
import DefaultValue::*;
import Pipe      :: *;
import SharedMemoryFifo::*;
import MemTypes  :: *;
import Portal    :: *;
import MemReadEngine :: *;
import MemWriteEngine :: *;

////////////////////
// Implementation //
////////////////////

interface RegFileMemory;
   method Action write(Bit#(10) addr, Bit#(64) data);
   method Bit#(64) read(Bit#(10) addr);
endinterface

module mkMemory#(MemReadClient#(64) readClient, MemWriteClient#(64) writeClient)(RegFileMemory);
   RegFile#(Bit#(10), Bit#(64)) regFile <- mkRegFileFull();

   Reg#(Bit#(MemOffsetSize)) readAddr <- mkReg(0);
   Reg#(Bit#(BurstLenSize)) readLen   <- mkReg(0);
   Reg#(Bit#(MemTagSize))   readTag   <- mkReg(0);

   Reg#(Bit#(MemOffsetSize)) writeAddr <- mkReg(0);
   Reg#(Bit#(BurstLenSize))  writeLen  <- mkReg(0);
   Reg#(Bit#(MemTagSize))    writeTag  <- mkReg(0);
   FIFO#(Bit#(MemTagSize))   writeDoneFifo <- mkFIFO();
   let verbose = True;

   rule readAddrRule if (readLen == 0 && writeLen == 0);
      let req <- readClient.readReq.get();
      if (verbose) $display("readAddrRule addr=%h", req.offset);
      readAddr <= req.offset;
      readLen  <= req.burstLen;
      readTag  <= req.tag;
   endrule
   rule readDataRule if (readLen > 0);
      if (verbose) $display("readDataRule addr=%h data=%h", readAddr, regFile.sub(truncate(readAddr)));
      readClient.readData.put(MemData { data: regFile.sub(truncate(readAddr)), tag: readTag, last: readLen <= 8 });
      readAddr <= readAddr + 8;
      readLen <= readLen - 8;
   endrule

   (* descending_urgency = "writeAddrRule,readAddrRule" *)
   rule writeAddrRule if (readLen == 0 && writeLen == 0);
      let req <- writeClient.writeReq.get();
      if (verbose) $display("writeAddrRule addr=%h burstLen=%d", req.offset, req.burstLen);
      writeAddr <= req.offset;
      writeLen  <= req.burstLen;
      writeTag  <= req.tag;
   endrule
   rule writeDataRule if (writeLen > 0);
      let md <- writeClient.writeData.get();
      if (verbose) $display("writeDataRule addr=%h data=%h", writeAddr, md.data);
      regFile.upd(truncate(writeAddr), md.data);
      if (writeLen <= 8)
	 writeDoneFifo.enq(writeTag); // NOTE: this rule deadlocks if it calls writeClient.writeDone.put directly
      writeLen <= writeLen - 8;
   endrule
   rule writeDoneRule;
      let tag <- toGet(writeDoneFifo).get();
      writeClient.writeDone.put(tag);
   endrule
   method Action write(Bit#(10) addr, Bit#(64) data);
      if (verbose) $display("mem.write addr=%h data=%h", addr, data);
      regFile.upd(addr, data);
   endmethod
   method Bit#(64) read(Bit#(10) addr);
      return regFile.sub(addr);
   endmethod

endmodule

module mkSharedMemoryFifoImpl(FIFO#(Bit#(32)));
   FIFO#(Bit#(32)) dataFifo <- mkFIFO();

   MemReadEngine#(64,64,4, 2)  readEngine <- mkMemReadEngine();
   MemWriteEngine#(64,64,4, 2) writeEngine <- mkMemWriteEngine();
   let mem <- mkMemory(readEngine.dmaClient, writeEngine.dmaClient);

   Reg#(Bit#(32)) dataReg <- mkReg(0);
   Reg#(Bit#(32)) headPtr <- mkReg(16);
   Reg#(Bit#(32)) tailPtr <- mkReg(16);

   let dut <- mkSharedMemoryRequestPipeOut(readEngine.readServers, writeEngine.writeServers);

   let fsm <- mkAutoFSM(seq
			mem.write(0, { headPtr>>2, 32*8 });
			mem.write(8, { 0, tailPtr>>2 });
			dut.cfg.setSglId(22);
      $display("wrote fifo head/tail ptr");
			while (True) seq
			   dataReg <= dataFifo.first();
			   dataFifo.deq();
			   mem.write(truncate(headPtr+0), {dataReg, 2});
			   headPtr <= headPtr + 8;
			   mem.write(0, { headPtr>>2, 32 });
			   endseq
			endseq
			);

   method enq = dataFifo.enq;
   method first = dut.data.first;
   method deq   = dut.data.deq;
endmodule

/////////////////////////
// Equivalence testing //
/////////////////////////

module [BlueCheck] checkSharedMemoryFifo ();
  /* Specification instance */
   FIFO#(Bit#(32)) spec <- mkSharedMemoryFifoImpl();

   /* Implmentation instance */
   FIFO#(Bit#(32)) imp <- mkSizedFIFO(32);

   Ensure ensure <- getEnsure;

  equiv("first"  , spec.first, imp.first);
  equiv("enq"    , spec.enq,    imp.enq);
  equiv("deq"    , spec.deq,    imp.deq);
endmodule

module [Module] testSharedMemoryFifo ();
  blueCheck(checkSharedMemoryFifo);
endmodule
