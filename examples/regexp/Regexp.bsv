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

import FIFO::*;
import FIFOF::*;
import SpecialFIFOs::*;
import Vector::*;
import BRAM::*;
import Gearbox::*;
import StmtFSM::*;
import ClientServer::*;
import GetPut::*;
import Connectable::*;

import AxiMasterSlave::*;
import MemTypes::*;
import MemreadEngine::*;
import Pipe::*;
import Dma2BRAM::*;
import RegexpEngine::*;

interface RegexpRequest;
   method Action setup(Bit#(32) mapSGLId, Bit#(32) mapLen);
   method Action search(Bit#(32) token, Bit#(32) haystackSGLId, Bit#(32) haystackLen);
endinterface

interface RegexpIndication;
   method Action setupComplete(Bit#(32) token);
   method Action searchResult(Bit#(32) token, Int#(32) v);
endinterface

interface Regexp#(numeric type busWidth);
   interface RegexpRequest request;
   interface ObjectReadClient#(busWidth) config_read_client;
   interface ObjectReadClient#(busWidth) haystack_read_client;
endinterface

typedef `DEGPAR DegPar;

module mkRegexp#(RegexpIndication indication)(Regexp#(64))
   provisos( Log#(`MAX_NUM_STATES,5)
	    ,Log#(`MAX_NUM_CHARS,5)
	    ,Div#(64,8,nc)
	    ,Mul#(nc,8,64)
	    ,Add#(0,DegPar,p)
	    ,Log#(p,lp)
	    );

   MemreadEngineV#(64, 1, p) config_re <- mkMemreadEngine;
   MemreadEngineV#(64, 1, p) haystack_re <- mkMemreadEngine;
   let read_servers = zip(config_re.read_servers,haystack_re.read_servers);
   Vector#(p, RegexpEngine#(lp)) rees <- mapM(uncurry(mkRegexpEngine), zip(read_servers,genVector));
   Reg#(RegexpState) state <- mkReg(Config_charMap);

   let readyFIFO <- mkSizedFIFOF(valueOf(p));
   Vector#(p, PipeOut#(LDR#(lp))) ldrPipes;   
   
   FIFOF#(Tuple2#(Bit#(lp), Pair#(Bit#(32)))) setsearchFIFO <- mkFIFOF;
   UnFunnelPipe#(1,p,Pair#(Bit#(32)),1) setsearchPipeUnFunnel <- mkUnFunnelPipesPipelined(cons(toPipeOut(setsearchFIFO),nil));

   for(Integer i = 0; i < valueOf(p); i=i+1) begin
      ldrPipes[i] = rees[i].ldr;
      mkConnection(setsearchPipeUnFunnel[i],rees[i].setsearch);
   end
   FunnelPipe#(1,p,LDR#(lp),1) ldr <- mkFunnelPipesPipelined(ldrPipes);
   
   rule ldrr;
      let rv <- toGet(ldr[0]).get;
      case (rv) matches
	 tagged Ready  .r : readyFIFO.enq(r);
	 tagged Done   .d : indication.searchResult(extend(d), -1);
	 tagged Loc    .l : indication.searchResult(extend(tpl_1(l)), tpl_2(l));
	 tagged Config .c : indication.setupComplete(extend(c));
      endcase
   endrule
      
   interface config_read_client = config_re.dmaClient;
   interface haystack_read_client = haystack_re.dmaClient;

   interface RegexpRequest request;
      method Action setup(Bit#(32) sglId, Bit#(32) len) if (state != Search);
	 setsearchFIFO.enq(tuple2(readyFIFO.first,tuple2(sglId,len)));
	 case (state) matches
	    Config_charMap:  state <= Config_stateMap;
	    Config_stateMap: state <= Config_stateTransitions;
	    Config_stateTransitions:  
	    begin
	       state <= Config_charMap;
	       readyFIFO.deq;
	    end
	 endcase
      endmethod
      method Action search(Bit#(32) token, Bit#(32) sglId, Bit#(32) len);
	 $display("mkRegexp::search %d %d %d", token, sglId, len);
	 setsearchFIFO.enq(tuple2(truncate(token),tuple2(sglId,len)));
      endmethod
   endinterface

endmodule

