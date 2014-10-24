
// Copyright (c) 2012 Nokia, Inc.
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
import GetPut::*;
import FIFOF::*;
import SpecialFIFOs::*;
import FIFO::*;
import Connectable::*;

import Portal::*;
import Directory::*;
import MemTypes::*;
import Arith::*;
import Pipe::*;

module mkInterruptMux#(Vector#(numPortals,ReadOnly#(Bool)) inputs) (ReadOnly#(Bool))
   provisos(Add#(nz, TLog#(numPortals), 4),
	    Add#(1, a__, numPortals));
   
   function Bool my_read(ReadOnly#(Bool) x);
      return x._read;
   endfunction
   
   method Bool _read;
      return fold(boolor,map(my_read,inputs));
   endmethod

endmodule

module mkSlaveMux#(Directory#(aw,aw,dataWidth) dir,
		   Vector#(numPortals,MemPortal#(aw,dataWidth)) portals) (MemSlave#(addrWidth,dataWidth))
   provisos(Add#(1,numPortals,numInputs),
	    Add#(a__,TLog#(numInputs),4),
	    Min#(2,TLog#(numInputs),bpc),
	    FunnelPipesPipelined#(1, numInputs, MemData#(dataWidth), bpc)
	    );
   
   Vector#(numInputs, MemSlave#(aw,dataWidth)) portalIfcs = cons(dir.portalIfc.slave,map(getSlave, portals));
   let port_sel_low = valueOf(aw);
   let port_sel_high = valueOf(TAdd#(3,aw));
   function Bit#(4) psel(Bit#(addrWidth) a);
      return a[port_sel_high:port_sel_low];
   endfunction
   function Bit#(aw) asel(Bit#(addrWidth) a);
      return a[(port_sel_low-1):0];
   endfunction
   function Get#(MemData#(dataWidth)) getMemPortalReadData(MemSlave#(aw,dataWidth) x) = x.read_server.readData;
   function Put#(MemData#(dataWidth)) getMemPortalWriteData(MemSlave#(aw,dataWidth) x) = x.write_server.writeData;
   
   FIFO#(Bit#(6))        doneFifo <- mkFIFO1();

   FIFO#(PhysMemRequest#(aw)) req_ars <- mkSizedFIFO(1);
   FIFO#(Bit#(TLog#(numInputs))) rs <- mkFIFO1();
   Vector#(numInputs, PipeOut#(MemData#(dataWidth))) readDataPipes <- mapM(mkPipeOut, map(getMemPortalReadData,portalIfcs));
   FunnelPipe#(1, numInputs, MemData#(dataWidth), bpc) read_data_funnel <- mkFunnelPipesPipelined(readDataPipes);
      
   FIFO#(PhysMemRequest#(aw)) req_aws <- mkFIFO1();
   FIFO#(Bit#(TLog#(numInputs))) ws <- mkFIFO1();
   FIFOF#(Tuple2#(Bit#(TLog#(numInputs)), MemData#(dataWidth))) write_data <- mkFIFOF;
   UnFunnelPipe#(1, numInputs, MemData#(dataWidth), bpc) write_data_unfunnel <- mkUnFunnelPipesPipelined(cons(toPipeOut(write_data),nil));
   Vector#(numInputs, PipeIn#(MemData#(dataWidth))) writeDataPipes <- mapM(mkPipeIn, map(getMemPortalWriteData,portalIfcs));
   zipWithM(mkConnection, write_data_unfunnel, writeDataPipes);
 
   rule req_aw;
      let req <- toGet(req_aws).get;
      portalIfcs[ws.first].write_server.writeReq.put(req);
   endrule
         
   rule req_ar;
      let req <- toGet(req_ars).get;
      portalIfcs[rs.first].read_server.readReq.put(req);
   endrule
   
   rule write_done_rule;
      let rv <- portalIfcs[ws.first].write_server.writeDone.get();
      ws.deq();
      doneFifo.enq(rv);
   endrule

   interface PhysMemWriteServer write_server;
      interface Put writeReq;
	 method Action put(PhysMemRequest#(addrWidth) req);
	    req_aws.enq(PhysMemRequest{addr:asel(req.addr), burstLen:req.burstLen, tag:req.tag});
	    if (req.burstLen > 4) $display("**** \n\n mkSlaveMux.writeReq len=%d \n\n ****", req.burstLen);
	    ws.enq(truncate(psel(req.addr)));
	    //$display("mkSlaveMux.writeReq addr=%h aw=%d psel=%h", req.addr, valueOf(aw), psel(req.addr));
	 endmethod
      endinterface
      interface Put writeData;
	 method Action put(MemData#(dataWidth) wdata);
	    write_data.enq(tuple2(ws.first,wdata));
	 endmethod
      endinterface
      interface Get writeDone;
	 method ActionValue#(Bit#(6)) get();
	    let rv <- toGet(doneFifo).get();
	    return rv;
	 endmethod
      endinterface
   endinterface
   interface PhysMemReadServer read_server;
      interface Put readReq;
	 method Action put(PhysMemRequest#(addrWidth) req);
	    req_ars.enq(PhysMemRequest{addr:asel(req.addr), burstLen:req.burstLen, tag:req.tag});
	    rs.enq(truncate(psel(req.addr)));
	    if (req.burstLen > 4) $display("**** \n\n mkSlaveMux.readReq len=%d \n\n ****", req.burstLen);
	    //$display("mkSlaveMux.readReq addr=%h aw=%d psel=%h", req.addr, valueOf(aw), psel(req.addr));
	 endmethod
      endinterface
      interface Get readData;
	 method ActionValue#(MemData#(dataWidth)) get();
	    let rv <- toGet(read_data_funnel[0]).get;
	    rs.deq();
	    //$display("mkSlaveMux.readData rs=%d data=%h", rs.first, rv.data);
	    return rv;
	 endmethod
      endinterface
   endinterface
   
endmodule

module mkMemSlaveMux#(Vector#(numSlaves,MemSlave#(aw,dataWidth)) slaves) (MemSlave#(addrWidth,dataWidth))
   provisos(Add#(selWidth,aw,addrWidth),
	    Add#(a__, TLog#(numSlaves), selWidth)
      );

   Vector#(numSlaves, MemSlave#(aw,dataWidth)) portalIfcs = take(slaves);
   let port_sel_low = valueOf(aw);
   let port_sel_high = valueOf(TSub#(addrWidth,1));
   function Bit#(selWidth) psel(Bit#(addrWidth) a);
      return a[port_sel_high:port_sel_low];
   endfunction
   function Bit#(aw) asel(Bit#(addrWidth) a);
      return a[(port_sel_low-1):0];
   endfunction

   FIFO#(Bit#(6)) doneFifo          <- mkFIFO1();

   FIFO#(PhysMemRequest#(aw))   req_ars <- mkFIFO1();
   FIFO#(Bit#(TLog#(numSlaves))) rs <- mkFIFO1();

   FIFO#(PhysMemRequest#(aw))   req_aws <- mkFIFO1();
   FIFO#(Bit#(TLog#(numSlaves))) ws <- mkFIFO1();

   rule write_done;
      let rv <- portalIfcs[ws.first].write_server.writeDone.get();
      ws.deq();
      doneFifo.enq(rv);
   endrule

   rule req_aw;
      let req <- toGet(req_aws).get;
      portalIfcs[ws.first].write_server.writeReq.put(req);
   endrule

   rule req_ar;
      let req <- toGet(req_ars).get;
      portalIfcs[rs.first].read_server.readReq.put(req);
   endrule

   interface PhysMemWriteServer write_server;
      interface Put writeReq;
	 method Action put(PhysMemRequest#(addrWidth) req);
	    req_aws.enq(PhysMemRequest{addr:asel(req.addr), burstLen:req.burstLen, tag:req.tag});
	    if (req.burstLen > 4) $display("**** \n\n mkMemSlaveMux.writeReq len=%d \n\n ****", req.burstLen);
	    //$display("mkMemSlaveMux.writeReq addr=%h selWidth=%d aw=%d psel=%h", req.addr, valueOf(selWidth), valueOf(aw), psel(req.addr));
	    ws.enq(truncate(psel(req.addr)));
	 endmethod
      endinterface
      interface Put writeData;
	 method Action put(MemData#(dataWidth) wdata);
	    //$display("mkMemSlaveMux.writeData aw=%d ws=%d data=%h", valueOf(aw), ws.first, wdata.data);
	    portalIfcs[ws.first].write_server.writeData.put(wdata);
	 endmethod
      endinterface
      interface Get writeDone;
	 method ActionValue#(Bit#(6)) get();
	    let rv <- toGet(doneFifo).get();
	    return rv;
	 endmethod
      endinterface
   endinterface
   interface PhysMemReadServer read_server;
      interface Put readReq;
	 method Action put(PhysMemRequest#(addrWidth) req);
	    req_ars.enq(PhysMemRequest{addr:asel(req.addr), burstLen:req.burstLen, tag:req.tag});
	    //$display("mkMemSlaveMux.readReq addr=%h aw=%d psel=%h", req.addr, valueOf(aw), psel(req.addr));
	    if (req.burstLen > 4) $display("**** \n\n mkMemSlaveMux.readReq len=%d \n\n ****", req.burstLen);
	    rs.enq(truncate(psel(req.addr)));
	 endmethod
      endinterface
      interface Get readData;
	 method ActionValue#(MemData#(dataWidth)) get();
	    let rv <- portalIfcs[rs.first].read_server.readData.get();
	    //$display("mkMemSlaveMux.readData aw=%d rs=%d data=%h", valueOf(aw), rs.first, rv.data);
	    //if (rv.last) begin
	       rs.deq();
	    //end
	    return rv;
	 endmethod
      endinterface
   endinterface

endmodule

