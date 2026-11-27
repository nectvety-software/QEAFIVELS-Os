/* SPDX-License-Identifier: MIT */
/* Host-only contract tests for the Violet sound service. */
#include "vqeaf_sound.h"
#include "sounds/vqeaf_sound_data.h"
#include "vqeaf_state.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "src/misc/lv_timer_private.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

vqeaf_state_t vqeaf_state = {
    .volume = 50,
};

#define MAX_CAPTURED_FRAMES 32768u
#define MAX_CLIP_FRAMES 22050u

static uint32_t g_tick_ms;
static lv_timer_t g_fake_timer;
static unsigned int g_timer_creates;
static unsigned int g_timer_deletes;
static unsigned int g_timer_errors;

typedef struct {
    int opens;
    int writes;
    int closes;
    int probe_writes;
    int runtime_writes;
    int probe_sample_count;
    int16_t probe_samples[2];
    size_t max_write_count;
    size_t captured_frames;
    int16_t captured_samples[MAX_CAPTURED_FRAMES];
    int errors;
} fake_backend_stats_t;

static fake_backend_stats_t g_stats;
static vqeaf_sound_backend_t g_backend;
static int g_probing;
static int g_fail_probe_open;
static int g_fail_probe_write;
static int g_fail_runtime_open;
static int g_fail_runtime_write;
static int g_zero_runtime_writes;
static int g_checks;

#define CHECK(condition) do { \
    g_checks++; \
    if(!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while(0)

uint32_t lv_tick_get(void)
{
    return g_tick_ms;
}

lv_timer_t *lv_timer_create(lv_timer_cb_t timer_xcb, uint32_t period,
                            void *user_data)
{
    memset(&g_fake_timer, 0, sizeof(g_fake_timer));
    g_fake_timer.timer_cb = timer_xcb;
    g_fake_timer.period = period;
    g_fake_timer.user_data = user_data;
    g_fake_timer.repeat_count = -1;
    g_timer_creates++;
    return &g_fake_timer;
}

void lv_timer_delete(lv_timer_t *timer)
{
    if(timer != &g_fake_timer) g_timer_errors++;
    g_timer_deletes++;
}

static int fake_open(void *context)
{
    if(context != &g_stats) g_stats.errors++;
    g_stats.opens++;
    if(g_probing && g_fail_probe_open) return -1;
    if(!g_probing && g_fail_runtime_open) return -1;
    return 0;
}

static int fake_write(void *context, const int16_t *samples, size_t count)
{
    if(context != &g_stats) g_stats.errors++;
    g_stats.writes++;
    if(samples == NULL || count == 0 || count > 256) {
        g_stats.errors++;
        return -1;
    }
    if(count > g_stats.max_write_count) g_stats.max_write_count = count;

    if(g_probing) {
        g_stats.probe_writes++;
        g_stats.probe_sample_count = (int)count;
        if(count == 2) {
            g_stats.probe_samples[0] = samples[0];
            g_stats.probe_samples[1] = samples[1];
        }
        if(g_fail_probe_write) return -1;
        return (int)count;
    }

    g_stats.runtime_writes++;
    if(g_fail_runtime_write) return -1;
    if(g_zero_runtime_writes) return 0;
    if(g_stats.captured_frames + count > MAX_CAPTURED_FRAMES) {
        g_stats.errors++;
        return -1;
    }
    memcpy(g_stats.captured_samples + g_stats.captured_frames, samples,
           count * sizeof(int16_t));
    g_stats.captured_frames += count;
    return (int)count;
}

static void fake_close(void *context)
{
    if(context != &g_stats) g_stats.errors++;
    g_stats.closes++;
}

static void reset_harness(void)
{
    vqeaf_sound_shutdown();
    memset(&g_stats, 0, sizeof(g_stats));
    memset(&g_fake_timer, 0, sizeof(g_fake_timer));
    g_tick_ms = 0;
    g_timer_creates = 0;
    g_timer_deletes = 0;
    g_timer_errors = 0;
    g_probing = 0;
    g_fail_probe_open = 0;
    g_fail_probe_write = 0;
    g_fail_runtime_open = 0;
    g_fail_runtime_write = 0;
    g_zero_runtime_writes = 0;
    vqeaf_state.volume = 50;
    g_backend.name = "host fake PCM";
    g_backend.context = &g_stats;
    g_backend.open = fake_open;
    g_backend.write = fake_write;
    g_backend.close = fake_close;
    vqeaf_sound_set_backend(&g_backend);
}

static int probe_init(void)
{
    int result;

    g_probing = 1;
    result = vqeaf_sound_init();
    g_probing = 0;
    return result;
}

static int start_successful_backend(void)
{
    reset_harness();
    CHECK(probe_init() == 0);
    CHECK(vqeaf_sound_ready() == 1);
    CHECK(strcmp(vqeaf_sound_status(), g_backend.name) == 0);
    CHECK(g_stats.opens == 1);
    CHECK(g_stats.writes == 1);
    CHECK(g_stats.probe_writes == 1);
    CHECK(g_stats.probe_sample_count == 2);
    CHECK(g_stats.probe_samples[0] == 0);
    CHECK(g_stats.probe_samples[1] == 0);
    CHECK(g_stats.closes == 1);
    CHECK(g_stats.runtime_writes == 0);
    CHECK(g_timer_creates == 1);
    CHECK(g_fake_timer.timer_cb != NULL);
    CHECK(g_fake_timer.period == 10);
    CHECK(g_fake_timer.user_data == NULL);
    CHECK(g_timer_errors == 0);
    CHECK(g_stats.errors == 0);
    return 1;
}

static int drain_playback(void)
{
    for(int i = 0; i < 4096; i++) {
        int before = g_stats.opens + g_stats.writes + g_stats.closes;
        int after;

        g_tick_ms += 10;
        vqeaf_sound_tick();
        if(!vqeaf_sound_ready()) return 0;
        after = g_stats.opens + g_stats.writes + g_stats.closes;
        if(after == before) return 1;
    }
    return 0;
}

static void write_le32(unsigned char *out, uint32_t value)
{
    out[0] = (unsigned char)(value & 0xffu);
    out[1] = (unsigned char)((value >> 8) & 0xffu);
    out[2] = (unsigned char)((value >> 16) & 0xffu);
    out[3] = (unsigned char)((value >> 24) & 0xffu);
}

/* Export descriptor bytes so Python can compare assets without parsing C. */
static int dump_clips(void)
{
    static const unsigned char magic[] = "VQEAF_SOUND_CLIPS_V1";
    static unsigned char pcm[MAX_CLIP_FRAMES * 2u];

#ifdef _WIN32
    if(_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
#endif

    if(fwrite(magic, 1, sizeof(magic), stdout) != sizeof(magic)) return 1;
    for(int event = 0; event < VQEAF_SOUND_COUNT; event++) {
        const vqeaf_sound_clip_t *clip = vqeaf_sound_clip(
            (vqeaf_sound_event_t)event);
        unsigned char header[8];
        size_t bytes;

        if(clip == NULL || clip->samples == NULL || clip->count == 0 ||
           clip->count > MAX_CLIP_FRAMES)
            return 1;
        write_le32(header, (uint32_t)event);
        write_le32(header + 4, (uint32_t)clip->count);
        bytes = clip->count * 2u;
        for(size_t i = 0; i < clip->count; i++) {
            uint16_t sample = (uint16_t)clip->samples[i];
            pcm[i * 2u] = (unsigned char)(sample & 0xffu);
            pcm[i * 2u + 1u] = (unsigned char)(sample >> 8);
        }
        if(fwrite(header, 1, sizeof(header), stdout) != sizeof(header) ||
           fwrite(pcm, 1, bytes, stdout) != bytes)
            return 1;
    }
    return fflush(stdout) == 0 ? 0 : 1;
}

static int test_descriptors(void)
{
    static const int16_t *const expected_samples[VQEAF_SOUND_COUNT] = {
        vqeaf_sound_tap, vqeaf_sound_drop, vqeaf_sound_navigate,
        vqeaf_sound_notify, vqeaf_sound_capture, vqeaf_sound_record,
        vqeaf_sound_warning, vqeaf_sound_charge,
    };
    static const size_t expected_counts[VQEAF_SOUND_COUNT] = {
        sizeof(vqeaf_sound_tap) / sizeof(vqeaf_sound_tap[0]),
        sizeof(vqeaf_sound_drop) / sizeof(vqeaf_sound_drop[0]),
        sizeof(vqeaf_sound_navigate) / sizeof(vqeaf_sound_navigate[0]),
        sizeof(vqeaf_sound_notify) / sizeof(vqeaf_sound_notify[0]),
        sizeof(vqeaf_sound_capture) / sizeof(vqeaf_sound_capture[0]),
        sizeof(vqeaf_sound_record) / sizeof(vqeaf_sound_record[0]),
        sizeof(vqeaf_sound_warning) / sizeof(vqeaf_sound_warning[0]),
        sizeof(vqeaf_sound_charge) / sizeof(vqeaf_sound_charge[0]),
    };

    for(int event = 0; event < VQEAF_SOUND_COUNT; event++) {
        const vqeaf_sound_clip_t *clip = vqeaf_sound_clip(
            (vqeaf_sound_event_t)event);
        int nonzero = 0;

        CHECK(clip != NULL);
        CHECK(clip == vqeaf_sound_clip((vqeaf_sound_event_t)event));
        CHECK(clip->samples != NULL);
        CHECK(clip->samples == expected_samples[event]);
        CHECK(clip->count == expected_counts[event]);
        CHECK(clip->count > 0);
        CHECK(clip->count <= MAX_CLIP_FRAMES);
        for(size_t i = 0; i < clip->count; i++) {
            if(clip->samples[i] != 0) nonzero = 1;
        }
        CHECK(nonzero == 1);
    }
    CHECK(vqeaf_sound_clip((vqeaf_sound_event_t)-1) == NULL);
    CHECK(vqeaf_sound_clip((vqeaf_sound_event_t)VQEAF_SOUND_COUNT) == NULL);
    CHECK(vqeaf_sound_clip((vqeaf_sound_event_t)(VQEAF_SOUND_COUNT + 1)) == NULL);
    CHECK(vqeaf_sound_clip((vqeaf_sound_event_t)INT_MAX) == NULL);
    return 1;
}

static int test_successful_playback_and_scaling(void)
{
    const vqeaf_sound_clip_t *tap = vqeaf_sound_clip(VQEAF_SOUND_TAP);
    const vqeaf_sound_clip_t *drop = vqeaf_sound_clip(VQEAF_SOUND_DROP);
    size_t total = tap->count + drop->count;
    size_t expected_runtime_writes =
        (tap->count + 255u) / 256u + (drop->count + 255u) / 256u;
    int matched_nonzero = 0;

    CHECK(start_successful_backend());
    vqeaf_state.volume = 50;
    CHECK(vqeaf_sound_play(VQEAF_SOUND_TAP) == 0);
    CHECK(vqeaf_sound_play(VQEAF_SOUND_DROP) == 0);
    CHECK(drain_playback());
    CHECK(vqeaf_sound_ready() == 1);
    CHECK(g_stats.opens == 3);
    CHECK(g_stats.closes == 3);
    CHECK(g_stats.runtime_writes == (int)expected_runtime_writes);
    CHECK(g_stats.writes == 1 + (int)expected_runtime_writes);
    CHECK(g_stats.max_write_count <= 256);
    CHECK(g_stats.captured_frames == total);
    CHECK(g_stats.errors == 0);

    for(size_t i = 0; i < total; i++) {
        const vqeaf_sound_clip_t *clip = i < tap->count ? tap : drop;
        size_t index = i < tap->count ? i : i - tap->count;
        int16_t expected = (int16_t)(
            (int32_t)clip->samples[index] * 50 / 100);

        CHECK(g_stats.captured_samples[i] == expected);
        if(expected != 0) {
            CHECK(g_stats.captured_samples[i] == expected);
            matched_nonzero++;
        }
    }
    CHECK(matched_nonzero > 0);
    vqeaf_sound_shutdown();
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(g_timer_deletes == 1);
    return 1;
}

static int test_zero_volume_preserves_queue(void)
{
    const vqeaf_sound_clip_t *tap = vqeaf_sound_clip(VQEAF_SOUND_TAP);
    size_t expected_writes = (tap->count + 255u) / 256u;

    CHECK(start_successful_backend());
    CHECK(vqeaf_sound_play(VQEAF_SOUND_TAP) == 0);
    vqeaf_state.volume = 0;
    CHECK(vqeaf_sound_play(VQEAF_SOUND_DROP) == -1);
    CHECK(vqeaf_sound_play(VQEAF_SOUND_NOTIFY) == -1);
    CHECK(drain_playback());
    CHECK(g_stats.opens == 2);
    CHECK(g_stats.closes == 2);
    CHECK(g_stats.runtime_writes == (int)expected_writes);
    CHECK(g_stats.captured_frames == tap->count);
    CHECK(g_stats.errors == 0);
    return 1;
}

static int test_queue_capacity_and_shutdown(void)
{
    const vqeaf_sound_clip_t *tap = vqeaf_sound_clip(VQEAF_SOUND_TAP);
    size_t expected_writes = (tap->count + 255u) / 256u;

    CHECK(start_successful_backend());
    for(int i = 0; i < 16; i++) {
        CHECK(vqeaf_sound_play((vqeaf_sound_event_t)(i % VQEAF_SOUND_COUNT)) == 0);
    }
    CHECK(vqeaf_sound_play(VQEAF_SOUND_TAP) == -1);
    vqeaf_sound_shutdown();
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(g_timer_deletes == 1);
    CHECK(vqeaf_sound_play(VQEAF_SOUND_TAP) == -1);

    CHECK(start_successful_backend());
    CHECK(vqeaf_sound_play(VQEAF_SOUND_TAP) == 0);
    CHECK(drain_playback());
    CHECK(g_stats.opens == 2);
    CHECK(g_stats.closes == 2);
    CHECK(g_stats.runtime_writes == (int)expected_writes);
    CHECK(g_stats.captured_frames == tap->count);
    vqeaf_sound_shutdown();
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(g_timer_deletes == 1);
    return 1;
}

static int test_init_open_failure(void)
{
    reset_harness();
    vqeaf_sound_set_backend(NULL);
    CHECK(probe_init() == -1);
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(strcmp(vqeaf_sound_status(), "Chưa có thiết bị PCM") == 0);
    CHECK(g_timer_creates == 0);

    vqeaf_sound_shutdown();
    reset_harness();
    g_fail_probe_open = 1;
    CHECK(probe_init() == -1);
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(strcmp(vqeaf_sound_status(), "Chưa có thiết bị PCM") == 0);
    CHECK(g_stats.opens == 1);
    CHECK(g_stats.writes == 0);
    CHECK(g_stats.closes == 0);
    CHECK(g_timer_creates == 0);
    return 1;
}

static int test_probe_write_failure(void)
{
    reset_harness();
    g_fail_probe_write = 1;
    CHECK(probe_init() == -1);
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(strcmp(vqeaf_sound_status(), "PCM không ghi được") == 0);
    CHECK(g_stats.opens == 1);
    CHECK(g_stats.probe_writes == 1);
    CHECK(g_stats.probe_sample_count == 2);
    CHECK(g_stats.probe_samples[0] == 0);
    CHECK(g_stats.probe_samples[1] == 0);
    CHECK(g_stats.closes == 1);
    CHECK(g_timer_creates == 0);
    return 1;
}

static int test_runtime_open_failure(void)
{
    CHECK(start_successful_backend());
    g_fail_runtime_open = 1;
    CHECK(vqeaf_sound_play(VQEAF_SOUND_TAP) == 0);
    g_tick_ms += 10;
    vqeaf_sound_tick();
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(strcmp(vqeaf_sound_status(), "PCM không mở được") == 0);
    CHECK(g_stats.opens == 2);
    CHECK(g_stats.runtime_writes == 0);
    CHECK(g_stats.closes == 1);
    vqeaf_sound_shutdown();
    CHECK(g_timer_deletes == 1);
    CHECK(g_stats.closes == 1);
    return 1;
}

static int test_runtime_write_failure(void)
{
    CHECK(start_successful_backend());
    g_fail_runtime_write = 1;
    CHECK(vqeaf_sound_play(VQEAF_SOUND_TAP) == 0);
    g_tick_ms += 10;
    vqeaf_sound_tick();
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(strcmp(vqeaf_sound_status(), "PCM ghi âm thanh thất bại") == 0);
    CHECK(g_stats.opens == 2);
    CHECK(g_stats.runtime_writes == 1);
    CHECK(g_stats.closes == 2);
    vqeaf_sound_shutdown();
    CHECK(g_timer_deletes == 1);
    CHECK(g_stats.closes == 2);
    return 1;
}

static int test_no_progress_timeout(void)
{
    CHECK(start_successful_backend());
    g_zero_runtime_writes = 1;
    CHECK(vqeaf_sound_play(VQEAF_SOUND_TAP) == 0);
    vqeaf_sound_tick();
    CHECK(vqeaf_sound_ready() == 1);
    CHECK(g_stats.runtime_writes == 1);
    CHECK(g_stats.closes == 1);

    g_tick_ms = 2000;
    vqeaf_sound_tick();
    CHECK(vqeaf_sound_ready() == 1);
    CHECK(g_stats.runtime_writes == 2);
    CHECK(g_stats.closes == 1);

    g_tick_ms = 2001;
    vqeaf_sound_tick();
    CHECK(vqeaf_sound_ready() == 0);
    CHECK(strcmp(vqeaf_sound_status(), "PCM phản hồi quá lâu") == 0);
    CHECK(g_stats.runtime_writes >= 2);
    CHECK(g_stats.closes == 2);
    vqeaf_sound_shutdown();
    CHECK(g_timer_deletes == 1);
    CHECK(g_stats.closes == 2);
    return 1;
}

static int test_invalid_event(void)
{
    CHECK(start_successful_backend());
    CHECK(vqeaf_sound_play(VQEAF_SOUND_NONE) == -1);
    CHECK(vqeaf_sound_play((vqeaf_sound_event_t)-1) == -1);
    CHECK(vqeaf_sound_play((vqeaf_sound_event_t)VQEAF_SOUND_COUNT) == -1);
    CHECK(vqeaf_sound_play((vqeaf_sound_event_t)INT_MAX) == -1);
    g_tick_ms += 10;
    vqeaf_sound_tick();
    CHECK(vqeaf_sound_ready() == 1);
    CHECK(g_stats.opens == 1);
    CHECK(g_stats.runtime_writes == 0);
    CHECK(g_stats.closes == 1);
    return 1;
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        int (*run)(void);
    } tests[] = {
        { "embedded clip descriptors", test_descriptors },
        { "probe and scaled TAP/DROP playback", test_successful_playback_and_scaling },
        { "zero volume preserves queue", test_zero_volume_preserves_queue },
        { "queue capacity and shutdown reset", test_queue_capacity_and_shutdown },
        { "unsupported and failed init open", test_init_open_failure },
        { "probe write failure", test_probe_write_failure },
        { "runtime open failure", test_runtime_open_failure },
        { "runtime write failure", test_runtime_write_failure },
        { "no-progress timeout", test_no_progress_timeout },
        { "invalid event rejection", test_invalid_event },
    };

    if(argc == 2 && strcmp(argv[1], "--dump-clips") == 0)
        return dump_clips();
    if(argc != 1) {
        fprintf(stderr, "usage: %s [--dump-clips]\n", argv[0]);
        return 2;
    }

    for(size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if(!tests[i].run()) {
            fprintf(stderr, "vqeaf_sound: FAILED %s\n", tests[i].name);
            vqeaf_sound_shutdown();
            return 1;
        }
        printf("PASS %s\n", tests[i].name);
    }
    vqeaf_sound_shutdown();
    printf("vqeaf_sound: %zu tests, %d checks passed\n",
           sizeof(tests) / sizeof(tests[0]), g_checks);
    return 0;
}
