#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <png.h>
#include <linux/videodev2.h>

/* Compile Options */
//#define VERBOSE 1	/* Print more detailed info */

//#define BACKGROUND_PLANE 1	/* Create a new Background frame buffer on the Primary Plane (do not use /dev/fb0) */

struct framebuffer_object {
	uint32_t width;
	uint32_t height;
	uint32_t alpha;	// Use alpha channel
	uint32_t x;	// destination offset
	uint32_t y;	// destination offset
	uint32_t pitch;	// Number of bytes per row. buffers might need to be on 8 or 16 byte boundaries
	uint32_t handle;
	uint32_t size;	// Total buffer size (pitch x height)
	uint32_t *vaddr;
	uint32_t fb_id;
};

struct framebuffer_object fb_background;
struct framebuffer_object fb_overlay;
struct framebuffer_object fb_overlay;

int png_width, png_height, png_row_bytes;
png_byte png_color_type;
png_byte png_bit_depth;
png_bytep *row_pointers = NULL;

void read_png_file(char *filename) {
	FILE *fp = fopen(filename, "rb");

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if(!png)
		exit(1);

	png_infop info = png_create_info_struct(png);
	if(!info)
		exit(1);

	if(setjmp(png_jmpbuf(png)))
		exit(1);

	png_init_io(png, fp);

	png_read_info(png, info);

	png_width      = png_get_image_width(png, info);
	png_height     = png_get_image_height(png, info);
	png_color_type = png_get_color_type(png, info);
	png_bit_depth  = png_get_bit_depth(png, info);
	png_row_bytes  = png_get_rowbytes(png, info);

#ifdef VERBOSE
	printf("png_width = %d\n", png_width);
	printf("png_height = %d\n", png_height);
	printf("png_bit_depth = %d\n", png_bit_depth);
	printf("png_row_bytes = %d\n", png_row_bytes);
#endif

	// Read any color_type into 8bit depth, RGBA format.
	// See http://www.libpng.org/pub/png/libpng-manual.txt
	if(png_bit_depth == 16)
		png_set_strip_16(png);

	if(png_color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);

	// PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
	if(png_color_type == PNG_COLOR_TYPE_GRAY && png_bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);

	if(png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);

	// These color_type don't have an alpha channel then fill it with 0xff.
	if(png_color_type == PNG_COLOR_TYPE_RGB ||
	   png_color_type == PNG_COLOR_TYPE_GRAY ||
	   png_color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

	if(png_color_type == PNG_COLOR_TYPE_GRAY ||
	   png_color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);

	// RGBA -> ARGB
	// Actually, this seems backwards than the documentation...so comment it out
//	if (png_color_type == PNG_COLOR_TYPE_RGB_ALPHA)
//		png_set_swap_alpha(png);

	// swap 'R' and 'B' (G stays the same)
	// RGB -> BGR
	// PNG files store 3-color pixels in red, green, blue order. This code changes the storage
	// of the pixels to blue, green, red
	if (png_color_type == PNG_COLOR_TYPE_RGB || png_color_type == PNG_COLOR_TYPE_RGB_ALPHA)
		png_set_bgr(png);

	png_read_update_info(png, info);

	if (row_pointers)
		exit(1);

	row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * png_height);
	for(int y = 0; y < png_height; y++) {
		row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png,info));
	}

	png_read_image(png, row_pointers);

	fclose(fp);

	png_destroy_read_struct(&png, &info, NULL);
}


static int create_fb(int fd, struct framebuffer_object *fb)
{
	struct drm_mode_create_dumb create = {};
	struct drm_mode_map_dumb map = {};
	int depth;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	uint32_t fmt;

	// Create a dumb-buffer memory
	create.width = fb->width;
	create.height = fb->height;
	create.bpp = 32;
	drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);

	// Add the new framebuffer
	fb->pitch = create.pitch;
	fb->size = create.size;
	fb->handle = create.handle;
	if (fb->alpha)
		depth = 32;
	else
		depth = 24;

	if (fb->alpha)
		fmt = V4L2_PIX_FMT_ABGR32;
	else
		fmt = V4L2_PIX_FMT_XBGR32;

	handles[0] = fb->handle;
	pitches[0] = fb->pitch;

	drmModeAddFB2(fd, fb->width, fb->height,
		fmt, /* pixel_format */
		handles, pitches, offsets,
		&fb->fb_id,
		0);

	// Map the dumb-buffer to userspace
	map.handle = create.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);

	fb->vaddr = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);

	return 0;
}



int main(int argc, char **argv)
{
	int fd;
	drmModeCrtc *crtc;
	drmModeConnector *conn;
	drmModeRes *res;
	drmModePlaneRes *plane_res;
	uint32_t conn_id;
	uint32_t crtc_id;
	//uint32_t plane_id;
	uint32_t screen_width;
	uint32_t screen_height;
	
	uint32_t plane_id_primary;
	uint32_t plane_id_overlay;
	char *png_filename = "renesas_logo.png";

	if (argc == 1) {
		printf("\nUsage: [png filename]\n\n");
		return -1;
	}
	png_filename = argv[1];

	/* Open handle to DRM driver */
	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (-1 == fd) {
		printf("no drm device found,fd = :%d!\n", fd);
		return -1;
	}
	
	/* Read PNG into buffers */
	read_png_file(png_filename);
	printf("png image = %d x %d\n", png_width, png_height);

	/* Retrieve current display configuration information */
	res = drmModeGetResources(fd);

	/* Print Frame Buffer Objects */
#ifdef VERBOSE
	printf("count_fbs=%d, count_crtcs=%d, count_connectors=%d, count_encoders=%d, "
		"min_width=%d, max_width=%d, min_height=%d, max_height=%d\n",
		res->count_fbs, res->count_crtcs, res->count_connectors, res->count_encoders,
		res->min_width, res->max_width, res->min_height, res->max_height);
#endif

	/* Save our connections. We will need to use these IDs */
	crtc_id = res->crtcs[0];
	conn_id = res->connectors[0];
#ifdef VERBOSE
	printf("crtc_id : %d\nconn_id : %d\n", crtc_id, conn_id);
#endif

	/* Retrieve current display mode information */
	crtc = drmModeGetCrtc(fd, crtc_id);
	screen_width = crtc->width;
	screen_height = crtc->height;
	printf("Current Screen Resolution = %d x %d\n", screen_width, screen_height);

	/* DRM core will expose all planes (overlay, primary, and cursor) to userspace. */
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	/* Gets information about planes.
	 * Gets a list of plane resources for a DRM device. A DRM application typically calls
	 * this function early to identify the available display layers.
	 * By default, the information returned includes only "Overlay" type (regular) planes – not
	 * "Primary" and "Cursor" planes. If DRM_CLIENT_CAP_UNIVERSAL_PLANES has been enabled
	 * with drmSetClientCap, the information returned includes "Primary" planes representing
	 * CTRCs, and "Cursor" planes representing Cursors. This allows CRTCs and Cursors to be
	 * manipulated with plane functions such as drmModeSetPlane.
	 */
	plane_res = drmModeGetPlaneResources(fd);
	if (!plane_res) {
		fprintf(stderr, "drmModeGetPlaneResources failed: %s\n",strerror(errno));
		return -1;
	}
	printf("count_planes = %d\n", plane_res->count_planes);

	/* Gets information about a plane.
	 * If plane_id is valid, fetches a drmModePlanePtr structure which contains information
	 * about the specified plane, such as its current CRTC and compatible CRTCs, the currently
	 * bound framebuffer, the number of gamma steps, and the position of the plane.
	 */

	/* Go through the available planes (hardware layers) and find which ones are connected
	 * to our current CTRC that is used for display output. */
	for (int i = 0; i < plane_res->count_planes; i++) {

		drmModePlane *pln = drmModeGetPlane(fd, plane_res->planes[i]);
		drmModeObjectPropertiesPtr properties;
		drmModePropertyPtr property;
		int j;

		printf("planes[%d]\n", i);

		if (!pln)
			continue;

#ifdef VERBOSE
		printf("\tcrtc_id: %d, plane_id: %d, fb_id: %d, crtc_x : %d, crtc_y: %d, x: %d, y: %d\n",
			pln->crtc_id, pln->plane_id, pln->fb_id, pln->crtc_x, pln->crtc_y, pln->x, pln->y);
#endif

		/* Find the plane IDs for the Primary(background) and Overlay */
		properties = drmModeObjectGetProperties(fd, pln->plane_id, DRM_MODE_OBJECT_PLANE);
		for (j = 0; j < properties->count_props; j++) {
			property = drmModeGetProperty(fd, properties->props[j]);
			if (!property)
				continue;
			printf("\tid=%d: %s=%ld \n", property->prop_id, property->name,
					properties->prop_values[j]);

			if (strcmp(property->name, "type") == 0)
			{
				switch (properties->prop_values[j]) {
				case DRM_PLANE_TYPE_OVERLAY:
					plane_id_overlay = pln->plane_id;
					break;
				case DRM_PLANE_TYPE_PRIMARY:
					plane_id_primary = pln->plane_id;
					break;
				case DRM_PLANE_TYPE_CURSOR:
					break;
				}
			}
			drmModeFreeProperty(property);
		}
		drmModeFreeObjectProperties(properties);
		drmModeFreePlane(pln);
	}
	printf("Number of planes : %d\n", plane_res->count_planes);
	printf("plane_id_primary : %d\n", plane_id_primary);
	printf("plane_id_overlay : %d\n", plane_id_overlay);

#ifdef BACKGROUND_PLANE
	/* Create background framebuffer and replace the current one */
	fb_background.width = screen_width;
	fb_background.height = screen_height;
	fb_background.x = 0;
	fb_background.y = 0;
	fb_background.alpha = 0;
	create_fb(fd, &fb_background);
	/* Background color */
	for (int i = 0; i < (fb_background.size / 4); i++)
		fb_background.vaddr[i] = 0xFF000080;	// Fill with blue
	printf("fb_background fb_id = %d\n", fb_background.fb_id);

	/* Assign our new background framebuffer we created to the Primary Plane. Then, we
	 * can updated the pixel data however we want.
	 * Since we are not going to modify the pixel data in the original framebuffer that
	 * existed and was connected to the CTRC when this app started, after this application ends,
	 * the screen will go back to what it was before (because we never touched that pixel data
	 */
	 /* 
	  * From:
	  * 	      [current fb id] --> [plane_id_primary] --> [crtc_id] --> [conn_id] --> LCD
	  * 
	  * To:
	  * 	[fb_background.fb_id] --> [plane_id_primary] --> [crtc_id] --> [conn_id] --> LCD
	  */
	if (drmModeSetPlane(fd,
		plane_id_primary, // Plane ID of the plane to be changed. 
		crtc_id, // CRTC ID of the CRTC that the plane is on. 
		fb_background.fb_id,	// Framebuffer ID of the framebuffer to display on the plane, or -1 to leave the framebuffer unchanged. 
		0, // Flags that control function behavior. No flags are currently supported for external use. 
		0, 0, fb_background.width, fb_background.height, // Offsets and dimensions on active display (destination)
		0 << 16, 0 << 16, fb_background.width << 16, fb_background.height << 16))
	{
		fprintf(stderr, "Failed to set Primary plane: %s\n", strerror(errno));
	}

	/* If we wanted to change the screen resolution (display mode), you can call drmModeSetCrtc.
	 * However, you will need a framebuffer that will fit.*/
	//drmModeSetCrtc(fd, crtc_id, fb_background.fb_id, fb_background.x, fb_background.y, &conn_id, 1, &conn->modes[0]);
	
	// You can also use drmModeSetCrtc to change to a different framebuffer and keep the mode the same.
	// For example you make two frame buffers so you can double buffer.
	//drmModeSetCrtc(fd, crtc_id, fb_background.fb_id, fb_background.x, fb_background.y, NULL, 0, NULL);
#endif


	/* Create overlay framebuffer to use for our Overlay plane */
	/* Create the frame buffer based on the size of our PNG image */
	fb_overlay.width = png_width;
	fb_overlay.height = png_height;
	fb_overlay.x = 200;
	fb_overlay.y = 200;
	fb_overlay.alpha = 1;
	create_fb(fd, &fb_overlay);
	for (int i = 0; i < (fb_overlay.size / 4); i++) {
		fb_overlay.vaddr[i] = 0x00000000; // Fill with Transparent (Alpha = 0)
	}
	printf("fb_overlay fb_id = %d\n", fb_overlay.fb_id);

	/* Copy in our PNG image to our overlay framebuffer */
	/* Remember that the framebuffer size might be different than the image
	 * because the size of each row must be on a boundary address */
	int row = 0;
	void *dest, *src;
	for (int i=0; i < png_height; i++) {
		src = row_pointers[i];
		dest = fb_overlay.vaddr;
		dest += row * fb_overlay.pitch; /* pitch is bytes per row */

		memcpy(dest,src,png_row_bytes);
		row++;
	}

	/* Now we can free the memory we allocated for the PNG rows */
	for(int row = 0; row < png_height; row++)
		free(row_pointers[row]);
	free(row_pointers);
	row_pointers = NULL;

	/* Add another plane (hardware overlay layer) to the CRTC
	 * 
	 * From:
	 * [fb_background.fb_id] --> [plane_id_primary] --> [crtc_id] --> [conn_id] --> LCD
	 * 
	 * To:
	 *                                                  -----------     -----------
	 * [fb_background.fb_id] --> [plane_id_primary] --> | crtc_id | --> | conn_id | --> LCD
	 * [fb_overlay.fb_id]    --> [plane_id_overlay] --> |         |     |         |
	 *                                                  -----------     -----------
	 */
	if (drmModeSetPlane(fd,
		plane_id_overlay, crtc_id, fb_overlay.fb_id,
		0,
		fb_overlay.x, fb_overlay.y, fb_overlay.width, fb_overlay.height, // (destination)
		0 << 16, 0 << 16, fb_overlay.width << 16, fb_overlay.height << 16))
	{
		fprintf(stderr, "Failed to set Overlay plane: %s\n", strerror(errno));
	}

	/****************************************
	 * Wait for user to press ENTER to 
	 ****************************************/
	printf("\nPress ENTER to exit...");
	getchar();

	drmModeFreeConnector(conn);
	drmModeFreePlaneResources(plane_res);
	drmModeFreeResources(res);
	drmModeFreeCrtc(crtc);

	close(fd);
	return 0;
}

