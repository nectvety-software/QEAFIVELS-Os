/* sim_driver.c - host-simulator proof for the CodeEditor native module.
 *
 * Loads a package given on argv[1], seeds one real source file under the
 * app's storage scope, feeds two input events (skip splash, open editor), and
 * runs the full lifecycle. Prints state, trace, log, draws, allocs and the
 * framebuffer hash so a test can assert real execution and real file content.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qeaf_sim.h"

/* Stub required by the simulator core. */
int vqeaf_ui_read_file(const char *name, uint8_t **out, size_t *size)
{
    (void)name; (void)out; (void)size;
    return -1;
}

/* qeaf_code_key_t values from qeaf_code_platform.h: OK=5, SOFT_LEFT=7. */
#define QEAF_KEY_OK 5
#define QEAF_KEY_SOFT_LEFT 7

static uint8_t *read_package(const char *path, size_t *size)
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
    if(argc < 2) {
        fprintf(stderr, "usage: %s package.qeapp\n", argv[0]);
        return 2;
    }
    size_t size = 0;
    uint8_t *package = read_package(argv[1], &size);
    if(package == NULL) {
        fprintf(stderr, "ERROR: cannot read %s\n", argv[1]);
        return 1;
    }

    static const char seeded[] =
        "# seeded by simulator\nint main(void) { return 0; }\n";
    qeaf_sim_file_t files[1];
    int seed = 1;
    if(getenv("CODEEDITOR_NO_SEED") != NULL) seed = 0;
    if(seed) {
        files[0].path = "apps/org.vqeaf.codeeditor/@codeeditor/MAIN.C";
        files[0].data = (const uint8_t *)seeded;
        files[0].size = sizeof(seeded);
    }

    qeaf_input_event_t input[2];
    memset(input, 0, sizeof(input));
    /* Any key skips the splash screen. */
    input[0].type = QEAF_INPUT_KEY;
    input[0].code = QEAF_KEY_OK;
    input[0].pressed = 1;
    /* Soft-left on the list opens the editor (OPEN). */
    input[1].type = QEAF_INPUT_KEY;
    input[1].code = QEAF_KEY_SOFT_LEFT;
    input[1].pressed = 1;

    qeaf_sim_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.package = package;
    cfg.package_size = size;
    cfg.files = seed ? files : NULL;
    cfg.file_count = seed ? 1 : 0;
    cfg.input = input;
    cfg.input_count = 2;
    cfg.scope = "apps/org.vqeaf.codeeditor";

    char error[256];
    qeaf_sim_t *sim = qeaf_sim_new(&cfg, error, sizeof(error));
    if(sim == NULL) {
        fprintf(stderr, "ERROR: %s\n", error);
        free(package);
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
    printf("input delivered: %zu\n", qeaf_sim_input_delivered(sim));
    printf("framebuffer: %ux%u hash=0x%08x\n", qeaf_sim_width(sim),
           qeaf_sim_height(sim), qeaf_sim_framebuffer_hash(sim));
    printf("outstanding allocs: %zu\n", qeaf_sim_outstanding_allocs(sim));
    printf("memory used: %zu\n", qeaf_sim_memory_used(sim));

    if(rc != 0) fprintf(stderr, "ERROR: %s\n", error);
    qeaf_sim_free(sim);
    free(package);
    return rc == 0 ? 0 : 1;
}
