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

#ifndef WESTON_COMPOSITOR_DECON_H
#define WESTON_COMPOSITOR_DECON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <libweston/libweston.h>

#define WESTON_DECON_BACKEND_CONFIG_VERSION 1

/**
 * Configuration options for the Samsung DECON direct display backend.
 *
 * Controls target framebuffer node, ION memory device, panel geometry,
 * refresh rate, and DECON overlay window selection.
 */
struct weston_decon_backend_config {
	struct weston_backend_config base;

	/** Selected renderer type (AUTO, PIXMAN, GL) */
	enum weston_renderer_type renderer_type;

	/** Path to DECON framebuffer device node (default "/dev/graphics/fb0") */
	const char *fb_dev;

	/** Path to Samsung ION allocator node (default "/dev/ion") */
	const char *ion_dev;

	/** Panel pixel width (default 1080) */
	uint32_t width;

	/** Panel pixel height (default 2340) */
	uint32_t height;

	/** Panel refresh rate in mHz (default 60000 = 60.000 Hz) */
	uint32_t refresh_mhz;

	/** Target DECON window index (default 3, routed to IDMA channel 0) */
	uint32_t window_index;

	/** If true, mock hardware ioctls and use software frame timing for tests */
	bool mock_mode;
};

#ifdef __cplusplus
}
#endif

#endif /* WESTON_COMPOSITOR_DECON_H */
