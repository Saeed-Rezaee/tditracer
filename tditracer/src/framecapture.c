
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <png.h>

#include "framelinkedlist.h"

extern int WriteBitmap(char* filename, unsigned char* bitmap, int width, int height, int colordepth);

#if 0
extern void write_png_file(char* file_name, int width, int height, png_bytep *row_pointers);
#endif

char*       map_base;
uint8_t*    framesbuffer;

int         currentframe = 0;

void*       memcpy_arm(void *, const void *, size_t);
void*       memcpy_neon(void *, const void *, size_t);


static int nr_frames = 0;


#define FRAMESIZE (1280 * 720 * 4)

int  framecapture_init(void);
void framecapture_capframe(void);
void framecapture_writebmpframes(int frames);
void framecapture_writepngframes(void);

int  framecapture_inited = 0;

int framecapture_init(void)
{
    int    fbdev;
    int    memdev;

    struct fb_fix_screeninfo fixInfo;

    if ((fbdev = open("/dev/fb0", O_RDONLY)) == 0)
    {
        printf("Error opening %s\n", "/dev/fb0");
        return -1;
    }

    if (ioctl(fbdev, FBIOGET_FSCREENINFO, &fixInfo) < 0)
    {
        printf("Error ioctl\n");
        return -1;
    }

    /*
     * Map 3 pages in the flipchain, using dev/mem
     */
    #define MAP_SIZE (FRAMESIZE * 3)

    off_t target = fixInfo.smem_start;

    if ((memdev = open("/dev/mem",O_RDWR)) == 0)
    {
        printf("Error opening file /dev/mem");
        return -1;
    }

    map_base = (char *)mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memdev, target);

    if (map_base == (void *)-1)
    {
        printf("Error mapping\n");
        return -1;
    }

    return 0;
}

void framecapture_capframe(void)
{
    if (!framecapture_inited) {
        framecapture_init();
        framecapture_inited = 1;
    }

    static int frame = -3;
    static int page = 1;

    if (frame >= 0) {

        framesbuffer = (uint8_t *)memalign(4096, FRAMESIZE);

        memcpy_arm(framesbuffer, map_base + page * FRAMESIZE , FRAMESIZE);

        framelinkedlist_add_to_list(nr_frames, nr_frames + 1, page, framesbuffer, true);

        nr_frames++;
    }

    frame++;
    page++;
    if (page == 3) page = 0;
}

void framecapture_writebmpframes(int frames)
{
    int frame;

    for (frame = 0 ; frames < frames ; frames++)
    {
        printf("writebmp[%d]\n", frame);

        char fname [64];
        sprintf(fname, "%d.bmp", frame);

        WriteBitmap(fname, (unsigned char*)(framesbuffer + (frame * FRAMESIZE)), 1280, 720, 4);
    }
}

void framecapture_writepngframes(void)
{
    int frame;


    frame_struct_t *frame_ptr = NULL;

    for (frame = 0 ; frame < nr_frames ; frame++)
    {

        frame_ptr = framelinkedlist_search_in_list(frame, NULL);
        if (NULL == frame_ptr)
        {
            printf("\n Search [id = %d] failed, no such element found\n", frame);
        } else {

            char fname[64];
            sprintf(fname, "f%03d-frame%d-p%d.png", frame_ptr->id, frame_ptr->name, frame_ptr->page);

            printf("writing frame, %s\n", fname);

            int x,y;

            png_bytep *row_pointers;

            row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * 720);

            for (y = 0 ; y < 720 ; y++)
            {
                row_pointers[y] = (png_byte*)malloc(1280 * 4);
            }

            for (y = 0 ; y < 720 ; y++)
            {
                png_byte* row = row_pointers[y];
                for (x = 0; x < 1280; x++)
                {
                    png_byte* ptr = &(row[x * 4]);

                    uint8_t* buf = frame_ptr->buf;

                    ptr[0] = buf[ (y * 1280 + x) * 4 + 2 ]; /* R */
                    ptr[1] = buf[ (y * 1280 + x) * 4 + 1 ]; /* G */
                    ptr[2] = buf[ (y * 1280 + x) * 4 + 0 ]; /* B */
                    ptr[3] = buf[ (y * 1280 + x) * 4 + 3 ]; /* A */
                }
            }

            #if 0
            write_png_file(fname, 1280, 720, row_pointers);
            #endif
            /*
             * write_png_file frees row_pointers
             */
        }
    }
}
