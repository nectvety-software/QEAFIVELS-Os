/* VQEAF AArch64 window manager (SurfaceFlinger-like) + input dispatcher
 * (InputFlinger-like) for the independent kernel. EMULATED QEMU virt only.
 *
 * A window is a real layer owned by one EL0 task. The kernel allocates the
 * backing store and maps it into that task's own user root; the client draws
 * straight into it and calls present(). The compositor then recomposes the
 * framebuffer from background -> legacy F2c surfaces -> windows by z order,
 * clipping every layer to the screen. Keyboard bytes arriving on the PL011 RX
 * IRQ are dispatched to the topmost visible window (focus), never to every
 * task: that is the InputFlinger focus-stack behaviour.
 */
#include "vqeaf/window.h"
#include "vqeaf/display_aarch64.h"
#include "vqeaf/memory.h"
#include "vqeaf/vm.h"
#include "vqeaf/scheduler.h"
#include "vqeaf/uart_aarch64.h"

typedef struct {
    int used;
    uint32_t id;
    int32_t owner;
    uint32_t x, y, w, h;
    int32_t z;
    uint32_t flags;
    uint64_t buffer_va;
    volatile uint32_t *buffer_pa;
    uint32_t bytes;
    uint64_t pages;
    int dirty;
    vqeaf_key_event_t keys[VQEAF_INPUT_QUEUE];
    uint32_t key_head, key_tail;
} vqeaf_window_t;

static vqeaf_window_t wins[VQEAF_WINDOW_MAX_COUNT];
static int32_t g_focus = -1;
static uint32_t g_next_id = 1;
static uint32_t g_deliveries;
static uint32_t g_seq;
static uint32_t g_generation;
static int g_frame_done;

void vqeaf_wm_init(void)
{
    for(unsigned i = 0; i < VQEAF_WINDOW_MAX_COUNT; i++) {
        wins[i].used = 0;
        wins[i].key_head = wins[i].key_tail = 0;
    }
    g_focus = -1; g_next_id = 1; g_deliveries = 0; g_seq = 0;
    g_generation = 0; g_frame_done = 0;
    vqeaf_uart_write("WM: window manager online (max ");
    vqeaf_uart_u64(VQEAF_WINDOW_MAX_COUNT);
    vqeaf_uart_write(" layers)\r\n");
}

static vqeaf_window_t *find(uint32_t id)
{
    if(!id || id > VQEAF_WINDOW_MAX_COUNT) return 0;
    vqeaf_window_t *w = &wins[id - 1];
    return w->used ? w : 0;
}

static void recompute_focus(void)
{
    int32_t best = -1;
    for(unsigned i = 0; i < VQEAF_WINDOW_MAX_COUNT; i++) {
        vqeaf_window_t *w = &wins[i];
        if(!w->used || !(w->flags & VQEAF_WINDOW_FLAG_VISIBLE)) continue;
        if(best < 0 || w->z > wins[best].z ||
           (w->z == wins[best].z && (int32_t)w->id > (int32_t)wins[best].id))
            best = (int32_t)i;
    }
    g_focus = best;
    if(best >= 0) {
        vqeaf_uart_write("WM: focus -> win ");
        vqeaf_uart_u64(wins[best].id);
        vqeaf_uart_write(" (z=");
        vqeaf_uart_u64((uint64_t)(int64_t)wins[best].z);
        vqeaf_uart_write(")\r\n");
    }
}

int vqeaf_wm_create(const vqeaf_window_desc_t *desc, int32_t owner)
{
    if(!desc || owner < 0) return -2;
    uint64_t wbytes = (uint64_t)desc->w * desc->h * 4u;
    if(!desc->w || !desc->h || wbytes > VQEAF_WINDOW_MAX_BYTES) return -3;
    if(desc->x >= VQEAF_FB_WIDTH || desc->y >= VQEAF_FB_HEIGHT) return -4;
    if(desc->z < -1024 || desc->z > 1024) return -5;
    uint64_t va = desc->buffer_va;
    if((va & 0xFFFull) || va < VQEAF_USER_REGION_BASE ||
       va >= VQEAF_USER_REGION_LIMIT) return -6;
    uint64_t pages = (wbytes + VQEAF_PAGE_SIZE - 1) / VQEAF_PAGE_SIZE;
    if(va + pages * VQEAF_PAGE_SIZE > VQEAF_USER_REGION_LIMIT) return -7;
    if(!(desc->flags & VQEAF_WINDOW_FLAG_VISIBLE)) return -8;

    int slot = -1;
    for(unsigned i = 0; i < VQEAF_WINDOW_MAX_COUNT; i++)
        if(!wins[i].used) { slot = (int)i; break; }
    if(slot < 0) return -9;

    uint64_t root = vqeaf_vm_current_root();
    if(!root) return -10;
    uint64_t pa = vqeaf_pmm_alloc_pages(pages);
    if(!pa) return -11;

    /* Map page by page: a partially mapped range must not be overwritten. */
    for(uint64_t i = 0; i < pages; i++) {
        if(vqeaf_vm_map(root, va + i * VQEAF_PAGE_SIZE,
                        pa + i * VQEAF_PAGE_SIZE,
                        VQEAF_VM_WRITE | VQEAF_VM_USER) != 0) return -12;
    }
    volatile uint32_t *buf = (volatile uint32_t *)pa;
    for(uint64_t i = 0; i < wbytes / 4u; i++) buf[i] = 0xFF101010u;
    __asm__ volatile("dsb sy" ::: "memory");

    vqeaf_window_t *w = &wins[slot];
    w->used = 1;
    w->id = g_next_id++;
    w->owner = owner;
    w->x = desc->x; w->y = desc->y; w->w = desc->w; w->h = desc->h;
    w->z = desc->z; w->flags = desc->flags;
    w->buffer_va = va; w->buffer_pa = buf;
    w->bytes = (uint32_t)wbytes; w->pages = pages;
    w->dirty = 1; w->key_head = w->key_tail = 0;

    vqeaf_uart_write("WM: create win=");
    vqeaf_uart_u64(w->id);
    vqeaf_uart_write(" owner=");
    vqeaf_uart_u64((uint64_t)owner);
    vqeaf_uart_write(" z=");
    vqeaf_uart_u64((uint64_t)(int64_t)w->z);
    vqeaf_uart_write(" bounds=");
    vqeaf_uart_u64(w->x); vqeaf_uart_write(",");
    vqeaf_uart_u64(w->y); vqeaf_uart_write(",");
    vqeaf_uart_u64(w->w); vqeaf_uart_write("x");
    vqeaf_uart_u64(w->h);
    vqeaf_uart_write("\r\n");
    recompute_focus();
    g_generation++;
    return (int)w->id;
}

int vqeaf_wm_present(uint32_t id, int32_t owner)
{
    vqeaf_window_t *w = find(id);
    if(!w || w->owner != owner) return -2;
    w->dirty = 1;
    vqeaf_uart_write("WM: present win=");
    vqeaf_uart_u64(id);
    vqeaf_uart_write("\r\n");
    g_generation++;
    return 0;
}

int vqeaf_wm_set_z(uint32_t id, int32_t owner, int32_t z)
{
    vqeaf_window_t *w = find(id);
    if(!w || w->owner != owner || z < -1024 || z > 1024) return -2;
    w->z = z; w->dirty = 1;
    vqeaf_uart_write("WM: raise win=");
    vqeaf_uart_u64(id);
    vqeaf_uart_write(" z=");
    vqeaf_uart_u64((uint64_t)(int64_t)z);
    vqeaf_uart_write("\r\n");
    recompute_focus();
    g_generation++;
    return 0;
}

int vqeaf_wm_close(uint32_t id, int32_t owner)
{
    vqeaf_window_t *w = find(id);
    if(!w || w->owner != owner) return -2;
    w->used = 0; w->dirty = 0;
    vqeaf_uart_write("WM: close win=");
    vqeaf_uart_u64(id);
    vqeaf_uart_write("\r\n");
    recompute_focus();
    g_generation++;
    return 0;
}

void vqeaf_wm_close_owner(int32_t owner)
{
    for(unsigned i = 0; i < VQEAF_WINDOW_MAX_COUNT; i++)
        if(wins[i].used && wins[i].owner == owner)
            vqeaf_wm_close(wins[i].id, owner);
}

void vqeaf_input_push(uint8_t key)
{
    if(g_focus < 0) return;               /* no focused layer: drop honestly */
    vqeaf_window_t *w = &wins[g_focus];
    uint32_t next = (w->key_head + 1) % VQEAF_INPUT_QUEUE;
    if(next == w->key_tail) return;       /* bounded queue, never overflow */
    vqeaf_key_event_t *e = &w->keys[w->key_head];
    e->type = VQEAF_KEY_EVENT_PRESS;
    e->key = key; e->mods = 0; e->seq = ++g_seq;
    __asm__ volatile("dsb sy" ::: "memory");
    w->key_head = next;
    g_deliveries++;
    g_generation++;
}

int vqeaf_wm_poll_input(uint32_t id, int32_t owner, vqeaf_key_event_t *out)
{
    vqeaf_window_t *w = find(id);
    if(!w || w->owner != owner || !out) return -2;
    if(w->key_tail == w->key_head) return 0;
    *out = w->keys[w->key_tail];
    w->key_tail = (w->key_tail + 1) % VQEAF_INPUT_QUEUE;
    return 1;
}

int vqeaf_wm_count(void)
{
    int n = 0;
    for(unsigned i = 0; i < VQEAF_WINDOW_MAX_COUNT; i++) if(wins[i].used) n++;
    return n;
}

int vqeaf_wm_focused(void)
{
    return g_focus >= 0 ? (int)wins[g_focus].id : -1;
}

uint32_t vqeaf_wm_deliveries(void) { return g_deliveries; }
uint32_t vqeaf_wm_generation(void) { return g_generation; }
int vqeaf_wm_frame_done(void) { return g_frame_done; }
void vqeaf_wm_set_frame_done(void) { g_frame_done = 1; }

/* Blit the visible window stack (ascending z) over an already prepared
 * framebuffer. The compositor owns the background + F2c legacy pass. */
void vqeaf_wm_compose(volatile uint32_t *fb)
{
    /* Stable insertion sort by ascending z; higher id wins on a tie. */
    int order[VQEAF_WINDOW_MAX_COUNT];
    int n = 0;
    for(unsigned i = 0; i < VQEAF_WINDOW_MAX_COUNT; i++) {
        vqeaf_window_t *w = &wins[i];
        if(!w->used || !(w->flags & VQEAF_WINDOW_FLAG_VISIBLE)) continue;
        int j = n++;
        while(j > 0) {
            vqeaf_window_t *p = &wins[order[j - 1]];
            if(p->z < w->z || (p->z == w->z && p->id < w->id)) break;
            order[j] = order[j - 1]; j--;
        }
        order[j] = (int)i;
    }
    for(int k = 0; k < n; k++) {
        vqeaf_window_t *w = &wins[order[k]];
        for(uint32_t y = 0; y < w->h; y++) {
            uint32_t sy = w->y + y;
            if(sy >= VQEAF_FB_HEIGHT) break;
            for(uint32_t x = 0; x < w->w; x++) {
                uint32_t sx = w->x + x;
                if(sx >= VQEAF_FB_WIDTH) break;
                fb[sy * VQEAF_FB_WIDTH + sx] =
                    w->buffer_pa[(uint32_t)y * w->w + x];
            }
        }
        w->dirty = 0;
    }
    __asm__ volatile("dsb sy" ::: "memory");
}
