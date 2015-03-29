
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

// bsv libraries
import Vector::*;

// Connectal libraries
import CtrlMux::*;
import Portal::*;
import MemTypes::*;
import HostInterface::*;
import MMU::*;
import MemServer::*;

// generated by tool
import GyroCtrlRequest::*;
import GyroCtrlIndication::*;
import HBridgeCtrlRequest::*;
import HBridgeCtrlIndication::*;
import MemServerRequest::*;
import MMURequest::*;
import MemServerIndication::*;
import MMUIndication::*;
import MaxSonarCtrlRequest::*;
import MaxSonarCtrlIndication::*;

// defined by user
import Controller::*;

typedef enum {MaxSonarControllerRequest, MaxSonarControllerIndication, 
	      GyroControllerRequest,     GyroControllerIndication, 
	      HBridgeControllerRequest,  HBridgeControllerIndication, 
	      HostMemServerRequest,      HostMemServerIndication,   
	      HostMMURequest,            HostMMUIndication,
	      GyroSampleStream,          MaxSonarSampleStream } IfcNames deriving (Eq,Bits);

module mkConnectalTop(ConnectalTop#(PhysAddrWidth,DataBusWidth,ZedboardRobotPins,1));


   GyroCtrlIndicationProxy gcp <- mkGyroCtrlIndicationProxy(GyroControllerIndication);
   MaxSonarCtrlIndicationProxy mscp <- mkMaxSonarCtrlIndicationProxy(MaxSonarControllerIndication);
   HBridgeCtrlIndicationProxy hbcp <- mkHBridgeCtrlIndicationProxy(HBridgeControllerIndication);
   Controller controller <- mkController(mscp.ifc, gcp.ifc, hbcp.ifc);
   HBridgeCtrlRequestWrapper hbcw <- mkHBridgeCtrlRequestWrapper(HBridgeControllerRequest, controller.hbridge_req);
   GyroCtrlRequestWrapper gcw <- mkGyroCtrlRequestWrapper(GyroControllerRequest, controller.gyro_req);
   MaxSonarCtrlRequestWrapper mscw <- mkMaxSonarCtrlRequestWrapper(MaxSonarControllerRequest, controller.maxsonar_req);
   
   MMUIndicationProxy hostMMUIndicationProxy <- mkMMUIndicationProxy(HostMMUIndication);
   MMU#(PhysAddrWidth) hostMMU <- mkMMU(0, True, hostMMUIndicationProxy.ifc);
   MMURequestWrapper hostMMURequestWrapper <- mkMMURequestWrapper(HostMMURequest, hostMMU.request);
   
   MemServerIndicationProxy hostMemServerIndicationProxy <- mkMemServerIndicationProxy(HostMemServerIndication);
   MemServer#(PhysAddrWidth,DataBusWidth,1) dma <- mkMemServer(nil, cons(controller.dmaClient,nil), cons(hostMMU,nil), hostMemServerIndicationProxy.ifc);
   MemServerRequestWrapper hostMemServerRequestWrapper <- mkMemServerRequestWrapper(HostMemServerRequest, dma.request);

   Vector#(10,StdPortal) portals;
   portals[0] = gcp.portalIfc;
   portals[1] = gcw.portalIfc;
   portals[2] = mscp.portalIfc;
   portals[3] = mscw.portalIfc;
   portals[4] = hostMemServerRequestWrapper.portalIfc;
   portals[5] = hostMemServerIndicationProxy.portalIfc; 
   portals[6] = hostMMURequestWrapper.portalIfc;
   portals[7] = hostMMUIndicationProxy.portalIfc;
   portals[8] = hbcp.portalIfc;
   portals[9] = hbcw.portalIfc;
   let ctrl_mux <- mkSlaveMux(portals);
   
   interface interrupt = getInterruptVector(portals);
   interface slave = ctrl_mux;
   interface masters = dma.masters;
   interface pins = controller.pins;

endmodule : mkConnectalTop

export Controller::*;
export mkConnectalTop;

