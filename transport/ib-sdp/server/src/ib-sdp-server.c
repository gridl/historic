/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include "dict.h"
#include "glusterfs.h"
#include "transport.h"
#include "protocol.h"
#include "logging.h"
#include "xlator.h"
#include "ib-sdp.h"


int32_t gf_transport_fini (struct transport *this);

static int32_t
ib_sdp_server_submit (transport_t *this, char *buf, int32_t len)
{
  ib_sdp_private_t *priv = this->private;
  int32_t ret;

  if (!priv->connected)
    return -1;

  pthread_mutex_lock (&priv->write_mutex);
  ret = gf_full_write (priv->sock, buf, len);
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

static int32_t
ib_sdp_server_writev (transport_t *this,
		      const struct iovec *vector,
		      int32_t count)
{
  ib_sdp_private_t *priv = this->private;
  int32_t ret;

  if (!priv->connected)
    return -1;

  pthread_mutex_lock (&priv->write_mutex);
  ret = gf_full_writev (priv->sock, vector, count);
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

struct transport_ops transport_ops = {
  //  .flush = ib_sdp_flush,
  .recieve = ib_sdp_recieve,
  .disconnect = ib_sdp_disconnect,

  .submit = ib_sdp_server_submit,
  .except = ib_sdp_except,

  .readv = ib_sdp_readv,
  .writev = ib_sdp_server_writev
};

static int32_t
ib_sdp_server_notify (xlator_t *xl,
		      int32_t event,
		      void *data,
		      ...)
{
  transport_t *trans = data;
  int32_t main_sock;
  transport_t *this = calloc (1, sizeof (transport_t));
  this->private = calloc (1, sizeof (ib_sdp_private_t));

  pthread_mutex_init (&((ib_sdp_private_t *)this->private)->read_mutex, NULL);
  pthread_mutex_init (&((ib_sdp_private_t *)this->private)->write_mutex, NULL);
  //  pthread_mutex_init (&((ib_sdp_private_t *)this->private)->queue_mutex, NULL);

  GF_ERROR_IF_NULL (xl);

  trans->xl = xl;
  this->xl = xl;

  ib_sdp_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);

  struct sockaddr_in sin;
  socklen_t addrlen = sizeof (sin);

  main_sock = ((ib_sdp_private_t *) trans->private)->sock;
  priv->sock = accept (main_sock, &sin, &addrlen);
  if (priv->sock == -1) {
    gf_log ("ib_sdp/server",
	    GF_LOG_ERROR,
	    "accept() failed: %s",
	    strerror (errno));
    free (this->private);
    return -1;
  }

  this->ops = &transport_ops;
  this->fini = (void *)gf_transport_fini;
  this->notify = ((ib_sdp_private_t *)trans->private)->notify;
  priv->connected = 1;
  priv->addr = sin.sin_addr.s_addr;
  priv->port = sin.sin_port;

  priv->options = get_new_dict ();
  dict_set (priv->options, "remote-host", 
	    data_from_dynstr (strdup (inet_ntoa (sin.sin_addr))));
  dict_set (priv->options, "remote-port", 
	    data_from_uint64 (ntohs (sin.sin_port)));

  socklen_t sock_len = sizeof (struct sockaddr_in);
  getpeername (priv->sock,
	       &this->peerinfo.sockaddr,
	       &sock_len);

  gf_log ("ib_sdp/server",
	  GF_LOG_DEBUG,
	  "Registering socket (%d) for new transport object of %s",
	  priv->sock,
	  data_to_str (dict_get (priv->options, "remote-host")));

  poll_register (this->xl->ctx, priv->sock, transport_ref (this));
  return 0;
}


int32_t
gf_transport_init (struct transport *this, 
		   dict_t *options,
		   event_notify_fn_t notify)
{
  data_t *bind_addr_data;
  data_t *listen_port_data;
  char *bind_addr;
  uint16_t listen_port;

  this->private = calloc (1, sizeof (ib_sdp_private_t));
  ((ib_sdp_private_t *)this->private)->notify = notify;

  this->notify = ib_sdp_server_notify;
  struct ib_sdp_private *priv = this->private;

  struct sockaddr_in sin;
  priv->sock = socket (AF_INET_SDP, SOCK_STREAM, 0);
  if (priv->sock == -1) {
    gf_log ("ib_sdp/server",
	    GF_LOG_CRITICAL,
	    "init: failed to create socket, error: %s",
	    strerror (errno));
    free (this->private);
    return -1;
  }

  bind_addr_data = dict_get (options, "bind-address");
  if (bind_addr_data)
    bind_addr = data_to_str (bind_addr_data);
  else
    bind_addr = "0.0.0.0";

  listen_port_data = dict_get (options, "listen-port");
  if (listen_port_data)
    listen_port = htons (data_to_int64 (listen_port_data));
  else
    /* TODO: move this default port to a macro definition */
    listen_port = htons (GF_DEFAULT_LISTEN_PORT);

  sin.sin_family = AF_INET_SDP;
  sin.sin_port = listen_port;
  sin.sin_addr.s_addr = bind_addr ? inet_addr (bind_addr) : htonl (INADDR_ANY);

  int opt = 1;
  setsockopt (priv->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
  if (bind (priv->sock,
	    (struct sockaddr *)&sin,
	    sizeof (sin)) != 0) {
    gf_log ("ib_sdp/server",
	    GF_LOG_CRITICAL,
	    "init: failed to bind to socket on port %d, error: %s",
	    sin.sin_port,
	    strerror (errno));
    free (this->private);
    return -1;
  }

  if (listen (priv->sock, 10) != 0) {
    gf_log ("ib_sdp/server",
	    GF_LOG_CRITICAL,
	    "init: listen () failed on socket, error: %s",
	    strerror (errno));
    free (this->private);
    return -1;
  }

  poll_register (this->xl->ctx, priv->sock, transport_ref (this));

  pthread_mutex_init (&((ib_sdp_private_t *)this->private)->read_mutex, NULL);
  pthread_mutex_init (&((ib_sdp_private_t *)this->private)->write_mutex, NULL);
  //  pthread_mutex_init (&((ib_sdp_private_t *)this->private)->queue_mutex, NULL);

  return 0;
}

int32_t
gf_transport_fini (struct transport *this)
{
  ib_sdp_private_t *priv = this->private;
  //  this->ops->flush (this);

  if (priv->options)
    gf_log ("ib_sdp/server",
	    GF_LOG_DEBUG,
	    "destroying transport object for %s:%s (fd=%d)",
	    data_to_str (dict_get (priv->options, "remote-host")),
	    data_to_str (dict_get (priv->options, "remote-port")),
	    priv->sock);

  pthread_mutex_destroy (&priv->read_mutex);
  pthread_mutex_destroy (&priv->write_mutex);

  if (priv->options)
    dict_destroy (priv->options);
  if (priv->connected)
    close (priv->sock);
  free (priv);
  return 0;
}
