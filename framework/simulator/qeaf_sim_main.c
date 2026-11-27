/* SPDX-License-Identifier: MIT */
/* framework/simulator/qeaf_sim_main.c - CLI driver for the host simulator. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qeaf_sim.h"
#include "panel_scale.h"

/* vqeaf_qeapp.c references the shell file reader for its install path; the
 * simulator reads the package itself and only uses inspect_native. */
int vqeaf_ui_read_file(const char *name, uint8_t **out, size_t *size)
{
    (void)name;
    (void)out;
    (void)size;
    return -1;
}

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
        fclose(file);
        free(buffer);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return buffer;
}

/* ---- interactive window (--ui) ------------------------------------------
 * The simulator is intentionally dependency-free, so the window is plain
 * Win32 + GDI: the app framebuffer is StretchDIBits'd into the client area.
 * WM_KEYDOWN is translated to evdev key codes and mouse press/move/release is
 * translated to touch events in the app canvas, then fed to the app through
 * qeaf_sim_input(). This makes a Redmi Note 7 Pro touch app playable on the PC
 * instead of a single printed frame. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct {
    qeaf_sim_t *sim;
    uint32_t width;
    uint32_t height;
} sim_window_t;

static int ui_key_code(WPARAM vk)
{
    switch(vk) {
    case VK_LEFT:  return 105;
    case VK_RIGHT: return 106;
    case VK_UP:    return 103;
    case VK_DOWN:  return 108;
    case 'W':      return 119;
    case 'A':      return 97;
    case 'S':      return 115;
    case 'D':      return 100;
    case VK_RETURN:return 28;
    default:       return -1;
    }
}

/* Mouse input is delivered in simulator client pixels. The client shows the
 * QEAPP canvas (720x1560 for violet), so map straight into canvas coordinates
 * before handing the event to the app. Device profiles that letterbox the
 * canvas are handled by panel_scale for --touches; the interactive window
 * always renders only the canvas. */
static void ui_touch(sim_window_t *win, HWND hwnd, LPARAM lp, int pressed)
{
    RECT rc;
    int canvas_x, canvas_y;
    qeaf_input_event_t event;
    char error[128];

    if(win == NULL || !GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0)
        return;
    canvas_x = (int)LOWORD(lp) * (int)win->width / rc.right;
    canvas_y = (int)HIWORD(lp) * (int)win->height / rc.bottom;
    if(canvas_x < 0) canvas_x = 0;
    if(canvas_y < 0) canvas_y = 0;
    if((uint32_t)canvas_x >= win->width) canvas_x = (int)win->width - 1;
    if((uint32_t)canvas_y >= win->height) canvas_y = (int)win->height - 1;

    event.type = QEAF_INPUT_TOUCH;
    event.code = 1u;
    event.x = canvas_x;
    event.y = canvas_y;
    event.pressed = (uint32_t)pressed;
    if(qeaf_sim_input(win->sim, &event, error, sizeof(error)) != 0)
        fprintf(stderr, "WINDOW: %s\n", error);
    else
        fprintf(stderr, "WINDOW: touch %d,%d pressed=%d\n", canvas_x, canvas_y, pressed);
    InvalidateRect(hwnd, NULL, FALSE);
}

static LRESULT CALLBACK sim_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    sim_window_t *win = (sim_window_t *)(intptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch(msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;

        GetClientRect(hwnd, &rc);
        if(win != NULL) {
            BITMAPINFO bmi;
            ZeroMemory(&bmi, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = (LONG)win->width;
            bmi.bmiHeader.biHeight = -(LONG)win->height;   /* top-down rows */
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, 0, 0, rc.right, rc.bottom, 0, 0,
                          (int)win->width, (int)win->height,
                          qeaf_sim_framebuffer(win->sim), &bmi,
                          DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_KEYDOWN: {
        int code = ui_key_code(wp);

        if(win != NULL && code >= 0) {
            qeaf_input_event_t event;
            char error[128];

            event.type = QEAF_INPUT_KEY;
            event.code = (uint32_t)code;
            event.x = 0;
            event.y = 0;
            event.pressed = 1;
            if(qeaf_sim_input(win->sim, &event, error, sizeof(error)) != 0)
                fprintf(stderr, "WINDOW: %s\n", error);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        sim_window_t *win = (sim_window_t *)(intptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        ui_touch(win, hwnd, lp, 1);
        SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if((wp & MK_LBUTTON) != 0) {
            sim_window_t *win = (sim_window_t *)(intptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            ui_touch(win, hwnd, lp, 1);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        sim_window_t *win = (sim_window_t *)(intptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        ui_touch(win, hwnd, lp, 0);
        ReleaseCapture();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Runs the window loop until the user closes it. The app must already be in
 * the "started" state; the caller stops/destroys it afterwards. */
static int run_ui(qeaf_sim_t *sim, const char *title, char *error, size_t cap)
{
    static const wchar_t class_name[] = L"VQEAFSimWindow";
    sim_window_t win;
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    RECT rect;
    int screen_h, percent, client_w, client_h;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = sim_window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = class_name;
    if(RegisterClassExW(&wc) == 0) {
        snprintf(error, cap, "window class registration failed");
        return -1;
    }

    win.sim = sim;
    win.width = qeaf_sim_width(sim);
    win.height = qeaf_sim_height(sim);
    /* Fit at most ~85% of the screen height; integer percent keeps it exact. */
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    percent = 100;
    if(screen_h > 0 && win.height > 0 &&
       (int64_t)win.height * 100 > (int64_t)screen_h * 85) {
        int64_t fit = (int64_t)screen_h * 85 * 100 / ((int64_t)win.height * 100);
        percent = (int)fit;
        if(percent < 10) percent = 10;
        if(percent > 100) percent = 100;
    }
    client_w = (int)((int64_t)win.width * percent / 100);
    client_h = (int)((int64_t)win.height * percent / 100);

    rect.left = 0; rect.top = 0; rect.right = client_w; rect.bottom = client_h;
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    hwnd = CreateWindowExW(0, class_name, L"M\u00e1y \u1ea3o VQEAF - qeaf-sim",
                           WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rect.right - rect.left, rect.bottom - rect.top,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if(hwnd == NULL) {
        snprintf(error, cap, "CreateWindowExW failed");
        return -1;
    }
    (void)title;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&win);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    fprintf(stderr, "WINDOW: %dx%d client, touch: mouse click/drag, keys: arrows / WASD, close to exit\n",
            client_w, client_h);

    while(GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
#else
static int run_ui(qeaf_sim_t *sim, const char *title, char *error, size_t cap)
{
    (void)sim;
    (void)title;
    snprintf(error, cap, "this simulator was built without a window backend");
    return -1;
}
#endif

static int write_framebuffer(const qeaf_sim_t *sim, const char *path)
{
    FILE *file = fopen(path, "wb");
    if(file == NULL) return -1;
    size_t count = (size_t)qeaf_sim_width(sim) * qeaf_sim_height(sim);
    size_t written = fwrite(qeaf_sim_framebuffer(sim), sizeof(uint32_t), count, file);
    fclose(file);
    return written == count ? 0 : -1;
}

/* Write the physical device scanout: the app canvas scaled onto the real
 * panel with the same integer maths the phone uses. Only meaningful when a
 * device profile was selected. */
static int write_panel_framebuffer(const qeaf_sim_t *sim, const char *path)
{
    const qeaf_device_profile_t *device = qeaf_sim_device(sim);
    if(device == NULL) return -1;
    uint32_t panel_w = qeaf_sim_panel_width(sim);
    uint32_t panel_h = qeaf_sim_panel_height(sim);
    uint32_t *scanout = calloc((size_t)panel_w * panel_h, sizeof(uint32_t));
    if(scanout == NULL) return -1;

    vio_panel_t panel;
    vio_panel_init(&panel, (int)qeaf_sim_width(sim), (int)qeaf_sim_height(sim),
                   (int)panel_w, (int)panel_h, (int)device->rotation);
    vio_panel_render(&panel, qeaf_sim_framebuffer(sim), scanout,
                     vio_panel_full_rect(&panel));
    FILE *file = fopen(path, "wb");
    int rc = -1;
    if(file != NULL) {
        size_t count = (size_t)panel_w * panel_h;
        size_t written = fwrite(scanout, sizeof(uint32_t), count, file);
        fclose(file);
        rc = written == count ? 0 : -1;
    }
    free(scanout);
    return rc;
}

int main(int argc, char **argv)
{
    const char *package_path = NULL;
    const char *fb_path = NULL;
    const char *panel_fb_path = NULL;
    const char *scope = NULL;
    const char *device_id = NULL;
    const char *keys = NULL;
    const char *touches = NULL;
    int ui = 0;
    qeaf_input_event_t input[QEAF_SIM_MAX_INPUT];
    size_t input_count = 0;
    size_t mem_bytes = 0;
    uint32_t call_ms = 0;

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--list-devices") == 0) {
            printf("devices:\n");
            for(size_t n = 0; n < qeaf_device_count(); n++) {
                const qeaf_device_profile_t *p = qeaf_device_at(n);
                printf("  %s: %s panel=%ux%u canvas=%ux%u format=%s\n",
                       p->id, p->name, p->panel_width, p->panel_height,
                       p->canvas_width, p->canvas_height, p->pixel_format);
            }
            return 0;
        }
        if(strcmp(argv[i], "--device") == 0 && i + 1 < argc)
            device_id = argv[++i];
        else if(strcmp(argv[i], "--fb") == 0 && i + 1 < argc)
            fb_path = argv[++i];
        else if(strcmp(argv[i], "--panel-fb") == 0 && i + 1 < argc)
            panel_fb_path = argv[++i];
        else if(strcmp(argv[i], "--scope") == 0 && i + 1 < argc)
            scope = argv[++i];
        else if(strcmp(argv[i], "--mem-bytes") == 0 && i + 1 < argc)
            mem_bytes = (size_t)strtoull(argv[++i], NULL, 10);
        else if(strcmp(argv[i], "--call-ms") == 0 && i + 1 < argc)
            call_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if(strcmp(argv[i], "--keys") == 0 && i + 1 < argc)
            keys = argv[++i];
        else if(strcmp(argv[i], "--touches") == 0 && i + 1 < argc)
            touches = argv[++i];
        else if(strcmp(argv[i], "--ui") == 0)
            ui = 1;
        else if(argv[i][0] == '-') {
            fprintf(stderr, "ERROR: unknown option or missing value: %s\n", argv[i]);
            return 2;
        }
        else if(package_path == NULL) package_path = argv[i];
        else {
            fprintf(stderr, "ERROR: unexpected argument: %s\n", argv[i]);
            return 2;
        }
    }
    if(package_path == NULL) {
        printf("usage: %s package.qeapp [--device id] [--fb out.raw] "
               "[--panel-fb out.raw] [--scope prefix] [--mem-bytes N] "
               "[--call-ms N] [--keys code[,code...]] "
               "[--touches x,y,pressed;...] [--list-devices]\n",
               argv[0]);
        return 2;
    }
    /* --keys feeds real QEAF_INPUT_KEY press events to the app's optional
     * on_input export, in order, after start(). It is how a host drives a
     * keyboard app (a game, a menu) from the command line; a package without
     * on_input simply receives nothing and reports input delivered: 0. */
    if(keys != NULL) {
        const char *cursor = keys;

        while(*cursor != '\0') {
            char *end = NULL;
            unsigned long code;

            if(input_count >= QEAF_SIM_MAX_INPUT) {
                fprintf(stderr, "ERROR: --keys exceeds the input event limit\n");
                return 2;
            }
            code = strtoul(cursor, &end, 0);
            if(end == cursor || code > 0xFFFFFFFFul ||
               (*end != '\0' && *end != ',')) {
                fprintf(stderr, "ERROR: --keys expects numeric key codes, "
                                "e.g. --keys 103,106\n");
                return 2;
            }
            input[input_count].type = QEAF_INPUT_KEY;
            input[input_count].code = (uint32_t)code;
            input[input_count].x = 0;
            input[input_count].y = 0;
            input[input_count].pressed = 1;
            input_count++;
            cursor = end;
            if(*cursor == ',') {
                cursor++;
                if(*cursor == '\0') {
                    fprintf(stderr, "ERROR: --keys has a trailing comma\n");
                    return 2;
                }
            }
        }
    }
    /* Touch scripts use semicolons between complete down/move/up events so a
     * drag is distinguishable from a tap in deterministic host runs. */
    if(touches != NULL) {
        const char *cursor = touches;
        while(*cursor != '\0') {
            char *end = NULL;
            if(input_count >= QEAF_SIM_MAX_INPUT) {
                fprintf(stderr, "ERROR: --touches exceeds the input event limit\n");
                return 2;
            }
            long x = strtol(cursor, &end, 10);
            if(end == cursor || *end != ',' || x < 0 || x > 2147483647L) {
                fprintf(stderr, "ERROR: --touches expects x,y,pressed;... events\n");
                return 2;
            }
            cursor = end + 1;
            long y = strtol(cursor, &end, 10);
            if(end == cursor || *end != ',' || y < 0 || y > 2147483647L) {
                fprintf(stderr, "ERROR: --touches expects x,y,pressed;... events\n");
                return 2;
            }
            cursor = end + 1;
            long pressed = strtol(cursor, &end, 10);
            if(end == cursor || (pressed != 0 && pressed != 1) ||
               (*end != '\0' && *end != ';')) {
                fprintf(stderr, "ERROR: --touches expects x,y,pressed;... events\n");
                return 2;
            }
            input[input_count].type = QEAF_INPUT_TOUCH;
            input[input_count].code = 1u;
            input[input_count].x = (int32_t)x;
            input[input_count].y = (int32_t)y;
            input[input_count].pressed = (uint32_t)pressed;
            input_count++;
            cursor = end;
            if(*cursor == ';') {
                cursor++;
                if(*cursor == '\0') {
                    fprintf(stderr, "ERROR: --touches has a trailing semicolon\n");
                    return 2;
                }
            }
        }
    }
    const qeaf_device_profile_t *device = qeaf_device_find(device_id);
    if(device_id != NULL && device == NULL) {
        fprintf(stderr, "ERROR: unknown device '%s' (try --list-devices)\n", device_id);
        return 2;
    }

    size_t size = 0;
    uint8_t *package = read_package(package_path, &size);
    if(package == NULL) {
        fprintf(stderr, "ERROR: cannot read %s\n", package_path);
        return 1;
    }
    qeaf_sim_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.package = package;
    cfg.package_size = size;
    cfg.scope = scope;
    cfg.memory_budget = mem_bytes;
    cfg.call_budget_ms = call_ms;
    cfg.device = device;
    cfg.input = input;
    cfg.input_count = input_count;

    char error[256];
    qeaf_sim_t *sim = qeaf_sim_new(&cfg, error, sizeof(error));
    if(sim == NULL) {
        fprintf(stderr, "ERROR: %s\n", error);
        free(package);
        return 1;
    }
    int rc;
    if(ui) {
        /* Interactive: create + start, then hand the framebuffer to the window
         * loop until the user closes it, and finish the lifecycle afterwards so
         * the normal leak/budget checks still run on the real path. */
        rc = qeaf_sim_create_app(sim, error, sizeof(error));
        if(rc == 0) rc = qeaf_sim_start(sim, error, sizeof(error));
        if(rc == 0 && run_ui(sim, package_path, error, sizeof(error)) != 0)
            rc = -1;
        if(rc == 0) rc = qeaf_sim_pause(sim, error, sizeof(error));
        if(rc == 0) rc = qeaf_sim_resume(sim, error, sizeof(error));
        if(rc == 0) rc = qeaf_sim_stop(sim, error, sizeof(error));
        if(rc == 0) rc = qeaf_sim_destroy_app(sim, error, sizeof(error));
        if(rc == 0 && qeaf_sim_call_timeout(sim)) {
            snprintf(error, sizeof(error),
                     "native call exceeded the per-call CPU budget");
            rc = -1;
        }
        if(rc == 0 && qeaf_sim_outstanding_allocs(sim) != 0) {
            snprintf(error, sizeof(error), "app leaked allocation(s) after destroy");
            rc = -1;
        }
    }
    else {
        rc = qeaf_sim_run(sim, error, sizeof(error));
    }

    if(qeaf_sim_device(sim) != NULL) {
        const qeaf_device_profile_t *p = qeaf_sim_device(sim);
        printf("device: %s (%s) panel=%ux%u canvas=%ux%u format=%s\n",
               p->name, p->id, qeaf_sim_panel_width(sim),
               qeaf_sim_panel_height(sim), qeaf_sim_width(sim),
               qeaf_sim_height(sim), p->pixel_format);
    }
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
    printf("call timeout: %d (max %u ms)\n", qeaf_sim_call_timeout(sim),
           qeaf_sim_max_call_ms(sim));
    printf("denied decisions: %u\n", qeaf_sim_deny_count(sim));
    printf("audit (%zu):\n", qeaf_sim_audit_count(sim));
    for(size_t i = 0; i < qeaf_sim_audit_count(sim); i++)
        printf("  %s\n", qeaf_sim_audit_at(sim, i));

    if(rc != 0) fprintf(stderr, "ERROR: %s\n", error);
    if(fb_path != NULL && write_framebuffer(sim, fb_path) != 0)
        fprintf(stderr, "ERROR: cannot write framebuffer %s\n", fb_path);
    if(panel_fb_path != NULL && write_panel_framebuffer(sim, panel_fb_path) != 0)
        fprintf(stderr, "ERROR: cannot write panel framebuffer %s "
                "(is --device set?)\n", panel_fb_path);
    qeaf_sim_free(sim);
    free(package);
    return rc == 0 ? 0 : 1;
}