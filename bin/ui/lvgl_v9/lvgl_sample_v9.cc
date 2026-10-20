// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lvgl_v9/lvgl.h>
#include <lvgl_v9/src/misc/lv_types.h>
#include <lvgl_v9/src/display/lv_display_private.h>
#include <lvgl_v9/demos/lv_demos.h>
#include <lvgl_v9/examples/lv_examples.h>
#include <lvgl_v9/src/core/lv_global.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/device/display.h>

#define FRAMEBUFFER_DEV "/dev/sys/platform/ramfb/mtk_ramfb/framebuffer"
#define RETRY 1000

void lv_example_event_draw(void);

typedef struct nebula_display {
    int fd;
    lv_color32_t *framebuffer;
} nebula_display_t;

static void flush_cb(lv_display_t * disp_drv, const lv_area_t * area, uint8_t * px_map)
{
    int32_t width = disp_drv->hor_res;
    int32_t height = disp_drv->ver_res;

    if (area->x2 < 0 || area->y2 < 0 || area->x1 >= width || area->y1 >= height) {
        lv_display_flush_ready(disp_drv);
        return;
    }

    lv_color32_t * buf = (lv_color32_t *)px_map;
    nebula_display_t *disp = (nebula_display_t *)disp_drv->user_data;
#if 1
    int width_in_area = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; y++) {
        /* Calculate the starting offset of the current row */
        int32_t offset = y * disp_drv->hor_res + area->x1;
        memcpy(&disp->framebuffer[offset], buf, width_in_area * sizeof(lv_color32_t));
        /* Move to the beginning of the next line */
        buf += width_in_area;
    }
#else
    int32_t x, y;
    for(y = area->y1; y <= area->y2; y++) {
        for(x = area->x1; x <= area->x2; x++) {
            *disp->framebuffer++ = *buf++;
        }
    }
#endif
    ioctl_display_region_t region;
    region.x = area->x1;
    region.y = area->y1;
    region.width = area->x2 - area->x1 + 1;
    region.height = area->y2 - area->y1 + 1;
    ioctl_display_flush_fb_region(disp->fd, &region);

    /*IMPORTANT!!!
      *Inform the graphics library that you are ready with the flushing*/
    lv_display_flush_ready(disp_drv);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <test_name>\n", argv[0]);
    }


    int fd;
    int retries = RETRY;
retry:
    if ((fd = open(FRAMEBUFFER_DEV, O_RDWR)) < 0) {
        if (--retries > 0) {
            goto retry;
        } else {
            printf("lvgl: failed to open display %s: %d\n", FRAMEBUFFER_DEV, fd);
            return -1;
        }
    }

    ioctl_display_get_fb_t fb;
    ssize_t result = ioctl_display_get_fb(fd, &fb);
    if (result != sizeof(fb)) {
        printf("lvgl: failed to get framebuffer: %zd\n", result);
        return result;
    }

    // Get framebuffer and map to local address space.
    zx_vaddr_t vaddr = 0;
    size_t size = fb.info.stride * fb.info.pixelsize * fb.info.height;
    zx_status_t status = zx_vmar_map(zx_vmar_root_self(),
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, 0, fb.vmo, 0, size, &vaddr);
    if (status != ZX_OK) {
        printf("lvgl: failed to map framebuffer: %d\n", status);
        return status;
    }

    nebula_display_t nbl_display;
    nbl_display.fd = fd;
    nbl_display.framebuffer = (lv_color32_t *)vaddr;

    uint32_t pixel_count = fb.info.width * fb.info.height;
    void *buf1 = malloc(pixel_count * sizeof(lv_color32_t));

    // Initialize LittlevGL.
    lv_init();

    lv_display_t * disp = lv_display_create(fb.info.width, fb.info.height);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(disp, buf1, NULL, pixel_count * sizeof(lv_color32_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, &nbl_display);
    lv_display_set_resolution(disp, fb.info.width, fb.info.height);

    const char *main_param = argv[1];
    const char *sub_param[] = {argv[2]};
    char *demos[] = {const_cast<char *>("music")};

    if (argc < 3) {
        lv_demos_create(demos, 1);
        //lv_example_event_draw();
    } else {
        if (strcmp(main_param, "e") == 0) {
            /* lvgl examples */
            if (strcmp(sub_param[0], "lab") == 0) {
                lv_example_get_started_1();
            } else if (strcmp(sub_param[0], "btn") == 0) {
                lv_example_style_18();
            } else if (strcmp(sub_param[0], "draw") == 0) {
                lv_example_event_draw();
            } else if (strcmp(sub_param[0], "anim") == 0) {
                lv_example_anim_2();
            } else {
                printf("Unrecognized sub-param: %s\n", sub_param[0]);
            }
        } else if (strcmp(main_param, "d") == 0) {
            /* lvgl demos */
            lv_demos_create(const_cast<char **>(sub_param), 1);
        } else {
            printf("Unrecognized main param: %s\n", main_param);
        }
    }

    struct timeval tv_start_total, tv_current;
    uint64_t elapsed_usec = 0;

    gettimeofday(&tv_start_total, NULL);

    do {
        struct timeval tv_start;
        struct timeval tv_end;
        uint64_t time_sleep_usec = 0;

        gettimeofday(&tv_start, NULL);

        lv_task_handler();
        usleep(5000);

        gettimeofday(&tv_end, NULL);

        time_sleep_usec = tv_end.tv_sec - tv_start.tv_sec;
        time_sleep_usec *= 1000000;
        time_sleep_usec += tv_end.tv_usec - tv_start.tv_usec;

        // Microseconds (time_sleep_usec) rounded to milliseconds.
        // If the microseconds more than 500 microseconds, it will be rounded to 1
        // millisecond or it will be discarded.
        lv_tick_inc((time_sleep_usec + 500) / 1000);

        gettimeofday(&tv_current, NULL);
        elapsed_usec = (tv_current.tv_sec - tv_start_total.tv_sec) * 1000000;
        elapsed_usec += tv_current.tv_usec - tv_start_total.tv_usec;

    } while(elapsed_usec < 3 * 1000000); //3s
    //} while(1);

    if (disp) {
        lv_disp_remove(disp);
        disp = NULL;
    }
    if (buf1) {
        free(buf1);
        buf1 = NULL;
    }
    if (vaddr) {
        zx_vmar_unmap(zx_vmar_root_self(), vaddr, size);
        vaddr = 0;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }

    return 0;
}

static uint32_t size = 0;
static bool size_dec = false;

static void timer_cb(lv_timer_t * timer)
{
    lv_obj_t * obj = (lv_obj_t *)lv_timer_get_user_data(timer);
    if (!obj){
        printf("zz debug obj is null\n");
        return;
    }
    lv_obj_invalidate(obj);
    if(size_dec) size--;
    else size++;

    if(size == 50) size_dec = true;
    else if(size == 0) size_dec = false;
}

static void event_cb(lv_event_t * e)
{
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t * base_dsc = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(draw_task);
    if(base_dsc->part == LV_PART_MAIN) {
        lv_draw_rect_dsc_t draw_dsc;
        lv_draw_rect_dsc_init(&draw_dsc);
        draw_dsc.bg_color = lv_color_hex(0xffaaaa);
        draw_dsc.radius = LV_RADIUS_CIRCLE;
        draw_dsc.border_color = lv_color_hex(0xff5555);
        draw_dsc.border_width = 2;
        draw_dsc.outline_color = lv_color_hex(0xff0000);
        draw_dsc.outline_pad = 3;
        draw_dsc.outline_width = 2;

        lv_area_t a;
        a.x1 = 0;
        a.y1 = 0;
        a.x2 = size;
        a.y2 = size;
        lv_area_t obj_coords;
        lv_obj_get_coords(obj, &obj_coords);
        lv_area_align(&obj_coords, &a, LV_ALIGN_CENTER, 0, 0);

        lv_draw_rect(base_dsc->layer, &draw_dsc, &a);
    }
}

/**
 * Demonstrate the usage of draw event
 */
void lv_example_event_draw(void)
{
    lv_obj_t * cont = lv_obj_create(lv_screen_active());
    lv_obj_set_size(cont, 200, 200);
    lv_obj_center(cont);
    lv_obj_add_event_cb(cont, event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_timer_create(timer_cb, 30, cont);
}