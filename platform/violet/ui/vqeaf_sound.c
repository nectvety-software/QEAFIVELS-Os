/* SPDX-License-Identifier: MIT */
#include "vqeaf_sound.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#endif

#include "lvgl.h"
#include "vqeaf_state.h"
#include "sounds/vqeaf_sound_data.h"

static const vqeaf_sound_backend_t *g_backend;
static const char *g_status = "Chưa kiểm tra PCM";
static lv_timer_t *g_timer;
static int g_ready;
static int g_opened;
static int g_current = -1;
static size_t g_offset;
static uint32_t g_started;
static vqeaf_sound_event_t g_queue[16];
static int g_head;
static int g_count;
static int16_t g_chunk[256];

#ifdef __linux__
static int g_oss_fd = -1;

static int oss_open(void *context)
{
    int channels = 1;
    int speed = 22050;
    int format = AFMT_S16_LE;
    int value = 0;

    (void)context;
    g_oss_fd = open("/dev/dsp", O_RDWR | O_CLOEXEC);
    if(g_oss_fd < 0) return -1;
    if(ioctl(g_oss_fd, SNDCTL_DSP_RESET) < 0 ||
       ioctl(g_oss_fd, SNDCTL_DSP_CHANNELS, &channels) < 0 ||
       ioctl(g_oss_fd, SNDCTL_DSP_SPEED, &speed) < 0 ||
       ioctl(g_oss_fd, SNDCTL_DSP_SETFMT, &format) < 0 ||
       channels != 1 || speed != 22050 || format != AFMT_S16_LE)
        goto fail;
    value = fcntl(g_oss_fd, F_GETFL);
    if(value < 0 || fcntl(g_oss_fd, F_SETFL, value | O_NONBLOCK) < 0)
        goto fail;
    return 0;
fail:
    close(g_oss_fd);
    g_oss_fd = -1;
    return -1;
}

static int oss_write(void *context, const int16_t *samples, size_t count)
{
    size_t done = 0;
    uint32_t deadline = (uint32_t)lv_tick_get() + 500u;
    (void)context;
    while(done < count) {
        ssize_t written = write(g_oss_fd, samples + done, (count - done) * sizeof(int16_t));
        if(written > 0) {
            done += (size_t)written / sizeof(int16_t);
            continue;
        }
        if(written < 0 && errno == EINTR) continue;
        if(written < 0 && errno == EAGAIN) {
            struct pollfd pfd = { .fd = g_oss_fd, .events = POLLOUT, 0 };
            int wait = (int)(deadline - (uint32_t)lv_tick_get());
            if(wait <= 0) return -1;
            if(poll(&pfd, 1, wait > 10 ? 10 : wait) < 0 && errno != EINTR)
                return -1;
            continue;
        }
        return -1;
    }
    return (int)done;
}

static void oss_close(void *context)
{
    (void)context;
    if(g_oss_fd >= 0) close(g_oss_fd);
    g_oss_fd = -1;
}

static const vqeaf_sound_backend_t g_oss_backend = {
    .name = "OSS PCM · 22.05 kHz",
    .open = oss_open,
    .write = oss_write,
    .close = oss_close,
};
#endif

static int unsupported_open(void *context)
{
    (void)context;
    return -1;
}

static int unsupported_write(void *context, const int16_t *samples, size_t count)
{
    (void)context;
    (void)samples;
    (void)count;
    return -1;
}

static void unsupported_close(void *context)
{
    (void)context;
}

static const vqeaf_sound_backend_t g_unsupported_backend = {
    .name = "Chưa có thiết bị PCM",
    .open = unsupported_open,
    .write = unsupported_write,
    .close = unsupported_close,
};

static void sound_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    vqeaf_sound_tick();
}

void vqeaf_sound_set_backend(const vqeaf_sound_backend_t *backend)
{
    if(g_timer != NULL || g_ready) return;
    g_backend = backend;
}

int vqeaf_sound_init(void)
{
    int16_t silence[2] = { 0, 0 };
    int written;

    if(g_timer != NULL) return g_ready ? 0 : -1;
    if(g_backend == NULL) {
#ifdef __linux__
        g_backend = &g_oss_backend;
#else
        g_backend = &g_unsupported_backend;
#endif
    }
    g_head = 0;
    g_count = 0;
    g_current = -1;
    g_offset = 0;
    g_opened = 0;
    if(g_backend->open == NULL || g_backend->write == NULL ||
       g_backend->open(g_backend->context) != 0) {
        g_status = "Chưa có thiết bị PCM";
        return -1;
    }
    written = g_backend->write(g_backend->context, silence, 2);
    if(g_backend->close != NULL) g_backend->close(g_backend->context);
    if(written != 2) {
        g_status = "PCM không ghi được";
        return -1;
    }
    g_status = g_backend->name;
    g_ready = 1;
    g_timer = lv_timer_create(sound_timer_cb, 10, NULL);
    if(g_timer == NULL) {
        g_ready = 0;
        g_status = "Không tạo được bộ phát âm thanh";
        return -1;
    }
    return 0;
}

void vqeaf_sound_shutdown(void)
{
    if(g_timer != NULL) lv_timer_delete(g_timer);
    g_timer = NULL;
    if(g_opened && g_backend != NULL && g_backend->close != NULL)
        g_backend->close(g_backend->context);
    g_opened = 0;
    g_ready = 0;
    g_current = -1;
    g_head = 0;
    g_count = 0;
}

int vqeaf_sound_ready(void)
{
    return g_ready;
}

const char *vqeaf_sound_status(void)
{
    return g_status;
}

int vqeaf_sound_play(vqeaf_sound_event_t event)
{
    if(!g_ready || (unsigned int)event >= VQEAF_SOUND_COUNT ||
       vqeaf_state.volume <= 0 || g_count >= (int)(sizeof(g_queue) / sizeof(g_queue[0])))
        return -1;
    g_queue[(g_head + g_count) % (int)(sizeof(g_queue) / sizeof(g_queue[0]))] = event;
    g_count++;
    return 0;
}

void vqeaf_sound_tick(void)
{
    const vqeaf_sound_clip_t *clip;
    size_t remaining, count;
    int volume, written;

    if(!g_ready || g_backend == NULL) return;
    if(g_current < 0) {
        if(g_count == 0) return;
        g_current = (int)g_queue[g_head];
        g_head = (g_head + 1) % (int)(sizeof(g_queue) / sizeof(g_queue[0]));
        g_count--;
        g_offset = 0;
        g_opened = 0;
        g_started = (uint32_t)lv_tick_get();
    }
    clip = vqeaf_sound_clip((vqeaf_sound_event_t)g_current);
    if(clip == NULL || clip->samples == NULL || clip->count == 0) {
        g_current = -1;
        return;
    }
    if((uint32_t)lv_tick_get() - g_started > 2000u) {
        if(g_opened && g_backend->close != NULL) g_backend->close(g_backend->context);
        g_opened = 0;
        g_ready = 0;
        g_status = "PCM phản hồi quá lâu";
        g_current = -1;
        return;
    }
    if(!g_opened) {
        if(g_backend->open(g_backend->context) != 0) {
            g_ready = 0;
            g_status = "PCM không mở được";
            g_current = -1;
            return;
        }
        g_opened = 1;
    }
    remaining = clip->count - g_offset;
    count = remaining < sizeof(g_chunk) / sizeof(g_chunk[0]) ? remaining :
            sizeof(g_chunk) / sizeof(g_chunk[0]);
    volume = vqeaf_state.volume > 100 ? 100 : vqeaf_state.volume;
    for(size_t i = 0; i < count; i++)
        g_chunk[i] = (int16_t)((int32_t)clip->samples[g_offset + i] * volume / 100);
    written = g_backend->write(g_backend->context, g_chunk, count);
    if(written < 0 || written > (int)count) {
        if(g_backend->close != NULL) g_backend->close(g_backend->context);
        g_opened = 0;
        g_ready = 0;
        g_status = "PCM ghi âm thanh thất bại";
        g_current = -1;
        return;
    }
    if(written == 0) {
        if((uint32_t)lv_tick_get() - g_started > 2000u) {
            if(g_backend->close != NULL) g_backend->close(g_backend->context);
            g_opened = 0;
            g_ready = 0;
            g_status = "PCM phản hồi quá lâu";
            g_current = -1;
        }
        return;
    }
    g_offset += (size_t)written;
    if(g_offset >= clip->count) {
        if(g_backend->close != NULL) g_backend->close(g_backend->context);
        g_opened = 0;
        g_current = -1;
    }
}
