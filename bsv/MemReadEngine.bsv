// Copyright (c) 2013 Quanta Research Cambridge, Inc.

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
import Vector::*;
import FIFOF::*;
import FIFO::*;
import GetPut::*;
import Assert::*;
import ClientServer::*;
import BRAM::*;
import BRAMFIFO::*;
import ConfigCounter::*;
import Connectable::*;
import ConnectalMemory::*;
import MemTypes::*;
import Pipe::*;
import MemUtils::*;

module mkMemReadEngine(MemReadEngine#(busWidth, userWidth, cmdQDepth, numServers))
   provisos( Mul#(TDiv#(busWidth, 8), 8, busWidth)
	    ,Add#(1, a__, numServers)
	    ,Add#(busWidth, 0, userWidth)
	    ,Add#(b__, TLog#(numServers), TAdd#(1, TLog#(TMul#(cmdQDepth,numServers))))
	    ,Add#(c__, TLog#(numServers), TLog#(TMul#(cmdQDepth, numServers)))
	    ,Add#(d__, TLog#(numServers), 6)
	    );
   let rv <- mkMemReadEngineBuff(valueOf(cmdQDepth) * valueOf(TExp#(BurstLenSize)));
   return rv;
endmodule

typedef struct {
    Bit#(BurstLenSize) len;
    Bool               last;
} NextReq deriving (Bits, Eq);

function NextReq getNext(Bit#(32) len, Bit#(BurstLenSize) burst);
   NextReq v;
   v.last = (len <= extend(burst));
   v.len = v.last ? truncate(len) : burst;
   return v;
endfunction

module mkMemReadEngineBuff#(Integer bufferSizeBytes) (MemReadEngine#(busWidth, userWidth, cmdQDepth, numServers))
   provisos (Div#(busWidth,8,busWidthBytes),
	     Mul#(busWidthBytes,8,busWidth),
	     Log#(busWidthBytes,beatShift),
	     Log#(cmdQDepth,logCmdQDepth),
	     Mul#(cmdQDepth,numServers,cmdBuffSz),
	     Log#(cmdBuffSz, cmdBuffAddrSz),
	     Log#(numServers, serverIdxSz),
	     Add#(busWidth, 0, userWidth),
	     Add#(1,logCmdQDepth, outCntSz),
	     Add#(1, c__, numServers),
	     Add#(b__, TLog#(numServers), cmdBuffAddrSz),
	     Add#(e__, TLog#(numServers), TAdd#(1, cmdBuffAddrSz)),
	     Add#(a__, serverIdxSz, cmdBuffAddrSz),
	     Min#(2,TLog#(numServers),bpc),
	     Add#(d__, TLog#(numServers), TAdd#(1, serverIdxSz)),
	     Add#(f__, serverIdxSz, 6));
   let verbose = False;
   let beatShift = fromInteger(valueOf(beatShift));

   Integer bufferSizeBeats = bufferSizeBytes/valueOf(busWidthBytes);
   Vector#(numServers, Reg#(Bool))          clientInFlight <- replicateM(mkReg(False));
   Vector#(numServers, ConfigCounter#(16))  clientAvail <- replicateM(mkConfigCounter(fromInteger(bufferSizeBeats)));
   Vector#(numServers, Reg#(MemengineCmd))  clientCommand <- replicateM(mkReg(unpack(0)));
   Vector#(numServers, FIFOF#(MemDataF#(userWidth))) clientDataFifo <- replicateM(mkSizedBRAMFIFOF(bufferSizeBeats));
   Vector#(numServers, FIFO#(MemengineCmd)) clientRequest <- replicateM(mkFIFO());
   Vector#(numServers, Reg#(Bit#(32)))      clientLen <- replicateM(mkReg(unpack(0)));
   Vector#(numServers, Reg#(Bit#(32)))      clientBase <- replicateM(mkReg(unpack(0)));
   Vector#(numServers, Reg#(NextReq))       clientNext <- replicateM(mkReg(unpack(0)));
   
   FIFO#(Tuple4#(Bit#(serverIdxSz),Bool,Bool,Bit#(BurstLenSize)))              serverCheckAvail <- mkSizedFIFO(1);
   FIFO#(Tuple6#(Bit#(serverIdxSz),SGLId,Bit#(32),Bit#(MemTagSize),Bool,Bit#(BurstLenSize))) serverRequest <- mkSizedFIFO(valueOf(cmdQDepth));
   FIFO#(Tuple3#(Bit#(8),Bit#(serverIdxSz),Bool)) serverProcessing <- mkSizedFIFO(valueOf(cmdQDepth));
   FIFOF#(MemData#(busWidth))                       serverDataFifo <- mkFIFOF;

   Reg#(Bit#(8))                    respCnt <- mkReg(0);
   Reg#(Bit#(32)) counter <- mkReg(0);

   rule incCounter;
      counter <= counter + 1;
   endrule
         
   for (Integer idx = 0; idx < valueOf(numServers); idx = idx + 1)
      rule rule_startNew if (!clientInFlight[idx]);
	 let cmd <- toGet(clientRequest[idx]).get();
	 clientInFlight[idx] <= True;
	 clientCommand[idx] <= cmd;
         clientLen[idx] <= cmd.len - extend(cmd.burstLen);
         clientBase[idx] <= cmd.base;
         clientNext[idx] <= getNext(cmd.len, cmd.burstLen);
	 if (verbose) $display("mkMemReadEngineBuff::%d rule_startNew %d %d", counter, idx, clientAvail[idx].read);
      endrule

   Reg#(Bit#(TAdd#(1,serverIdxSz))) loadIdx <- mkReg(0);
   rule rule_checkAvail;
      if (clientInFlight[loadIdx]) begin
         let cmd_len = clientNext[loadIdx].len;
         let last_burst = clientNext[loadIdx].last;
	 let cond0 <- clientAvail[loadIdx].maybeDecrement(unpack(extend(cmd_len>>beatShift)));
	 serverCheckAvail.enq(tuple4(truncate(loadIdx),cond0,last_burst,cmd_len));
	 if (verbose) $display("mkMemReadEngineBuff::%d rule_checkAvail avail[%d] %d burstLen %d cond0 %d last_burst %d", counter, loadIdx, clientAvail[loadIdx].read(), cmd_len>>beatShift, cond0, last_burst);
      end
      if(loadIdx+1 >= fromInteger(valueOf(numServers)))
         loadIdx <= 0;
      else
         loadIdx <= loadIdx+1;
   endrule

   // should use an EHR for clientInFlight to avoid the need for this pragma
   (* descending_urgency = "rule_requestServer, rule_startNew" *)
   rule rule_requestServer;
      match {.idx, .cond0,.last_burst,.cmd_len} <- toGet(serverCheckAvail).get;
      if  (cond0) begin
	 if (verbose) $display("mkMemReadEngineBuff::%d rule_requestServer clientLen %d idx %d cond0 %d last_burst %d", counter, clientLen[idx], idx, cond0, last_burst);
	 serverRequest.enq(tuple6(idx,clientCommand[idx].sglId,clientBase[idx],clientCommand[idx].tag,last_burst,cmd_len));
         clientBase[idx] <= clientBase[idx] + extend(cmd_len);
         clientLen[idx] <= clientLen[idx] - extend(cmd_len);
         clientNext[idx] <= getNext(clientLen[idx], clientCommand[idx].burstLen);
	 if (last_burst) begin
	    if (verbose) $display("mkMemReadEngineBuff::%d rule_requestServer last_burst %d", counter, last_burst);
	    clientInFlight[idx] <= False;
	 end
      end
   endrule
   
   rule read_data_rule;
      let d <- toGet(serverDataFifo).get();
      match {.rc, .idx, .last_burst} = serverProcessing.first;
      let new_respCnt = respCnt+1;
      let l = False;
      if (verbose) $display("mkMemReadEngineBuff::%d data %h new_respCnt %d rc %d last_burst %d idx %d clientInFlight %d eob %d", counter, d.data, new_respCnt, rc, last_burst, idx, clientInFlight[idx], d.last);
      if (new_respCnt == rc) begin
	 respCnt <= 0;
	 serverProcessing.deq;
	 //$display("eob %d", idx);
	 l = last_burst;
      end
      else begin
	 respCnt <= new_respCnt;
      end
      clientDataFifo[idx].enq(MemDataF { data: d.data, tag: d.tag, first: (respCnt == 0), last: l});
   endrule

   Vector#(numServers, MemReadEngineServer#(userWidth)) rs;
   for(Integer i = 0; i < valueOf(numServers); i=i+1)
      rs[i] = (interface MemReadEngineServer#(userWidth);
		  interface Put request;
		     method Action put(MemengineCmd cmd);
			Bit#(32) bsb = fromInteger(bufferSizeBytes);
`ifdef SIMULATION
			Bit#(32) dw = fromInteger(valueOf(busWidthBytes));
			let mdw = ((cmd.len)/dw)*dw != cmd.len;
			let bbl = extend(cmd.burstLen) > bsb;
			if(bbl || mdw) begin
			   if (bbl)
			      $display("XXXXXXXXXX mkMemReadEngineBuff::unsupported burstLen %d %d", bsb, cmd.burstLen);
			   if (mdw)
			      $display("XXXXXXXXXX mkMemReadEngineBuff::unsupported len %d", cmd.len);
			end
			else
`endif
                           begin
			   clientRequest[i].enq(cmd);
			   end
 		     endmethod
		  endinterface
                  interface data = interface PipeOut;
	             method MemDataF#(userWidth) first;
	                return clientDataFifo[i].first;
	             endmethod
	             method Action deq;
	                if (verbose) $display("mkMemReadEngineBuff::check_out: idx %d data %h clientAvail %d eob %d", i, clientDataFifo[i].first.data, clientAvail[i].read(), clientDataFifo[i].first.last);
	                clientDataFifo[i].deq;
	                clientAvail[i].increment(1);
	             endmethod
	             method Bool notEmpty = clientDataFifo[i].notEmpty;
                  endinterface;
               endinterface);
   interface readServers = rs;
   interface MemReadClient dmaClient;
      interface Get readReq;
	 method ActionValue#(MemRequest) get();
	    match {.idx, .cmd_sglId, .cmd_base, .cmd_tag, .last_burst, .bl} <- toGet(serverRequest).get;
	    serverProcessing.enq(tuple3(truncate(bl>>beatShift), idx, last_burst));
	    if (verbose) $display("MemReadEngine::%d readReq idx %d offset %h burstLenBytes %h last_burst %d", counter, idx, cmd_base, bl, last_burst);
	    return MemRequest { sglId: cmd_sglId, offset: extend(cmd_base), burstLen:bl, tag: (cmd_tag << valueOf(serverIdxSz)) | extend(idx)
`ifdef BYTE_ENABLES
			       , firstbe: maxBound, lastbe: maxBound
`endif
};
	 endmethod
      endinterface
      interface Put readData = toPut(serverDataFifo);
   endinterface 
endmodule