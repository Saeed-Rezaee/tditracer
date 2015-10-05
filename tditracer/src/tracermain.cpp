
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <stdarg.h>

#include "tdi.h"

#include "tracermain.h"
#include "tracerutils.h"

extern "C" {
#include "shadercapture.h"
#include "texturecapture.h"
#include "framecapture.h"
}

static void init(void);
static void dump(void);

static void __attribute__((constructor)) tditracer_constructor();
static void __attribute__((destructor)) tditracer_destructor();

static void tditracer_constructor() {
    tditrace_init();

    init();
}

static void tditracer_destructor() {
    fprintf(stderr, "tditracer: exit[%d]\n", getpid());
}

int framestorecord;
bool texturerecording;
bool renderbufferrecording;
bool shaderrecording;

bool libcrecording;
bool libcopenrecording;
bool libcfopenrecording;
bool libcfdopenrecording;
bool libcfreopenrecording;
bool libcreadrecording;
bool libcwriterecording;
bool libcsocketrecording;
bool libcsendrecording;
bool libcsendtorecording;
bool libcsendmsgrecording;
bool libcsendmmsgrecording;
bool libcrecvrecording;
bool libcrecvfromrecording;
bool libcrecvmsgrecording;
bool libcrecvmmsgrecording;
bool libcselectrecording;
bool libcpollrecording;
bool libcioctlrecording;

unsigned int libcmalloc;
unsigned int libccalloc;
unsigned int libcrealloc;

bool pthreadrecording;
bool eglrecording;
bool glesrecording;
bool gldrawrecording;

int shaders_captured = 0;
int textures_captured = 0;
int texturebytes_captured = 0;
int frames_captured = 0;

typedef unsigned int mz_uint;
typedef int mz_bool;
extern "C" void *tdefl_write_image_to_png_file_in_memory(const void *pImage,
                                                         int w, int h,
                                                         int num_chans,
                                                         size_t *pLen_out);
extern "C" void *
tdefl_write_image_to_png_file_in_memory_ex(const void *pImage, int w, int h,
                                           int num_chans, size_t *pLen_out,
                                           mz_uint level, mz_bool flip);

static void signalhandler(int sig, siginfo_t *si, void *context) {

    switch (sig) {
    case SIGINT:

        static bool diddump = false;

        printf("tditracer: received SIGINT\n");

        /*
         * do not dump again if another ctrl-c is received, instead
         * go direct to abort, allowing a current dumping to be aborted
         */
        if (!diddump) {
            diddump = true;

            if (framestorecord > 0) {

                framecapture_capframe();
                frames_captured++;

                framecapture_capframe();
                frames_captured++;

                framecapture_capframe();
                frames_captured++;
            }

            dump();
        }

        if (texturerecording) {

#if 0
                unsigned char* p = (unsigned char*)malloc(1280 * 720 * 4);
                glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, p);
                void *pPNG_data;
                size_t png_data_size = 0;
                pPNG_data = tdefl_write_image_to_png_file_in_memory_ex(p, 1280, 720, 4, &png_data_size, 6, 1);
                FILE *pFile = fopen("frame.png", "wb");
                fwrite(pPNG_data, 1, png_data_size, pFile);
                chmod("frame.png", 0666);
                fclose(pFile);
#endif
        }

        abort();

        break;

    case SIGQUIT:

        printf("tditracer: received SIGQUIT, rewinding tracebuffer\n");
        tditrace_rewind();
        break;
    }
}

static void dump(void) {
    if (texturerecording || (framestorecord > 0) || shaderrecording) {
        printf(
            "dumping, #shaders captured = %d, #textures captured = %d, #frames "
            "captured = %d\n",
            shaders_captured, textures_captured, frames_captured);

        if (shaderrecording) {
            shadercapture_writeshaders();
        }

        if (texturerecording) {
            texturecapture_writepngtextures();
            texturecapture_deletetextures();
        }

        if (framestorecord > 0) {
            framecapture_writepngframes();
        }
    }
}

static void init(void) {
    static bool inited = false;
    if (!inited) {

#if 0
        static struct sigaction sVal;

        sVal.sa_flags = SA_SIGINFO;
        sVal.sa_sigaction = signalhandler;
        // Register for SIGINT
        sigaction(SIGINT, &sVal, NULL);
        // Register for SIGQUIT
        sigaction(SIGQUIT, &sVal, NULL);

#endif
        char* env;
        if (env = getenv("LIBC")) {
            libcrecording = (atoi(env) >= 1);
            libcopenrecording = true;
            libcfopenrecording = true;
            libcfdopenrecording = true;
            libcfreopenrecording = true;
            libcreadrecording = true;
            libcwriterecording = true;
            libcsocketrecording = true;
            libcsendrecording = true;
            libcsendtorecording = true;
            libcsendmsgrecording = true;
            libcsendmmsgrecording = true;
            libcrecvrecording = true;
            libcrecvfromrecording = true;
            libcrecvmsgrecording = true;
            libcrecvmmsgrecording = true;
            libcselectrecording = true;
            libcpollrecording = true;
            libcioctlrecording = false;
            libcmalloc = 0;
            libccalloc = 0;
            libcrealloc = 0;

        } else {
            libcrecording = false;
            libcioctlrecording = false;
        }

        if (env = getenv("LIBCOPEN")) {
            libcopenrecording = libcfopenrecording = libcfdopenrecording = libcfreopenrecording = (atoi(env) >= 1);
        }
        if (env = getenv("LIBCREAD")) {
            libcreadrecording = (atoi(env) >= 1);
        }
        if (env = getenv("LIBCWRITE")) {
            libcwriterecording = (atoi(env) >= 1);
        }
        if (env = getenv("LIBCSOCKET")) {
            libcsocketrecording = (atoi(env) >= 1);
            libcsendrecording = libcsendtorecording = libcsendmsgrecording = libcsendmmsgrecording = (atoi(env) >= 1);
            libcrecvrecording = libcrecvfromrecording = libcrecvmsgrecording = libcrecvmmsgrecording = (atoi(env) >= 1);
            libcselectrecording = (atoi(env) >= 1);
            libcpollrecording = (atoi(env) >= 1);
        }
        if (env = getenv("LIBCIOCTL")) {
            libcioctlrecording = (atoi(env) >= 1);
        }

        if (env = getenv("LIBCMALLOC")) {
            libcmalloc = atoi(env);
        }
        if (env = getenv("LIBCCALLOC")) {
            libccalloc = atoi(env);
        }
        if (env = getenv("LIBCREALLOC")) {
            libcrealloc = atoi(env);
        }


        if (getenv("PTHREAD")) {
            pthreadrecording = (atoi(getenv("PTHREAD")) >= 1);
        } else {
            pthreadrecording = false;
        }

        if (getenv("EGL")) {
            eglrecording = (atoi(getenv("EGL")) >= 1);
        } else {
            eglrecording = false;
        }

        if (getenv("GLES")) {
            glesrecording = (atoi(getenv("GLES")) >= 1);
        } else {
            glesrecording = false;
        }

        if (getenv("GLDRAW")) {
            gldrawrecording = (atoi(getenv("GLDRAW")) >= 1);
        } else {
            gldrawrecording = false;
        }

        if (getenv("TR")) {
            texturerecording = (atoi(getenv("TR")) >= 1);
        }
        if (getenv("RR")) {
            renderbufferrecording = (atoi(getenv("RR")) >= 1);
        }
        if (getenv("FR")) {
            framestorecord = atoi(getenv("FR"));
        }
        if (getenv("SR")) {
            shaderrecording = (atoi(getenv("SR")) >= 1);
        }

        if (renderbufferrecording) {
            texturerecording = true;
        }

        printf("tditracer: init[%d], libc:%s, pthread:%s, shaders:%s, "
               "textures:%s, "
               "renderbuffers:%s, frames:%d\n",
               getpid(), libcrecording ? "yes" : "no",
               pthreadrecording ? "yes" : "no", shaderrecording ? "yes" : "no",
               texturerecording ? "yes" : "no",
               renderbufferrecording ? "yes" : "no", framestorecord);

        inited = true;
    }
}
