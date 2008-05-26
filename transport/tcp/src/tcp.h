/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _TCP_H
#define _TCP_H


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "event.h"
#include "transport.h"
#include "logging.h"
#include "dict.h"
#include "mem-pool.h"

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif /* MAX_IOVEC */


typedef enum {
  TCP_PROTO_STATE_NADA = 0,
  TCP_PROTO_STATE_HEADER_COMING,
  TCP_PROTO_STATE_HEADER_CAME,
  TCP_PROTO_STATE_DATA_COMING,
  TCP_PROTO_STATE_DATA_CAME,
  TCP_PROTO_STATE_COMPLETE,
} tcp_proto_state_t;


struct tcp_header {
  char     colonO[3];
  uint32_t size1;
  uint32_t size2;
  char     version;
} __attribute__((packed));


struct ioq {
  union {
    struct list_head list;
    struct {
      struct ioq    *next;
      struct ioq    *prev;
    };
  };
  struct tcp_header  header;
  struct iovec       vector[MAX_IOVEC];
  int                count;
  struct iovec      *pending_vector;
  int                pending_count;
  dict_t            *refs;
};


typedef struct {
  int32_t             sock;
  int32_t             idx;
  unsigned char       connected; // -1 = not connected. 0 = in progress. 1 = connected
  union {
    struct list_head  ioq;
    struct {
      struct ioq     *ioq_next;
      struct ioq     *ioq_prev;
    };
  };
  struct {
    int               state;
    struct tcp_header header;
    char             *hdr_p;
    size_t            hdrlen;
    char             *buf_p;
    size_t            buflen;
    struct iovec      vector[2];
    int               count;
    struct iovec     *pending_vector;
    int               pending_count;
  } incoming;
  pthread_mutex_t     lock;
} tcp_private_t;


#endif
