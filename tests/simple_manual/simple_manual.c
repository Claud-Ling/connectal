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

#include "GeneratedTypes.h"

static int v1a = 42;
static int v2a = 2;
static int v2b = 4;
#define MAX_INDARRAY 2
static PortalInternal intarr[MAX_INDARRAY];
//static PORTAL_INDFUNC indfn[MAX_INDARRAY];

void SimpleIndicationWrapperheard1_cb (  struct PortalInternal *p, const uint32_t v )
{
    PORTAL_PRINTF("heard1(%d)\n", v);
}
void SimpleIndicationWrapperheard2_cb (  struct PortalInternal *p, const uint32_t a, const uint32_t b )
{
    PORTAL_PRINTF("heard2(%d %d)\n", a, b);
}

static void manual_event(void)
{
    int i;
    for (i = 0; i < MAX_INDARRAY; i++)
      portalCheckIndication(&intarr[i]);
}

int main(int argc, const char **argv)
{

   init_portal_internal(&intarr[0], IfcNames_SimpleRequest, NULL, SimpleRequestProxy_reqsize); // portal 1
   init_portal_internal(&intarr[1], IfcNames_SimpleIndication, SimpleIndicationWrapper_handleMessage, SimpleIndicationWrapper_reqsize); // portal 2

   portalEnableInterrupts(&intarr[0], 0);
   portalEnableInterrupts(&intarr[1], 0);
   PORTAL_PRINTF("Main::calling say1(%d)\n", v1a);
   //device->say1(v1a);  
   SimpleRequestProxy_say1 (&intarr[0], v1a);
   manual_event();

   PORTAL_PRINTF("Main::calling say2(%d, %d)\n", v2a,v2b);
   //device->say2(v2a,v2b);
   SimpleRequestProxy_say2 (&intarr[0], v2a, v2b);
   manual_event();
   return 0;
}
