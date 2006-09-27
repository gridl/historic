# Copyright (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
#  
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#  
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#  
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#  
ib-osminclude.%: PACKAGE_NAME=openib-userspace-svn3640-1
ib-osminclude.%: subdir=osm/include
ib-osminclude.%: LIVE_PREPARE_CMD=cd userspace/management/$(subdir) \
		&& ([ -f configure ] || ./autogen.sh)
ib-osminclude.%: LIVE_CONFIGURE_CMD=cd userspace/management/$(subdir) \
		&& ([ -f Makefile ] || \
		./configure --prefix=/usr \
		--host=$(CROSS) --build=$(GLUSTER_BUILD))
ib-osminclude.%: LIVE_BUILD_CMD=cd userspace/management/$(subdir) \
		&& $(DEFAULT_LIVE_BUILD_CMD)
ib-osminclude.%: LIVE_INSTALL_CMD=cd userspace/management/$(subdir) \
		&& make install DESTDIR=$1

### hpc

ib-osminclude.%: PKG_PREPARE_CMD=cd userspace/management/$(subdir) \
		&& ([ -f configure ] || ./autogen.sh)
ib-osminclude.%: PKG_CONFIGURE_CMD=cd userspace/management/$(subdir) \
		&& ([ -f Makefile ] || \
		./configure --prefix=/opt/gluster  \
		--host=$(CROSS) --build=$(GLUSTER_BUILD))
ib-osminclude.%: PKG_BUILD_CMD=cd userspace/management/$(subdir) \
		&& $(DEFAULT_PKG_BUILD_CMD)
ib-osminclude.%: PKG_INSTALL_CMD=cd userspace/management/$(subdir) \
		&& make install DESTDIR=$1
