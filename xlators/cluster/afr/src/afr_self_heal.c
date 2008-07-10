/*
   Copyright (c) 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat.h"

enum {
  AFR_LABEL_1,
  AFR_LABEL_2,
  AFR_LABEL_3,
  AFR_LABEL_4,
  AFR_LABEL_5,
  AFR_LABEL_6,
  AFR_LABEL_7,
  AFR_LABEL_8,
  AFR_LABEL_9,
};

extern int32_t
afr_lookup_unlock_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno);

void afr_lookup_directory_selfheal (call_frame_t*);

void
dummy_inode_destroy (inode_t * inode)
{
  dict_destroy (inode->ctx);
  LOCK_DESTROY (&inode->lock);
  FREE (inode);
}

static inode_t *
dummy_inode (inode_table_t *table)
{
  inode_t *dummy;

  dummy = calloc (1, sizeof (*dummy));
  ERR_ABORT (dummy);

  dummy->table = table;

  INIT_LIST_HEAD (&dummy->list);
  INIT_LIST_HEAD (&dummy->inode_hash);
  INIT_LIST_HEAD (&dummy->fds);
  INIT_LIST_HEAD (&dummy->dentry.name_hash);
  INIT_LIST_HEAD (&dummy->dentry.inode_list);

  dummy->ref = 1;
  dummy->ctx = get_new_dict ();

  LOCK_INIT (&dummy->lock);
  return dummy;
}

int32_t
afr_lds_closedir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }
  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);

  if (callcnt == 0)
    afr_lookup_directory_selfheal (frame);
  return 0;
}

int32_t
afr_lds_setxattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }

  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);

  if (callcnt == 0)
    afr_lookup_directory_selfheal (frame);
  return 0;
}

int32_t
afr_lds_setdents_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }

  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);

  if (callcnt == 0)
    afr_lookup_directory_selfheal (frame);
  return 0;
}

int32_t
afr_lds_rmelem_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }
  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);

  if (callcnt == 0) {
    dir_entry_t *entry;
    for (entry = asp->entries; entry;) {
      dir_entry_t *temp;
      temp = entry;
      entry = temp->next;
      if (temp->name)
	FREE (temp->name);
      FREE (temp);
    }
    afr_lookup_directory_selfheal (frame);
  }
  return 0;
}

int32_t
afr_lds_lookup_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf,
		    dict_t *xattr)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;
  dir_entry_t *entry = asp->entries;

  if (inode != NULL)
    inode_destroy (inode);
  else
    GF_ERROR (this, "inode is NULL");
  if (op_ret == -1 && op_errno != ENOENT) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }
  LOCK(&frame->lock);
  if (op_ret == 0) {
    if (entry == cookie) {
      FREE (entry->name);
      entry->name = NULL;
    } else {
      while (entry) {
	if (entry->next == cookie) {
	  FREE (entry->next->name);
	  entry->next->name = NULL;
	  break;
	}
	entry = entry->next;
      }
    }
  }
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);
  if (callcnt == 0)
    afr_lookup_directory_selfheal (frame);
  return 0;
}

int32_t
afr_lds_getdents_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      dir_entry_t *entry,
		      int32_t count)
{
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  } else
    asp->dents_count = count;
  if (count > 0) {
    asp->entries = entry->next;
    entry->next = NULL;
  }
  afr_lookup_directory_selfheal(frame);
  return 0;
}

int32_t
afr_lds_opendir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }

  if (callcnt == 0) {
    afr_lookup_directory_selfheal (frame);
  }
  return 0;
}

void afr_lookup_directory_selfheal(call_frame_t *frame)
{
  afr_private_t *pvt = frame->this->private;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;
  int32_t child_count = pvt->child_count, i;
  afr_selfheal_t *ashptr = local->ashptr;
  xlator_t **children = pvt->children;
  xlator_t *this = frame->this;
  dir_entry_t *entry;
  int32_t latest = local->latest;
  int32_t cnt = 0, len = 0;
  char DEBUG1[BUFSIZ] = {0,}, DEBUG2[BUFSIZ] = {0,};

  local = frame->local;
  asp = local->asp;

  if (asp == NULL)
    goto AFR_LABEL_1_GOTO;

  i = asp->i;

  switch (asp->label) {
  case AFR_LABEL_2:
    goto AFR_LABEL_2_GOTO;
  case AFR_LABEL_3:
    goto AFR_LABEL_3_GOTO;
  case AFR_LABEL_4:
    goto AFR_LABEL_4_GOTO;
  case AFR_LABEL_5:
    goto AFR_LABEL_5_GOTO;
  case AFR_LABEL_6:
    goto AFR_LABEL_6_GOTO;
  case AFR_LABEL_7:
    goto AFR_LABEL_7_GOTO;
  case AFR_LABEL_8:
    goto AFR_LABEL_8_GOTO;
  case AFR_LABEL_9:
    goto AFR_LABEL_9_GOTO;
  default:
    goto AFR_ERROR;
  }

 AFR_LABEL_1_GOTO:
  GF_TRACE (this, "at LABEL1");
  /* FIXME: free asp */
  asp = calloc (1, sizeof(*asp));
  ERR_ABORT (asp);
  asp->loc = calloc (1, sizeof (loc_t));
  ERR_ABORT (asp->loc);

  local->asp = asp;
  DEBUG2[0] = '\0';
  for (i = 0; i < child_count; i++) {
    if (ashptr[i].repair) {
      strcat (DEBUG2, " ");
      strcat (DEBUG2, children[i]->name);
    }

    if (i == local->latest || ashptr[i].repair)
      local->call_count++;
  }

  GF_TRACE (this, "Latest on %s, repair on %s, call_count %d", children[local->latest]->name, DEBUG2, local->call_count);
  asp->label = AFR_LABEL_2;
  for (i = 0; i < child_count; i++) {
    if (i == local->latest || ashptr[i].repair) {
      STACK_WIND (frame,
		  afr_lds_opendir_cbk,
		  children[i],
		  children[i]->fops->opendir,
		  local->loc,
		  local->fd);
    }
  }
  return;
 AFR_LABEL_2_GOTO:
  GF_TRACE (this, "at LABEL_2");
  if (asp->error)
    goto AFR_ERROR;
  for (i = 0; i < child_count; i++) {
    local->offset = 0;
    if (ashptr[i].repair) {
      while (1) {
	asp->i = i;
	asp->label = AFR_LABEL_3;
	STACK_WIND (frame,
		    afr_lds_getdents_cbk,
		    children[i],
		    children[i]->fops->getdents,
		    local->fd,
		    100,
		    0,
		    0);
	return;
 AFR_LABEL_3_GOTO:
	GF_TRACE (this, "at LABEL_3");
	if (asp->error)
	  goto AFR_ERROR;
	/* all the contents read */
	if (asp->dents_count == 0) {
	  GF_TRACE (this, "dents_count = 0, all conents read from %s, breaking.", children[i]->name);
	  break;
	}
	DEBUG1[0] = '\0';
	len = 0;
	asp->label = AFR_LABEL_4;
	for (entry = asp->entries; entry; entry = entry->next) {
	  local->call_count++;

	  /* for debug info only */
	  len += strlen (entry->name) + 1;
	  if (len + 1 >= BUFSIZ) {
	    GF_TRACE (this, "getdents contents: %s", DEBUG1);
	    len = 0;
	    DEBUG1[0] = '\0';
	  }
	  strcat (DEBUG1, entry->name);
	  strcat (DEBUG1, " ");
	}
	cnt = local->call_count;
	if (len) {
	  GF_TRACE (this, "getdents contents: %s", DEBUG1);
	}
	GF_TRACE (this, "call_count = %d", cnt);
	for (entry = asp->entries; entry; entry = entry->next) {
	  char path[PATH_MAX];
	  strcpy (path, local->loc->path);
	  strcat (path, "/");
	  strcat (path, entry->name);
	  asp->loc->path = path;
	  asp->loc->inode = dummy_inode (local->loc->inode->table);
	  /* inode structure will be freed in the call back */
	  STACK_WIND_COOKIE (frame,
			     afr_lds_lookup_cbk,
			     entry,
			     children[local->latest],
			     children[local->latest]->fops->lookup,
			     asp->loc,
			     0);
	  if (--cnt == 0)
	    break;
	}
	return;
 AFR_LABEL_4_GOTO:
	GF_TRACE (this, "at LABEL_4");
	if (asp->error)
	  goto AFR_ERROR;
	DEBUG1[0] = '\0';
	len = 0;
	asp->label = AFR_LABEL_5;
	for (entry = asp->entries; entry; entry = entry->next) {
	  if (entry->name == NULL)
	    continue;
	  local->call_count++;

	  /* for debug info only */
	  len += strlen (entry->name) + 1;
	  if (len + 1 >= BUFSIZ) {
	    GF_TRACE (this, "rmelem on %s: %s", children[i]->name, DEBUG1);
	    len = 0;
	    DEBUG1[0] = '\0';
	  }
	  strcat (DEBUG1, entry->name);
	  strcat (DEBUG1, " ");
	}
	if (local->call_count == 0) {
	  GF_TRACE (this, "nothing to rmelem in this loop");
	  continue;
	}

	cnt = local->call_count;
	if (len) {
	  GF_TRACE (this, "rmelem contents: %s", DEBUG1);
	}
	GF_TRACE (this, "call_count = %d", cnt);
	for (entry = asp->entries; entry; entry = entry->next) {
	  char path[PATH_MAX];
	  if (entry->name == NULL)
	    continue;
	  strcpy (path, local->loc->path);
	  strcat (path, "/");
	  strcat (path, entry->name);
	  STACK_WIND (frame,
		      afr_lds_rmelem_cbk,
		      children[i],
		      children[i]->fops->rmelem,
		      path);
	  if (--cnt == 0)
	    break;
	}
	return;
 AFR_LABEL_5_GOTO:
	GF_TRACE (this, "at LABEL_5");
	if (asp->error)
	  goto AFR_ERROR;
      }
    }
  }

  while (1) {
    asp->label = AFR_LABEL_6;
    STACK_WIND (frame,
		afr_lds_getdents_cbk,
		children[local->latest],
		children[local->latest]->fops->getdents,
		local->fd,
		100,
		0,
		0);
    return;
 AFR_LABEL_6_GOTO:
    GF_TRACE (this, "at LABEL_6");
    if (asp->error)
      goto  AFR_ERROR;
    if (asp->dents_count == 0)
      break;
    asp->label = AFR_LABEL_7;
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair)
	local->call_count++;
    }
    int32_t count = 0, cnt;
    for (entry = asp->entries; entry; entry = entry->next)
      count++;
    cnt = local->call_count;
    GF_TRACE (this, "calling setdents(), number of entries is %d", count);
    dir_entry_t *tmpdir = calloc (1, sizeof (*tmpdir));
    ERR_ABORT (tmpdir);
    tmpdir->next = asp->entries;
    asp->entries = tmpdir;
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair) {
	STACK_WIND (frame,
		    afr_lds_setdents_cbk,
		    children[i],
		    children[i]->fops->setdents,
		    local->fd,
		    GF_SET_IF_NOT_PRESENT | GF_SET_EPOCH_TIME,
		    asp->entries,
		    count);
	if (--cnt == 0)
	  break;
      }
    }
    for (entry = tmpdir->next; entry;) {
      dir_entry_t *temp;
      temp = entry;
      entry = temp->next;
      FREE (temp->name);
      FREE (temp);
    }
    FREE (tmpdir);

    return;
 AFR_LABEL_7_GOTO:
    GF_TRACE (this, "at LABEL_7");
    if (asp->error)
      goto AFR_ERROR;
  }

  {
    dict_t *latest_xattr;
    char *version_str, *ctime_str;

    latest_xattr = get_new_dict();
    asprintf (&version_str, "%u", ashptr[latest].version);
    asprintf (&ctime_str, "%u", ashptr[latest].ctime);
    dict_set (latest_xattr, GLUSTERFS_VERSION, data_from_dynptr (version_str, strlen(version_str)));
    dict_set (latest_xattr, GLUSTERFS_CREATETIME, data_from_dynptr (ctime_str, strlen(ctime_str)));

    GF_TRACE (this, "latest ctime/version = %s/%s", ctime_str, version_str);

    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair)
	local->call_count++;
    }
    cnt = local->call_count;
    asp->label = AFR_LABEL_8;
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair) {
	STACK_WIND (frame,
		    afr_lds_setxattr_cbk,
		    children[i],
		    children[i]->fops->setxattr,
		    local->loc,
		    latest_xattr,
		    0);
	if (--cnt == 0)
	  break;
      }
    }
    dict_destroy (latest_xattr);
    return;
  }

 AFR_LABEL_8_GOTO:
  GF_TRACE (this, "at LABEL_8");
  if (asp->error)
    goto AFR_ERROR;

  for (i = 0; i < child_count; i++) {
    if (ashptr[i].repair || i == latest)
      local->call_count++;
  }
  asp->label = AFR_LABEL_9;
  cnt = local->call_count;
  for (i = 0; i < child_count; i++) {
    if (ashptr[i].repair || i == latest) {
      STACK_WIND (frame,
		  afr_lds_closedir_cbk,
		  children[i],
		  children[i]->fops->closedir,
		  local->fd);
      if (--cnt == 0)
	break;
    }
  }
  return;
 AFR_LABEL_9_GOTO:
  GF_TRACE (this, "at LABEL_8");
  if (asp->error)
    goto AFR_ERROR;

  goto AFR_SUCCESS;
 AFR_ERROR:
  GF_TRACE (this, "at AFR_ERROR");
  local->rmelem_status = 1;
 AFR_SUCCESS:
  GF_TRACE (this, "at AFR_SUCCESS");
  if (asp) {
    FREE (asp->loc);
    FREE (asp);
  }
  char *lock_path;
  asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
  STACK_WIND (frame,
	      afr_lookup_unlock_cbk,
	      local->lock_node,
	      local->lock_node->mops->unlock,
	      lock_path);
  return;
}
