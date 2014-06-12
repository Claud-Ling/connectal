// bsv libraries
import Vector::*;
import FIFO::*;
import Connectable::*;

// portz libraries
import Directory::*;
import CtrlMux::*;
import Portal::*;
import Leds::*;
import MemTypes::*;

// generated by tool
import YuvIndicationProxy::*;
import YuvRequestWrapper::*;

// defined by user
import YuvRequest::*;

typedef enum {YuvIndicationPortal, YuvRequestPortal} IfcNames deriving (Eq,Bits);

module mkPortalTop(StdPortalTop#(addrWidth));

   // instantiate user portals
   YuvIndicationProxy yuvIndicationProxy <- mkYuvIndicationProxy(YuvIndicationPortal);
   YuvRequest yuvRequest <- mkYuvRequest(yuvIndicationProxy.ifc);
   YuvRequestWrapper yuvRequestWrapper <- mkYuvRequestWrapper(YuvRequestPortal,yuvRequest);
   
   Vector#(2,StdPortal) portals;
   portals[0] = yuvRequestWrapper.portalIfc; 
   portals[1] = yuvIndicationProxy.portalIfc;
   
   // instantiate system directory
   StdDirectory dir <- mkStdDirectory(portals);
   let ctrl_mux <- mkSlaveMux(dir,portals);
   
   interface interrupt = getInterruptVector(portals);
   interface slave = ctrl_mux;
   interface masters = nil;
   interface leds = default_leds;

endmodule : mkPortalTop


