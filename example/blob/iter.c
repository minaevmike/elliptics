/*
 * 2008+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elliptics/interface.h"
#include "backends.h"
#include "blob.h"

#ifndef __unused
#define __unused	__attribute__ ((unused))
#endif

int blob_iterate(int fd, unsigned int bsize,
		int (* callback)(struct blob_disk_control *dc, void *data, off_t position, void *priv), void *priv)
{
	struct blob_disk_control dc;
	void *data, *ptr;
	off_t position;
	struct stat st;
	size_t size, sz;
	int err;

	err = fstat(fd, &st);
	if (err) {
		err = -errno;
		dnet_backend_log(DNET_LOG_ERROR, "blob: failed to stat file: %s.\n", strerror(errno));
		goto err_out_exit;
	}

	size = st.st_size;

	if (!size) {
		err = 0;
		goto err_out_exit;
	}

	ptr = data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		err = -errno;
		dnet_backend_log(DNET_LOG_ERROR, "blob: failed to mmap file, size: %zu: %s.\n", strerror(errno));
		goto err_out_exit;
	}

	while (size) {
		err = -EINVAL;

		if (size < sizeof(struct blob_disk_control)) {
			dnet_backend_log(DNET_LOG_ERROR, "blob: iteration fails: size (%zu) is less than disk control struct (%zu).\n",
					size, sizeof(struct blob_disk_control));
			goto err_out_unmap;
		}

		dc = *(struct blob_disk_control *)ptr;
		blob_convert_disk_control(&dc);

		position = ptr - data;

		if (size < dc.size + sizeof(struct blob_disk_control)) {
			dnet_backend_log(DNET_LOG_ERROR, "blob: iteration fails: size (%zu) is less than on-disk specified size (%llu).\n",
					size, (unsigned long long)dc.size);
			goto err_out_unmap;
		}

		err = callback(&dc, ptr + sizeof(struct blob_disk_control), position, priv);
		if (err < 0) {
			dnet_backend_log(DNET_LOG_ERROR, "blob: iteration callback fails: size: %llu, position: %llu, err: %d.\n",
					dc.size, position, err);
			goto err_out_unmap;
		}

		sz = dc.size + sizeof(struct blob_disk_control);
		if (bsize)
			sz = ALIGN(sz, bsize);

		ptr += sz;
		size -= sz;
	}

	err = 0;

err_out_unmap:
	munmap(data, st.st_size);
err_out_exit:
	return err;
}
