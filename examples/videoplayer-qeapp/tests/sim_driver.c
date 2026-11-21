/* sim_driver.c - host-simulator proof for the VideoPlayer native module.
 *
 * Loads a package given on argv[1], seeds one real QVF video file under the
 * app's storage scope, feeds three input events (OK = next frame twice,
 * LEFT = back), and runs the full lifecycle. Prints state, trace, log,
 * draws, blits, allocs and the framebuffer hash so a test can assert real
 * execution and real decoded video frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qeaf_sim.h"

/* Stub required by the simulator core (packages are passed in memory). */
int vqeaf_ui_read_file(const char *name, uint8_t **out, size_t *size)
{
    (void)name; (void)out; (void)size;
    return -1;
}

/* QVF key codes used by the app: OK=5, LEFT=3. */
#define VP_KEY_OK 5
#define VP_KEY_LEFT 3

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if(file == NULL) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if(length <= 0) { fclose(file); return NULL; }
    uint8_t *buffer = malloc((size_t)length);
    if(buffer == NULL) { fclose(file); return NULL; }
    if(fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        fclose(file); free(buffer); return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return buffer;
}

int main(int argc, char **argv)
{
    const char *fb_out = NULL;
    /* usage: sim package.qeapp [video.qvf] [--fb out.raw] */
    for(int i = 2; i < argc; i++) {
        if(strcmp(argv[i], "--fb") == 0 && i + 1 < argc) fb_out = argv[++i];
    }
    if(argc < 2) {
        fprintf(stderr, "usage: %s package.qeapp [video.qvf]\n", argv[0]);
        return 2;
    }
    size_t package_size = 0;
    uint8_t *package = read_file(argv[1], &package_size);
    if(package == NULL) {
        fprintf(stderr, "ERROR: cannot read %s\n", argv[1]);
        return 1;
    }

    qeaf_sim_file_t files[1];
    int seed = 1;
    uint8_t *video = NULL;
    size_t video_size = 0;
    if(getenv("VIDEOPLAYER_NO_SEED") != NULL) seed = 0;
    if(seed) {
        const char *video_path = argc >= 3 ? argv[2]
                                           : "build/media-fixtures/video1.qvf";
        video = read_file(video_path, &video_size);
        if(video == NULL) {
            fprintf(stderr, "ERROR: cannot read QVF %s\n", video_path);
            free(package);
            return 1;
        }
        files[0].path = "apps/org.vqeaf.videoplayer/@media/VIDEO1.QVF";
        files[0].data = video;
        files[0].size = video_size;
    }

    qeaf_input_event_t input[3];
    memset(input, 0, sizeof(input));
    /* OK steps forward twice, LEFT steps back once. */
    input[0].type = QEAF_INPUT_KEY;
    input[0].code = VP_KEY_OK;
    input[0].pressed = 1;
    input[1].type = QEAF_INPUT_KEY;
    input[1].code = VP_KEY_OK;
    input[1].pressed = 1;
    input[2].type = QEAF_INPUT_KEY;
    input[2].code = VP_KEY_LEFT;
    input[2].pressed = 1;

    qeaf_sim_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.package = package;
    cfg.package_size = package_size;
    cfg.files = seed ? files : NULL;
    cfg.file_count = seed ? 1 : 0;
    cfg.input = input;
    cfg.input_count = 3;
    cfg.scope = "apps/org.vqeaf.videoplayer";

    char error[256];
    qeaf_sim_t *sim = qeaf_sim_new(&cfg, error, sizeof(error));
    if(sim == NULL) {
        fprintf(stderr, "ERROR: %s\n", error);
        free(package);
        free(video);
        return 1;
    }
    int rc = qeaf_sim_run(sim, error, sizeof(error));

    printf("state: %s\n", qeaf_sim_state(sim));
    printf("permissions: 0x%08x\n", qeaf_sim_permissions(sim));
    printf("lifecycle:");
    for(size_t i = 0; i < qeaf_sim_trace_count(sim); i++)
        printf(" %s", qeaf_sim_trace_at(sim, i));
    printf("\n");
    printf("log (%zu):\n", qeaf_sim_log_count(sim));
    for(size_t i = 0; i < qeaf_sim_log_count(sim); i++)
        printf("  %s\n", qeaf_sim_log_at(sim, i));
    printf("draws (%zu):\n", qeaf_sim_draw_count(sim));
    for(size_t i = 0; i < qeaf_sim_draw_count(sim); i++) {
        int x = 0, y = 0;
        char text[128];
        qeaf_sim_draw_at(sim, i, &x, &y, text, sizeof(text));
        printf("  (%d,%d) \"%s\"\n", x, y, text);
    }
    int bx = 0, by = 0, bw = 0, bh = 0;
    int have_blit = qeaf_sim_blit_last(sim, &bx, &by, &bw, &bh) == 0;
    printf("blits: %zu last=(%d,%d %dx%d) %s\n", qeaf_sim_blit_count(sim),
           bx, by, bw, bh, have_blit ? "" : "(none)");
    printf("input delivered: %zu\n", qeaf_sim_input_delivered(sim));
    printf("framebuffer: %ux%u hash=0x%08x\n", qeaf_sim_width(sim),
           qeaf_sim_height(sim), qeaf_sim_framebuffer_hash(sim));
    printf("outstanding allocs: %zu\n", qeaf_sim_outstanding_allocs(sim));
    printf("memory used: %zu\n", qeaf_sim_memory_used(sim));

    if(rc != 0) fprintf(stderr, "ERROR: %s\n", error);
    if(fb_out != NULL) {
        FILE *fb = fopen(fb_out, "wb");
        if(fb == NULL) {
            fprintf(stderr, "ERROR: cannot write framebuffer %s\n", fb_out);
            rc = -1;
        } else {
            size_t count = (size_t)qeaf_sim_width(sim) *
                           qeaf_sim_height(sim);
            if(fwrite(qeaf_sim_framebuffer(sim), sizeof(uint32_t), count,
                      fb) != count) {
                fprintf(stderr, "ERROR: short framebuffer write\n");
                rc = -1;
            }
            fclose(fb);
        }
    }
    qeaf_sim_free(sim);
    free(package);
    free(video);
    return rc == 0 ? 0 : 1;
}
