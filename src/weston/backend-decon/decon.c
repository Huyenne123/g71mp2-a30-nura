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

#include "decon_internal.h"
#include <shared/timespec-util.h>

static const uint32_t decon_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_RGBX8888,
};

static void
decon_output_destroy(struct weston_output *base);

static int
decon_output_disable(struct weston_output *base);

static void
decon_backend_destroy(struct weston_backend *backend);

/* -------------------------------------------------------------------------
 * Presentation & Retire Fence Synchronization Loop
 * ------------------------------------------------------------------------- */

static struct timespec g_t_last_fence = { 0, 0 };
static double g_last_render_ms = 0.0;
static double g_last_present_ms = 0.0;

static int
decon_fence_handler(int fd, uint32_t mask, void *data)
{
	struct decon_output *output = data;
	struct timespec ts, t_now;
	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC;

	(void)fd;

	if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
		weston_log("[-] decon: Retire fence error or hangup on fd %d (mask 0x%x)\n",
			   output->current_retire_fence, mask);
		flags = WP_PRESENTATION_FEEDBACK_INVALID;
	}

	if (output->fence_source) {
		wl_event_source_remove(output->fence_source);
		output->fence_source = NULL;
	}

	if (output->current_retire_fence >= 0) {
		close(output->current_retire_fence);
		output->current_retire_fence = -1;
	}

	clock_gettime(CLOCK_MONOTONIC, &t_now);
	double fence_interval_ms = 0.0;
	if (g_t_last_fence.tv_sec > 0) {
		fence_interval_ms = (t_now.tv_sec - g_t_last_fence.tv_sec) * 1000.0 +
				    (t_now.tv_nsec - g_t_last_fence.tv_nsec) / 1000000.0;
	}
	g_t_last_fence = t_now;

	weston_compositor_read_presentation_clock(output->base.compositor, &ts);
	weston_output_finish_frame(&output->base, &ts, flags);

	static uint32_t fence_count = 0;
	fence_count++;
	if (fence_count <= 5 || fence_count % 60 == 0) {
		weston_log("[+] decon: Retire fence #%u signaled (interval: %.2f ms, render: %.2f ms, present: %.2f ms, flags 0x%x)\n",
			   fence_count, fence_interval_ms, g_last_render_ms, g_last_present_ms, flags);
	}

	/* Toggle ping-pong active buffer index */
	output->active_idx ^= 1;
	if (output->engine)
		output->engine->active_idx = output->active_idx;

	return 0;
}

static int
finish_frame_timer_handler(void *data)
{
	struct decon_output *output = data;
	struct timespec ts;

	weston_compositor_read_presentation_clock(output->base.compositor, &ts);
	weston_output_finish_frame(&output->base, &ts, WP_PRESENTATION_FEEDBACK_INVALID);

	output->active_idx ^= 1;
	if (output->engine)
		output->engine->active_idx = output->active_idx;

	return 0;
}

static int
decon_output_start_repaint_loop(struct weston_output *base)
{
	struct timespec ts;

	weston_compositor_read_presentation_clock(base->compositor, &ts);
	weston_output_finish_frame(base, &ts, WP_PRESENTATION_FEEDBACK_INVALID);

	return 0;
}

static int
decon_output_repaint(struct weston_output *base)
{
	struct decon_output *output = to_decon_output(base);
	struct weston_compositor *ec;
	pixman_region32_t damage;
	int retire_fence = -1;

	assert(output);
	ec = output->base.compositor;

	pixman_region32_init(&damage);
	weston_output_flush_damage_for_primary_plane(base, &damage);

	struct timespec t0, t1, t2;
	clock_gettime(CLOCK_MONOTONIC, &t0);

	/* Repaint into active swapchain renderbuffer */
	ec->renderer->repaint_output(base, &damage,
				     output->renderbuffers[output->active_idx]);
	pixman_region32_fini(&damage);
	clock_gettime(CLOCK_MONOTONIC, &t1);

	/* Guard against any stale in-flight retire fence before new submission */
	if (output->fence_source) {
		weston_log("[-] decon: Stale retire fence detected during repaint, cleaning up\n");
		wl_event_source_remove(output->fence_source);
		output->fence_source = NULL;
	}
	if (output->current_retire_fence >= 0) {
		close(output->current_retire_fence);
		output->current_retire_fence = -1;
	}

	/* Present buffer to Samsung DECON Window 3 */
	if (output->engine && !output->backend->config.mock_mode) {
		int ret = decon_backend_present(output->engine, &retire_fence);
		if (ret < 0) {
			weston_log("[-] decon: S3CFB_WIN_CONFIG ioctl failed: %s\n",
				   strerror(errno));
			return -1;
		}

		clock_gettime(CLOCK_MONOTONIC, &t2);
		g_last_render_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1000000.0;
		g_last_present_ms = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_nsec - t1.tv_nsec) / 1000000.0;

		static uint32_t repaint_count = 0;
		repaint_count++;
		if (repaint_count <= 5 || repaint_count % 60 == 0) {
			weston_log("[+] decon: Hardware repaint #%u presented on buffer %u (fence fd %d, render: %.2f ms, present: %.2f ms)\n",
				   repaint_count, output->active_idx, retire_fence, g_last_render_ms, g_last_present_ms);
		}
	}

	/* Register retire fence on Wayland event loop */
	if (retire_fence >= 0) {
		struct wl_event_loop *loop =
			wl_display_get_event_loop(ec->wl_display);

		output->current_retire_fence = retire_fence;
		output->fence_source = wl_event_loop_add_fd(
			loop,
			retire_fence,
			WL_EVENT_READABLE,
			decon_fence_handler,
			output
		);

		if (!output->fence_source) {
			weston_log("[-] decon: Failed to attach retire fence to event loop\n");
			close(retire_fence);
			output->current_retire_fence = -1;
			wl_event_source_timer_update(output->finish_frame_timer, 16);
		}
	} else {
		/* Fallback software timer for mock mode or software scanout */
		int delay_msec = millihz_to_nsec(output->mode.refresh) / 1000000;
		if (delay_msec < 1)
			delay_msec = 16;
		wl_event_source_timer_update(output->finish_frame_timer, delay_msec);
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * Output Lifecycle Operations
 * ------------------------------------------------------------------------- */

static int
decon_output_enable(struct weston_output *base)
{
	struct decon_output *output = to_decon_output(base);
	struct weston_decon_backend *b;
	struct wl_event_loop *loop;
	struct weston_head *head;

	assert(output);
	b = output->backend;

	if (output->base.enabled)
		return 0;

	output->base.start_repaint_loop = decon_output_start_repaint_loop;
	output->base.repaint = decon_output_repaint;

	loop = wl_display_get_event_loop(b->compositor->wl_display);
	output->finish_frame_timer =
		wl_event_loop_add_timer(loop, finish_frame_timer_handler, output);
	if (!output->finish_frame_timer) {
		weston_log("[-] decon: Failed to register finish frame timer\n");
		return -1;
	}

	/* Initialize underlying hardware DECON engine and ION swapchain */
	if (!b->config.mock_mode) {
		struct decon_backend_config eng_cfg;
		memset(&eng_cfg, 0, sizeof(eng_cfg));
		eng_cfg.width = b->config.width;
		eng_cfg.height = b->config.height;
		eng_cfg.stride = b->config.width * 4;
		eng_cfg.buffer_size = b->config.width * b->config.height * 4;
		eng_cfg.window_index = b->config.window_index;
		eng_cfg.fb_node = b->config.fb_dev;
		eng_cfg.ion_node = b->config.ion_dev;

		if (!output->engine) {
			output->engine = decon_backend_init(&eng_cfg);
			if (!output->engine) {
				weston_log("[-] decon: Failed to open %s and %s\n",
					   b->config.fb_dev, b->config.ion_dev);
				wl_event_source_remove(output->finish_frame_timer);
				output->finish_frame_timer = NULL;
				return -1;
			}
		}

		if (!output->engine->buffers[0].cpu_mmap_ptr) {
			int ret = decon_backend_setup_swapchain_ion(output->engine, 1 /* map_cpu */);
			if (ret < 0) {
				weston_log("[-] decon: Failed to allocate ION swapchain (err %d)\n", ret);
				decon_backend_teardown(output->engine);
				output->engine = NULL;
				wl_event_source_remove(output->finish_frame_timer);
				output->finish_frame_timer = NULL;
				return -1;
			}
		}
	}

	/* Create Pixman renderbuffers */
	if (b->compositor->renderer->type == WESTON_RENDERER_PIXMAN) {
		const struct pixman_renderer_output_options options = {
			.use_shadow = false,
			.fb_size = {
				.width = output->mode.width,
				.height = output->mode.height
			},
			.format = pixel_format_get_info(DRM_FORMAT_XRGB8888)
		};

		const struct pixman_renderer_interface *pixman =
			b->compositor->renderer->pixman;

		if (pixman->output_create(&output->base, &options) < 0) {
			weston_log("[-] decon: Failed to initialize pixman output state\n");
			if (output->engine) {
				decon_backend_teardown(output->engine);
				output->engine = NULL;
			}
			wl_event_source_remove(output->finish_frame_timer);
			output->finish_frame_timer = NULL;
			return -1;
		}

		for (int i = 0; i < 2; i++) {
			void *map_ptr = output->engine ?
				output->engine->buffers[i].cpu_mmap_ptr : NULL;

			if (map_ptr) {
				output->renderbuffers[i] = pixman->create_image_from_ptr(
					&output->base,
					options.format,
					output->mode.width,
					output->mode.height,
					map_ptr,
					output->mode.width * 4
				);
			} else {
				output->renderbuffers[i] = pixman->create_image(
					&output->base,
					options.format,
					output->mode.width,
					output->mode.height
				);
			}

			if (!output->renderbuffers[i]) {
				weston_log("[-] decon: Failed to create renderbuffer %d\n", i);
				decon_output_disable(&output->base);
				return -1;
			}
		}
	}

	wl_list_for_each(head, &output->base.head_list, output_link) {
		weston_head_set_monitor_strings(head, "Samsung", "EA8076 Super AMOLED", "DECON-0");
		weston_head_set_physical_size(head, 68, 147);
	}

	output->active_idx = 0;
	output->current_retire_fence = -1;
	return 0;
}

static int
decon_output_disable(struct weston_output *base)
{
	struct decon_output *output = to_decon_output(base);
	struct weston_compositor *ec;

	assert(output);

	if (!output->base.enabled)
		return 0;

	ec = output->base.compositor;

	if (output->fence_source) {
		wl_event_source_remove(output->fence_source);
		output->fence_source = NULL;
	}

	if (output->current_retire_fence >= 0) {
		close(output->current_retire_fence);
		output->current_retire_fence = -1;
	}

	if (output->finish_frame_timer) {
		wl_event_source_remove(output->finish_frame_timer);
		output->finish_frame_timer = NULL;
	}

	for (int i = 0; i < 2; i++) {
		if (output->renderbuffers[i]) {
			weston_renderbuffer_unref(output->renderbuffers[i]);
			output->renderbuffers[i] = NULL;
		}
	}

	if (ec->renderer && ec->renderer->pixman)
		ec->renderer->pixman->output_destroy(&output->base);

	if (output->engine) {
		decon_backend_teardown(output->engine);
		output->engine = NULL;
	}

	return 0;
}

static void
decon_output_destroy(struct weston_output *base)
{
	struct decon_output *output = to_decon_output(base);

	assert(output);

	decon_output_disable(&output->base);
	weston_output_release(&output->base);
	free(output);
}

static int
decon_output_attach_head(struct weston_output *output_base,
			 struct weston_head *head)
{
	(void)output_base;
	(void)head;
	return 0;
}

static struct weston_output *
decon_output_create(struct weston_backend *backend, const char *name)
{
	struct weston_decon_backend *b = to_weston_decon_backend(backend);
	struct weston_compositor *compositor = b->compositor;
	struct decon_output *output;

	assert(name);

	output = zalloc(sizeof *output);
	if (!output)
		return NULL;

	weston_output_init(&output->base, compositor, name);

	output->base.destroy = decon_output_destroy;
	output->base.disable = decon_output_disable;
	output->base.enable = decon_output_enable;
	output->base.attach_head = decon_output_attach_head;

	output->backend = b;
	output->current_retire_fence = -1;

	/* Initialize fixed AMOLED display mode (1080x2340@60Hz) */
	output->mode.flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
	output->mode.width = b->config.width;
	output->mode.height = b->config.height;
	output->mode.refresh = b->config.refresh_mhz;
	wl_list_insert(&output->base.mode_list, &output->mode.link);

	output->base.native_scale = 1;
	output->base.current_scale = 1;
	output->base.original_scale = 1;
	output->base.current_mode = &output->mode;
	output->base.native_mode = &output->mode;
	output->base.original_mode = &output->mode;

	weston_compositor_add_pending_output(&output->base, compositor);

	return &output->base;
}

/* -------------------------------------------------------------------------
 * Head & Monitor Registration
 * ------------------------------------------------------------------------- */

static int
decon_head_create(struct weston_decon_backend *backend, const char *name)
{
	struct decon_head *head;

	assert(name);

	head = zalloc(sizeof *head);
	if (!head)
		return -1;

	weston_head_init(&head->base, name);
	head->base.backend = &backend->base;
	head->backend = backend;

	weston_head_set_connection_status(&head->base, true);
	weston_head_set_internal(&head->base);
	weston_head_set_monitor_strings(&head->base, "Samsung", "EA8076 Super AMOLED", "DECON-0");
	weston_head_set_physical_size(&head->base, 68, 147);
	weston_head_set_subpixel(&head->base, WL_OUTPUT_SUBPIXEL_NONE);

	weston_compositor_add_head(backend->compositor, &head->base);
	backend->head = head;

	return 0;
}

static void
decon_head_destroy(struct weston_head *base)
{
	struct decon_head *head = to_decon_head(base);

	assert(head);

	weston_head_release(&head->base);
	free(head);
}

/* -------------------------------------------------------------------------
 * Backend Module Lifecycle
 * ------------------------------------------------------------------------- */

static void
decon_backend_destroy(struct weston_backend *backend)
{
	struct weston_decon_backend *b = to_weston_decon_backend(backend);
	struct weston_compositor *ec = b->compositor;
	struct weston_head *base, *next;

	wl_list_remove(&b->base.link);

	wl_list_for_each_safe(base, next, &ec->head_list, compositor_link) {
		if (base->backend == &b->base)
			decon_head_destroy(base);
	}

#ifdef BUILD_DECON_INPUT
	if (b->udev) {
		udev_input_destroy(&b->input);
		udev_unref(b->udev);
		b->udev = NULL;
	}
#endif

	if (b->formats) {
		free(b->formats);
		b->formats = NULL;
	}

	free(b);
}

#ifdef BUILD_DECON_INPUT
#include <fcntl.h>
/* Direct embedded device opener when running without a session launcher / seatd */
WL_EXPORT int
weston_launcher_open(struct weston_launcher *launcher,
		     const char *path, int flags)
{
	(void)launcher;
	return open(path, flags);
}

WL_EXPORT void
weston_launcher_close(struct weston_launcher *launcher, int fd)
{
	(void)launcher;
	close(fd);
}
#endif

static struct weston_decon_backend *
decon_backend_create(struct weston_compositor *compositor,
		     struct weston_decon_backend_config *config)
{
	struct weston_decon_backend *b;
	int ret;

	b = zalloc(sizeof *b);
	if (!b)
		return NULL;

	b->compositor = compositor;
	memcpy(&b->config, config, sizeof(*config));
	wl_list_insert(&compositor->backend_list, &b->base.link);

	b->base.supported_presentation_clocks =
		WESTON_PRESENTATION_CLOCKS_SOFTWARE | (1 << CLOCK_MONOTONIC);
	b->base.destroy = decon_backend_destroy;
	b->base.create_output = decon_output_create;

	b->formats_count = ARRAY_LENGTH(decon_formats);
	b->formats = pixel_format_get_array(decon_formats, b->formats_count);

	/* Initialize compositor renderer if not already active */
	if (!compositor->renderer) {
		switch (config->renderer_type) {
		case WESTON_RENDERER_PIXMAN:
		case WESTON_RENDERER_AUTO:
			ret = weston_compositor_init_renderer(compositor,
							      WESTON_RENDERER_PIXMAN,
							      NULL);
			break;
		case WESTON_RENDERER_GL:
			weston_log("[-] decon: GL renderer requested. Note: proprietary GLES requires Bionic/libhybris.\n");
			ret = weston_compositor_init_renderer(compositor,
							      WESTON_RENDERER_GL,
							      NULL);
			break;
		case WESTON_RENDERER_NOOP:
			ret = noop_renderer_init(compositor);
			break;
		default:
			weston_log("[-] decon: Unsupported renderer type %d\n",
				   config->renderer_type);
			ret = -1;
			break;
		}

		if (ret < 0) {
			weston_log("[-] decon: Failed to initialize renderer\n");
			goto err_free;
		}
	}

#ifdef BUILD_DECON_INPUT
	b->udev = udev_new();
	if (b->udev) {
		if (udev_input_init(&b->input, compositor, b->udev,
				    "seat0", NULL) < 0) {
			weston_log("[-] decon: Failed to initialize udev input on seat0\n");
		} else {
			weston_log("[+] decon: Udev input successfully initialized on seat0\n");
		}
	} else {
		weston_log("[-] decon: Failed to create udev context\n");
	}
#endif

	/* Register primary display head */
	if (decon_head_create(b, DECON_DEFAULT_OUTPUT_NAME) < 0) {
		weston_log("[-] decon: Failed to register display head\n");
		goto err_free;
	}

	return b;

err_free:
	if (b->formats)
		free(b->formats);
	wl_list_remove(&b->base.link);
	free(b);
	return NULL;
}

static void
config_init_to_defaults(struct weston_decon_backend_config *config)
{
	config->renderer_type = WESTON_RENDERER_PIXMAN;
	config->fb_dev = "/dev/graphics/fb0";
	config->ion_dev = "/dev/ion";
	config->width = DECON_DEFAULT_WIDTH;
	config->height = DECON_DEFAULT_HEIGHT;
	config->refresh_mhz = DECON_DEFAULT_REFRESH_MHZ;
	config->window_index = DECON_PRIMARY_WINDOW;
	config->mock_mode = false;
}

WL_EXPORT int
weston_backend_init(struct weston_compositor *compositor,
		    struct weston_backend_config *config_base)
{
	struct weston_decon_backend *b;
	struct weston_decon_backend_config config;

	memset(&config, 0, sizeof(config));
	config_init_to_defaults(&config);

	if (config_base != NULL) {
		if (config_base->struct_version != WESTON_DECON_BACKEND_CONFIG_VERSION ||
		    config_base->struct_size > sizeof(struct weston_decon_backend_config)) {
			weston_log("[-] decon: Backend config structure is invalid (version %u, size %zu)\n",
				   config_base->struct_version, (size_t)config_base->struct_size);
			return -1;
		}
		memcpy(&config, config_base, config_base->struct_size);
	}

	b = decon_backend_create(compositor, &config);
	if (!b)
		return -1;

	return 0;
}
