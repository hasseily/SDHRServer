#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>


/*
 * modeset_buf and modeset_dev stay mostly the same. But 6 new fields are added
 * to modeset_dev: r, g, b, r_up, g_up, b_up. They are used to compute the
 * current color that is drawn on this output device. You can ignore them as
 * they aren't important for this example.
 * The modeset-double-buffered.c example used exactly the same fields but as
 * local variables in modeset_draw().
 *
 * The \pflip_pending variable is true when a page-flip is currently pending,
 * that is, the kernel will flip buffers on the next vertical blank. The
 * \cleanup variable is true if the device is currently cleaned up and no more
 * pageflips should be scheduled. They are used to synchronize the cleanup
 * routines.
 */

struct modeset_buf {
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t size;
	uint32_t handle;
	uint8_t* map;
	uint32_t fb;
};

struct modeset_dev {
	struct modeset_dev* next;

	unsigned int front_buf;
	struct modeset_buf bufs[2];

	drmModeModeInfo mode;
	uint32_t conn;
	uint32_t crtc;
	drmModeCrtc* saved_crtc;

	bool pflip_pending;
	bool cleanup;
};

static struct modeset_dev* modeset_list = NULL;
static int modeset_fd;

static void modeset_page_flip_event(int fd, unsigned int frame,
	unsigned int sec, unsigned int usec, void* data);

static int modeset_find_crtc(int fd, drmModeRes* res, drmModeConnector* conn,
	struct modeset_dev* dev);
static int modeset_create_fb(int fd, struct modeset_buf* buf);
static void modeset_destroy_fb(int fd, struct modeset_buf* buf);
static int modeset_setup_dev(int fd, drmModeRes* res, drmModeConnector* conn,
	struct modeset_dev* dev);
static int modeset_open(int* out, const char* node);
static int modeset_prepare(int fd);
static void modeset_draw(int fd);
static void modeset_draw_dev(int fd, struct modeset_dev* dev);
static void modeset_cleanup();