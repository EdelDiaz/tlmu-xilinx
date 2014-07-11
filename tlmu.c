/*
 * Interface towards the raw TLMu shared objects.
 *
 * Copyright (c) 2011 Edgar E. Iglesias.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <libgen.h>

#include <pthread.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <dlfcn.h>

#include "tlmu.h"

void tlmu_init(struct tlmu *t, const char *name)
{
	memset(t, 0, sizeof *t);
	t->name = name;

	/* Setup the default args.  */
	tlmu_append_arg(t, "TLMu");

	tlmu_append_arg(t, "-qmp");
	tlmu_append_arg(t, "null");
}

static void copylib(const char *path, const char *newpath)
{
	int s = -1, d = -1;
	ssize_t r, wr;
	const char *ld_path = NULL;
	Dl_info info;
	void *handle = NULL;
	void *addr;
	int ret;
	struct stat stb;

	if (stat(path, &stb) == 0) {
		/* If the path exists, use it directly */
		ld_path = strdup(path);
	} else {
		/* Otherwise, use dlopen to find path */
		handle = dlopen(path, RTLD_LOCAL | RTLD_DEEPBIND | RTLD_NOW);
		if (!handle) {
			fprintf(stderr, "dlopen(\"%s\") failed: %s\n", path, dlerror());
			goto err;
		}

		addr = dlsym(handle, "vl_main");
		if(!addr){
			fprintf(stderr, "dlsym(\"vl_main\") failed: %s\n", dlerror());
			goto err;
		}

		ret = dladdr(addr, &info);
		if (!ret) {
			fprintf(stderr, "dladdr(%p) failed: %s\n", addr, dlerror());
			goto err;
		}
		ld_path = strdup(info.dli_fname);
    }

	/* Now copy it into our per instance store.  */
	s = open(ld_path, O_RDONLY);
	if (s < 0) {
		perror(ld_path);
		goto err;
	}

	d = open(newpath, O_WRONLY | O_CREAT, S_IRWXU | S_IRWXG);
	if (d < 0) {
		if (errno != EEXIST) {
			perror(newpath);
		}
		goto err;
	}
	do {
		ssize_t written;

		char buf[4 * 1024];
		r = read(s, buf, sizeof buf);
		if (r < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		/* TODO: handle partial writes.  */
		if (r > 0) {
			written = 0;
			do {
				wr = write(d, buf, r);
				if (wr <= 0) {
					goto err;
				}
				written += wr;
			} while (written < r);
		}
	} while (r);
err:
	free((void *) ld_path);
	close(s);
	close(d);
	if(handle) dlclose(handle);
}

static void *dlsym_wrap(void *handle, const char *sym){
    void *const ret = dlsym(handle, sym);
    if(!ret) fprintf(stderr, "Error dlsym(%p, \"%s\"):%s\n", handle, sym, dlerror());
    return ret;
}

int tlmu_load(struct tlmu *q, const char *soname)
{
	char *libname;
	char *logname;
	char *sobasename;
	char *socopy;
	int err = 0;
	int n;

	mkdir(".tlmu", S_IRWXU | S_IRWXG);

	socopy = strdup(soname);
	sobasename = basename(socopy);

	n = asprintf(&libname, ".tlmu/%s-%s", sobasename, q->name);
	if (n < 0)
		return 1;

	copylib(soname, libname);

	q->dl_handle = dlopen(libname, RTLD_LOCAL | RTLD_DEEPBIND | RTLD_NOW);
    if(!q->dl_handle){
        fprintf(stderr, "dlopen(%s):%s\n", libname, dlerror());
    }
//	err = unlink(libname);
	if (err) {
		perror(libname);
	}
	free(libname);
	if (!q->dl_handle) {
		free(socopy);
		return 1;
	}

	q->main = dlsym_wrap(q->dl_handle, "vl_main");
	q->tlm_set_log_filename = dlsym_wrap(q->dl_handle, "qemu_set_log_filename");
	q->tlm_image_load_base = dlsym_wrap(q->dl_handle, "tlm_image_load_base");
	q->tlm_image_load_size = dlsym_wrap(q->dl_handle, "tlm_image_load_size");
	q->tlm_map_ram = dlsym_wrap(q->dl_handle, "tlm_map_ram");
	q->tlm_opaque = dlsym_wrap(q->dl_handle, "tlm_opaque");
	q->tlm_notify_event = dlsym_wrap(q->dl_handle, "tlm_notify_event");
	q->tlm_sync = dlsym_wrap(q->dl_handle, "tlm_sync");
	q->tlm_sync_period_ns = dlsym_wrap(q->dl_handle, "tlm_sync_period_ns");
	q->tlm_boot_state = dlsym_wrap(q->dl_handle, "tlm_boot_state");
	q->tlm_bus_access_cb = dlsym_wrap(q->dl_handle, "tlm_bus_access_cb");
	q->tlm_bus_access_dbg_cb = dlsym_wrap(q->dl_handle, "tlm_bus_access_dbg_cb");
	q->tlm_bus_access = dlsym_wrap(q->dl_handle, "tlm_bus_access");
	q->tlm_bus_access_dbg = dlsym_wrap(q->dl_handle, "tlm_bus_access_dbg");
	q->tlm_get_dmi_ptr_cb = dlsym_wrap(q->dl_handle, "tlm_get_dmi_ptr_cb");
	q->tlm_get_dmi_ptr = dlsym_wrap(q->dl_handle, "tlm_get_dmi_ptr");
    q->qemu_system_shutdown_request = dlsym_wrap(q->dl_handle, "qemu_system_shutdown_request");
	if (!q->main
		|| !q->tlm_map_ram
		|| !q->tlm_set_log_filename
		|| !q->tlm_image_load_base
		|| !q->tlm_image_load_size
		|| !q->tlm_opaque
		|| !q->tlm_notify_event
		|| !q->tlm_sync
		|| !q->tlm_sync_period_ns
		|| !q->tlm_boot_state
		|| !q->tlm_bus_access_cb
		|| !q->tlm_bus_access_dbg_cb
		|| !q->tlm_bus_access
		|| !q->tlm_bus_access_dbg
		|| !q->tlm_get_dmi_ptr_cb
		|| !q->tlm_get_dmi_ptr
        || !q->qemu_system_shutdown_request) {
		dlclose(q->dl_handle);
		free(socopy);
		return 1;
	}

	n = asprintf(&logname, ".tlmu/%s-%s.log", sobasename, q->name);
	tlmu_set_log_filename(q, logname);
	free(logname);

	free(socopy);
	return 0;
}

void tlmu_notify_event(struct tlmu *q, enum tlmu_event ev, void *d)
{
	q->tlm_notify_event(ev, d);
}

void tlmu_set_opaque(struct tlmu *q, void *o)
{
	*q->tlm_opaque = o;
}

void tlmu_set_bus_access_cb(struct tlmu *q,
		int (*access)(void *, int64_t, int, uint64_t, void *, int))
{
	*q->tlm_bus_access_cb = access;
}

void tlmu_set_bus_access_dbg_cb(struct tlmu *q,
		void (*access)(void *, int64_t, int, uint64_t, void *, int))
{
	*q->tlm_bus_access_dbg_cb = access;
}

void tlmu_set_bus_get_dmi_ptr_cb(struct tlmu *q,
			void (*dmi)(void *, uint64_t, struct tlmu_dmi*))
{
	*q->tlm_get_dmi_ptr_cb = dmi;
}

void tlmu_set_sync_period_ns(struct tlmu *q, uint64_t period_ns)
{
	*q->tlm_sync_period_ns = period_ns;
}

void tlmu_set_boot_state(struct tlmu *q, int v)
{
	*q->tlm_boot_state = v;
}

void tlmu_set_sync_cb(struct tlmu *q, void (*cb)(void *, int64_t))
{
	*q->tlm_sync = cb;
}

int tlmu_bus_access(struct tlmu *q, int rw, uint64_t addr, void *data, int len)
{
	return q->tlm_bus_access(rw, addr, data, len);
}

void tlmu_bus_access_dbg(struct tlmu *q,
			int rw, uint64_t addr, void *data, int len)
{
	q->tlm_bus_access_dbg(rw, addr, data, len);
}

int tlmu_get_dmi_ptr(struct tlmu *q, struct tlmu_dmi *dmi)
{
	return q->tlm_get_dmi_ptr(dmi);
}

void tlmu_map_ram(struct tlmu *q, const char *name,
		uint64_t addr, uint64_t size, int rw)
{
	q->tlm_map_ram(name, addr, size, rw, 0);
}


void tlmu_map_ram_nosync(struct tlmu *q, const char *name,
		uint64_t addr, uint64_t size, int rw)
{
	q->tlm_map_ram(name, addr, size, rw, 1);
}


void tlmu_set_log_filename(struct tlmu *q, const char *f)
{
	q->tlm_set_log_filename(f);
}

void tlmu_set_image_load_params(struct tlmu *q, uint64_t base, uint64_t size)
{
	*q->tlm_image_load_base = base;
	*q->tlm_image_load_size = size;
}

void tlmu_append_arg(struct tlmu *t, const char *arg)
{
	int i = 0;

	while (t->argv[i])
		i++;

	if (i + 1 >= (sizeof t->argv / sizeof t->argv[0])) {
		assert(0);
	}

	t->argv[i] = arg;
	t->argv[i + 1] = NULL;
}

void tlmu_run(struct tlmu *t)
{
	int argc = 0;

	while (t->argv[argc])
		argc++;

    t->main(0, 1, 1, argc, t->argv, NULL);
}

void tlmu_exit(struct tlmu *t)
{
    (*(t->qemu_system_shutdown_request))();
}
