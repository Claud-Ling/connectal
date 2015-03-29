
// Copyright (c) 2014 Quanta Research Cambridge, Inc.

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

import GetPut::*;
import Vector::*;
import StmtFSM::*;
import FIFO::*;
import SpecialFIFOs::*;
import Gearbox::*;

import MemTypes::*;
import Leds::*;
import ConnectalSpi::*;

interface GyroCtrlRequest;
   method Action write_reg_req(Bit#(8) addr, Bit#(8) val);
   method Action read_reg_req(Bit#(8) addr);
   method Action sample(Bit#(32) sgl_id, Bit#(32) alloc_sz, Bit#(32) sample_freq);
   method Action set_en(Bit#(32) en);
endinterface

interface GyroCtrlIndication;
   method Action read_reg_resp(Bit#(8) val);
   method Action write_reg_resp(Bit#(8) addr);
   method Action memwrite_status(Bit#(32) addr, Bit#(32) wrap_cnt);
endinterface

interface GyroSampleStream;
   method Action sample(Int#(16) x, Int#(16) y, Int#(16) z);
endinterface

interface GyroController;
   interface GyroCtrlRequest req;
   interface SpiPins spi;
   interface LEDS leds;
   interface MemWriteClient#(64) dmaClient;
endinterface

module mkGyroController#(GyroCtrlIndication ind)(GyroController);

   Reg#(Bit#(32))  en_memwr   <- mkReg(maxBound);
   SPI#(Bit#(16))  spiCtrl    <- mkSPI(1000, True);
   FIFO#(Bool)     spi_aux    <- mkPipelineFIFO;
   Reg#(Bit#(32))  sampleFreq <- mkReg(0);
   Reg#(Bit#(32))  sampleCnt  <- mkReg(0);
   Reg#(Bit#(32))  wrapCnt    <- mkReg(0);
   Reg#(Bit#(32))  sglId      <- mkReg(0);
   Reg#(Bit#(32))  allocSz    <- mkReg(0);
   Reg#(Bit#(32))  writePtr   <- mkReg(0);
   FIFO#(Bool)     rc_fifo    <- mkSizedFIFO(1);
   FIFO#(Bit#(8))  wr_queue   <- mkSizedFIFO(4);
   Reg#(Bit#(8))   wr_reg     <- mkReg(0);
`ifdef BSIM
   Reg#(Bit#(8))   bsim_cnt   <- mkReg(0);
`endif
   let clk <- exposeCurrentClock;
   let rst <- exposeCurrentReset;
   Gearbox#(1,8,Bit#(8)) gb   <- mk1toNGearbox(clk,rst,clk,rst);
   
   let out_X_L = 'h28;
   let out_X_H = 'h29;
   let out_Y_L = 'h2A;
   let out_Y_H = 'h2B;
   let out_Z_L = 'h2C;
   let out_Z_H = 'h2D;
   let verbose = False;
   
   rule read_reg_resp;
      let rv <- spiCtrl.response.get;
      if (rc_fifo.first)
	 ind.read_reg_resp(truncate(rv));
      rc_fifo.deq;
   endrule
      
   rule sample_req(sampleFreq > 0);
      let new_sampleCnt = sampleCnt+1; 
      let sampling = True;
      if (new_sampleCnt == sampleFreq-5) begin
	 spiCtrl.request.put({1'b1,1'b0,out_X_L,8'h00});
	 if(verbose) $display("sample_x_l");
      end
      else if (new_sampleCnt == sampleFreq-4) begin
	 spiCtrl.request.put({1'b1,1'b0,out_X_H,8'h00});
	 if(verbose) $display("sample_x_h");
      end
      else if (new_sampleCnt == sampleFreq-3) begin
	 spiCtrl.request.put({1'b1,1'b0,out_Y_L,8'h00});
	 if(verbose) $display("sample_y_l");
      end
      else if (new_sampleCnt == sampleFreq-2) begin
	 spiCtrl.request.put({1'b1,1'b0,out_Y_H,8'h00});
	 if(verbose) $display("sample_y_h");
      end
      else if (new_sampleCnt == sampleFreq-1) begin
	 spiCtrl.request.put({1'b1,1'b0,out_Z_L,8'h00});
	 if(verbose) $display("sample_z_l");
      end
      else if (new_sampleCnt >= sampleFreq-0) begin
	 spiCtrl.request.put({1'b1,1'b0,out_Z_H,8'h00});
	 if(verbose) $display("sample_z_h");
	 new_sampleCnt = 0;
      end
      else begin
	 sampling = False;
      end
      if(sampling) spi_aux.enq(True);
      sampleCnt <= new_sampleCnt;
   endrule
   
   rule sample_resp(sampleFreq > 0);
      spi_aux.deq;
      if(verbose) $display("sample_resp");
      let rv <- spiCtrl.response.get;
`ifdef BSIM
      bsim_cnt <= (bsim_cnt == 5) ? 0 : bsim_cnt+1;
      let bsim_val = bsim_cnt[0]==1'b0 ? bsim_cnt+1 : 0;
      gb.enq(cons(bsim_val,nil));
`else
      gb.enq(cons(truncate(rv),nil));
`endif
   endrule
      
   interface GyroCtrlRequest req;
      method Action write_reg_req(Bit#(8) addr, Bit#(8) val);
	 spiCtrl.request.put({1'b0,1'b0,addr[5:0],val});
	 ind.write_reg_resp(addr);
	 rc_fifo.enq(False);
      endmethod
      method Action read_reg_req(Bit#(8) addr);
	 spiCtrl.request.put({1'b1,1'b0,addr[5:0],8'h00});
	 rc_fifo.enq(True);
      endmethod
      method Action sample(Bit#(32) sgl_id, Bit#(32) alloc_sz, Bit#(32) sample_freq);
	 $display("sample %d %d %d", sgl_id, alloc_sz, sample_freq);
	 sampleFreq <= sample_freq;
	 sampleCnt <= 0;
	 allocSz <= alloc_sz;
	 sglId <= sgl_id;
      endmethod
      method Action set_en(Bit#(32) en);
	 en_memwr <= en;
	 if(en == 0) ind.memwrite_status(writePtr, wrapCnt);
      endmethod
   endinterface
   
   interface LEDS leds;
      method Bit#(LedsWidth) leds() = truncate(pack(wrapCnt));
   endinterface

   interface SpiPins spi = spiCtrl.pins;

   interface MemWriteClient dmaClient;
      interface Get writeReq;
	 method ActionValue#(MemRequest) get if (allocSz > 0 && en_memwr > 0);
	    Bit#(8) bl = 128;
	    Bit#(32) new_writePtr = writePtr + extend(bl);
	    if (new_writePtr >= allocSz) begin
	       new_writePtr = 0;
	       bl =  truncate(allocSz-writePtr);
	       wrapCnt <= wrapCnt+1;
	       en_memwr <= en_memwr-1;
	    end
	    writePtr <= new_writePtr;
	    if (verbose) $display("writeReq %d", writePtr);
	    wr_queue.enq(bl);
	    return MemRequest {sglId:sglId, offset:extend(writePtr), burstLen:bl, tag:0};
	 endmethod
      endinterface
      interface Get writeData;
	 method ActionValue#(MemData#(64)) get;
	    gb.deq;
	    let new_wr_reg = wr_reg-8;
	    if (wr_reg == 0) begin
	       let wrv <- toGet(wr_queue).get;
	       new_wr_reg = wrv-8;
	    end
	    wr_reg <= new_wr_reg;
	    let rv = pack(gb.first);
	    if(verbose) $display("writeData %h", rv);
	    return MemData{data:rv, tag:0, last:(new_wr_reg==0)};
	 endmethod
      endinterface
      interface Put writeDone;
	 method Action put(Bit#(MemTagSize) tag);
	    if(verbose) $display("writeDone");
	 endmethod
      endinterface
   endinterface
endmodule
