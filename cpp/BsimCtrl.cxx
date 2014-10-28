// Copyright (c) 2013-2014 Quanta Research Cambridge, Inc.

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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/un.h>
#include <pthread.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "portal.h"
#include "sock_utils.h"

#define MAX_FD_ARRAY 10

static struct {
    struct memrequest req;
    int sockfd;
    unsigned int pnum;
    int valid;
    int inflight;
} head;
static struct memresponse respitem;
static pthread_mutex_t socket_mutex;
static int trace_port;// = 1;
static int fd_array[MAX_FD_ARRAY];
static int fd_array_index = 0;

static void *pthread_worker(void *p)
{
    int listening_socket = init_listening(SOCKET_NAME);
    if (trace_port)
        fprintf(stderr, "%s[%d]: waiting for a connection...\n",__FUNCTION__, listening_socket);
    while (1) {
        int sockfd = accept(listening_socket, NULL, NULL);
        if (sockfd == -1) {
            fprintf(stderr, "%s[%d]: accept error %s\n",__FUNCTION__, listening_socket, strerror(errno));
            exit(1);
        }
        if (trace_port)
            printf("[%s:%d] sockfd %d\n", __FUNCTION__, __LINE__, sockfd);
        fd_array[fd_array_index++] = sockfd;
    }
}

extern "C" void initPortal(void)
{
    pthread_t threaddata;
    pthread_mutex_init(&socket_mutex, NULL);
    pthread_create(&threaddata, NULL, &pthread_worker, NULL);
}

extern "C" void interruptLevel(uint32_t ivalue)
{
    static struct memresponse respitem;
    int i;
    if (ivalue != respitem.data) {
        respitem.portal = MAGIC_PORTAL_FOR_SENDING_INTERRUPT;
        respitem.data = ivalue;
        if (trace_port)
            printf("%s: %d\n", __FUNCTION__, ivalue);
        pthread_mutex_lock(&socket_mutex);
        for (i = 0; i < fd_array_index; i++)
           portalSend(fd_array[i], &respitem, sizeof(respitem));
        pthread_mutex_unlock(&socket_mutex);
    }
}

extern "C" bool checkForRequest(uint32_t rr)
{
    if (!head.valid){
	int rv = -1;
        int i, recvfd;
        for (i = 0; i < fd_array_index; i++) {
            head.sockfd = fd_array[i];
            rv = portalRecvFd(head.sockfd, &head.req, sizeof(head.req), &recvfd);
	    if(rv > 0){
	        //fprintf(stderr, "recv size %d\n", rv);
	        assert(rv == sizeof(memrequest));
	        respitem.portal = head.req.portal;
	        head.valid = 1;
	        head.inflight = 1;
                if (rv == sizeof(head.req) && head.req.write_flag == MAGIC_PORTAL_FOR_SENDING_FD) {
                    head.req.data_or_tag = recvfd;
                    head.req.write_flag = 1;
                }
	        if(trace_port)
	            fprintf(stderr, "processr p=%d w=%d, a=%8lx, dt=%8x:", 
		        head.req.portal, head.req.write_flag, (long)head.req.addr, head.req.data_or_tag);
                break;
	    }
        }
    }
    return head.valid && head.inflight == 1 && head.req.write_flag == (int)rr;
}

extern "C" unsigned long long getRequest32(uint32_t rr)
{
    if(trace_port)
        fprintf(stderr, " get%c", rr ? '\n' : ':');
    if (rr)
        head.valid = 0;
    head.inflight = 0;
    return (((unsigned long long)head.req.data_or_tag) << 32) | ((long)head.req.addr);
}
  
extern "C" void readResponse32(unsigned int data, unsigned int tag)
{
    if(trace_port)
        fprintf(stderr, " read = %x\n", data);
    pthread_mutex_lock(&socket_mutex);
    respitem.data = data;
    respitem.tag = tag;
    portalSend(head.sockfd, &respitem, sizeof(respitem));
    pthread_mutex_unlock(&socket_mutex);
    head.valid = 0;
}
