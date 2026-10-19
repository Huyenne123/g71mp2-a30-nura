/*
 * Copyright © 2026 Samsung Exynos 7904 / Mali-G71 Open Graphics Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef DECON_INTERNAL_H
#define DECON_INTERNAL_H

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <libweston/libweston.h>
#include "backend.h"
#include "pixel-formats.h"
#include "pixman-renderer.h"
#include "shared/helpers.h"
#include "shared/weston-drm-fourcc.h"

#include <libweston/backend-decon.h>
#include "decon_backend.h"

#ifdef BUILD_DECON_INPUT
#include <libudev.h>
#include "libinput-seat.h"
#endif

#ifdef ENABLE_DECON_GL
#include <hybris/hwcomposerwindow/hwcomposer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "renderer-gl/gl-renderer.h"

typedef struct native_handle {
	int version;
	int numFds;
	int numInts;
	int data[0];
} native_handle_t;

struct android_native_base_t {
	int magic;
	int version;
	void *reserved[4];
	void (*incRef)(struct android_native_base_t *base);
	void (*decRef)(struct android_native_base_t *base);
};

struct ANativeWindowBuffer {
	struct android_native_base_t common;
	int width;
	int height;
	int stride;
	int format;
	int usage_deprecated;
	uintptr_t layerCount;
	void *reserved[1];
	const native_handle_t *handle;
	uint64_t usage;
};
#endif

#define DECON_DEFAULT_OUTPUT_NAME "DECON-0"
#define DECON_DEFAULT_REFRESH_MHZ 60000

#ifndef WP_PRESENTATION_FEEDBACK_KIND_VSYNC
#define WP_PRESENTATION_FEEDBACK_KIND_VSYNC 0x1
#endif
#ifndef WP_PRESENTATION_FEEDBACK_INVALID
#define WP_PRESENTATION_FEEDBACK_INVALID 0
#endif

struct decon_head {
	struct weston_head base;
	struct weston_decon_backend *backend;
};

struct weston_decon_backend {
	struct weston_backend base;
	struct weston_compositor *compositor;
	struct weston_decon_backend_config config;
	struct decon_head *head;
	struct wl_listener output_created_listener;
	struct wl_list output_list;
	const struct pixel_format_info **formats;
	unsigned int formats_count;
	int fb_fd;
#ifdef BUILD_DECON_INPUT
	struct udev *udev;
	struct udev_input input;
#endif
};

static inline struct decon_head *
to_decon_head(struct weston_head *base)
{
	return container_of(base, struct decon_head, base);
}

struct decon_output {
	struct weston_output base;
	struct weston_decon_backend *backend;
	struct weston_mode mode;
	struct decon_backend *engine;

	struct weston_renderbuffer *renderbuffers[2];
	uint32_t active_idx;

	struct wl_event_source *finish_frame_timer;
	struct wl_event_source *fence_source;
	int current_retire_fence;

#ifdef ENABLE_DECON_GL
	struct ANativeWindow *native_window;
#endif
	bool is_gl_renderer;
	uint32_t gl_repaint_count;
	uint32_t pixman_repaint_count;
};

static inline struct weston_decon_backend *
to_weston_decon_backend(struct weston_backend *backend)
{
	return container_of(backend, struct weston_decon_backend, base);
}

static inline struct decon_output *
to_decon_output(struct weston_output *output)
{
	return container_of(output, struct decon_output, base);
}

#endif /* DECON_INTERNAL_H */
