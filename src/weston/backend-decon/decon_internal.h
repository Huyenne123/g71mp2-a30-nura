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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>

#include <libweston/libweston.h>
#include <libweston/backend.h>
#include <libweston/pixel-formats.h>
#include <libweston/pixman-renderer.h>
#include <shared/helpers.h>
#include <shared/weston-drm-fourcc.h>

#include "backend-decon.h"
#include "../../compositor/decon_backend.h"

#define DECON_DEFAULT_OUTPUT_NAME "DECON-0"
#define DECON_DEFAULT_REFRESH_MHZ 60000

#ifndef WP_PRESENTATION_FEEDBACK_KIND_VSYNC
#define WP_PRESENTATION_FEEDBACK_KIND_VSYNC 0x1
#endif
#ifndef WP_PRESENTATION_FEEDBACK_INVALID
#define WP_PRESENTATION_FEEDBACK_INVALID 0
#endif

struct weston_decon_backend {
	struct weston_backend base;
	struct weston_compositor *compositor;
	struct weston_decon_backend_config config;

	const struct pixel_format_info **formats;
	unsigned int formats_count;

	struct decon_head *head;
	struct decon_output *output;
};

struct decon_head {
	struct weston_head base;
	struct weston_decon_backend *backend;
};

struct decon_output {
	struct weston_output base;
	struct weston_decon_backend *backend;

	struct weston_mode mode;
	struct wl_event_source *finish_frame_timer;
	struct wl_event_source *fence_source;

	/* Underlying hardware presentation engine */
	struct decon_backend *engine;

	/* Weston renderbuffers (backed by CPU mmap of ION DMA-BUFs) */
	struct weston_renderbuffer *renderbuffers[2];
	uint32_t active_idx;

	/* In-flight retire fence descriptor */
	int current_retire_fence;
};

static inline struct weston_decon_backend *
to_weston_decon_backend(struct weston_backend *base)
{
	return container_of(base, struct weston_decon_backend, base);
}

static inline struct decon_head *
to_decon_head(struct weston_head *base)
{
	return container_of(base, struct decon_head, base);
}

static inline struct decon_output *
to_decon_output(struct weston_output *base)
{
	return container_of(base, struct decon_output, base);
}

#endif /* DECON_INTERNAL_H */
