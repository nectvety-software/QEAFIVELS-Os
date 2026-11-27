#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vqeaf/capability.h"
#include "vqeaf/sm6150_display.h"
#include "vqeaf/sm6150_panel.h"

static const char *events[64];
static uint32_t event_count;
static uint32_t fail_stage;
static uint32_t dsi_version;
static int dsi_probe_result;
static int dsi_configure_result;
static int dsi_enable_result;
static int dsi_wait_result;
static uint32_t dsi_disable_count;

static void event(const char *name)
{
    assert(event_count < 64u);
    events[event_count++] = name;
}

static void reset_events(void)
{
    event_count = 0u;
    fail_stage = 0u;
    dsi_version = VQEAF_SM6150_DSI_6G_V2_3_1;
    dsi_probe_result = 0;
    dsi_configure_result = 0;
    dsi_enable_result = 0;
    dsi_wait_result = 0;
    dsi_disable_count = 0u;
    vqeaf_cap_reset();
}

static int fake_clocks(void *context, int enabled)
{
    (void)context;
    event(enabled ? "clocks on" : "clocks off");
    return enabled && fail_stage == 1u ? -10 : 0;
}

static int fake_regulators(void *context, int enabled)
{
    (void)context;
    event(enabled ? "regulators on" : "regulators off");
    return enabled && fail_stage == 2u ? -20 : 0;
}

static int fake_gpio(void *context, int high)
{
    (void)context;
    event(high ? "reset high" : "reset low");
    return fail_stage == 3u ? -30 : 0;
}

static void fake_delay(void *context, uint32_t microseconds)
{
    (void)context;
    if (microseconds == 1000u) event("delay 1ms");
    else if (microseconds == 10000u) event("delay 10ms");
    else event("delay other");
}

static int fake_panel_initialize(void *context)
{
    (void)context;
    event("panel initialize");
    return fail_stage == 4u ? -40 : 0;
}

static int fake_panel_deinitialize(void *context)
{
    (void)context;
    event("panel deinitialize");
    return 0;
}

static int fake_backlight(void *context, int enabled)
{
    (void)context;
    event(enabled ? "backlight on" : "backlight off");
    return enabled && fail_stage == 7u ? -70 : 0;
}

static int fake_dpu_start(void *context, const vqeaf_sm6150_dsi_mode_t *mode,
                          uint64_t framebuffer_pa, uint32_t stride)
{
    (void)context;
    assert(mode != NULL && mode->lane_count == 4u);
    assert(framebuffer_pa == 0x80000000ull && stride == 4320u);
    event("dpu start");
    return fail_stage == 6u ? -60 : 0;
}

static int fake_dpu_stop(void *context)
{
    (void)context;
    event("dpu stop");
    return 0;
}

static const vqeaf_sm6150_display_ops_t ops = {
    fake_clocks, fake_regulators, fake_gpio, fake_delay, fake_panel_initialize,
    fake_panel_deinitialize, fake_backlight, fake_dpu_start, fake_dpu_stop
};

static uint32_t fake_read(void *context, uint32_t offset)
{
    (void)context;
    (void)offset;
    return dsi_version;
}

static void fake_write(void *context, uint32_t offset, uint32_t value)
{
    (void)context;
    (void)offset;
    (void)value;
}

static void fake_dsi_delay(void *context, uint32_t microseconds)
{
    (void)context;
    assert(microseconds == 20000u);
}

static const vqeaf_sm6150_dsi_bus_t dsi_bus = {
    fake_read, fake_write, fake_dsi_delay, NULL, 100u
};

int vqeaf_sm6150_dsi_probe(const vqeaf_sm6150_dsi_bus_t *bus, uint32_t *version)
{
    assert(bus == &dsi_bus && version != NULL);
    event("dsi probe");
    *version = dsi_version;
    return dsi_probe_result;
}

int vqeaf_sm6150_dsi_configure(const vqeaf_sm6150_dsi_bus_t *bus,
                               const vqeaf_sm6150_dsi_mode_t *mode)
{
    assert(bus == &dsi_bus && mode != NULL);
    event("dsi configure");
    return dsi_configure_result;
}

int vqeaf_sm6150_dsi_enable(const vqeaf_sm6150_dsi_bus_t *bus)
{
    assert(bus == &dsi_bus);
    event("dsi enable");
    return dsi_enable_result;
}

int vqeaf_sm6150_dsi_wait_video_done(const vqeaf_sm6150_dsi_bus_t *bus)
{
    assert(bus == &dsi_bus);
    event("dsi wait video done");
    return dsi_wait_result;
}

int vqeaf_sm6150_dsi_disable(const vqeaf_sm6150_dsi_bus_t *bus)
{
    assert(bus == &dsi_bus);
    ++dsi_disable_count;
    event("dsi disable");
    return 0;
}

static void init_info(vqeaf_violet_display_info_t *info)
{
    static const char *const supplies[] = {"vdda", "vcca", "vddi", "avdd", "avee"};
    memset(info, 0, sizeof(*info));
    info->found = 1u;
    info->mdss_found = info->dpu_found = info->dsi_found = 1u;
    info->phy_found = info->dispcc_found = info->panel_found = 1u;
    info->mdss_base = 0x0ae00000ull;
    info->dpu_base = 0x0ae01000ull;
    info->dsi_base = 0x0ae94000ull;
    info->phy_base = 0x0ae94400ull;
    info->dispcc_base = 0x0af00000ull;
    info->dsi_clock_count = 6u;
    info->lane_count = 4u;
    info->lanes[0] = 1u;
    info->lanes[1] = 2u;
    info->lanes[2] = 3u;
    info->lanes[3] = 0u;
    info->reset_phandle = 0x41u;
    info->backlight_phandle = 0xf4u;
    info->supply_count = 5u;
    for (uint32_t i = 0; i < 5u; ++i)
        strcpy(info->supplies[i].name, supplies[i]);
    info->panel_timing_present = 1u;
    info->panel_command_data_present = 1u;
}

static void init_device(vqeaf_sm6150_display_device_t *device,
                        const vqeaf_violet_display_info_t *info)
{
    memset(device, 0, sizeof(*device));
    device->info = info;
    device->ops = &ops;
    device->dsi_bus = &dsi_bus;
    device->boot_handoff_verified = 1u;
    device->mmu_enabled = 1u;
    device->panel_commands_verified = 1u;
    device->panel_mode = vqeaf_sm6150_panel_reference_mode();
    device->framebuffer_pa = 0x80000000ull;
    device->framebuffer_stride = 4320u;
}

static void assert_capability(VqeafCapabilityState state, int32_t detail)
{
    VqeafCapability capability;
    assert(vqeaf_cap_get("display", &capability) == 0);
    assert(capability.state == state && capability.detail == detail);
}

static void assert_event(uint32_t index, const char *name)
{
    assert(index < event_count);
    assert(strcmp(events[index], name) == 0);
}

static void test_deferral_gates(void)
{
    vqeaf_violet_display_info_t info;
    vqeaf_sm6150_display_device_t device;
    const VqeafDriver *driver = vqeaf_sm6150_display_driver();

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    info.dsi_found = 0u;
    assert(driver->probe(&device) == VQEAF_DRIVER_DEFER);
    assert(event_count == 0u);
    assert_capability(VQEAF_CAP_PRESENT, -1);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    device.boot_handoff_verified = 0u;
    assert(driver->probe(&device) == VQEAF_DRIVER_DEFER);
    assert(event_count == 0u);
    assert_capability(VQEAF_CAP_PRESENT, -2);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    info.panel_timing_present = 0u;
    assert(driver->probe(&device) == VQEAF_DRIVER_DEFER);
    assert(event_count == 0u);
    assert_capability(VQEAF_CAP_BLOCKED_FIRMWARE, -3);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    info.panel_command_data_present = 0u;
    assert(driver->probe(&device) == VQEAF_DRIVER_DEFER);
    assert_capability(VQEAF_CAP_BLOCKED_FIRMWARE, -3);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    device.panel_mode = NULL;
    assert(driver->probe(&device) == VQEAF_DRIVER_DEFER);
    assert(event_count == 0u);
    assert_capability(VQEAF_CAP_PRESENT, -4);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    device.framebuffer_stride = 4319u;
    assert(driver->probe(&device) == VQEAF_DRIVER_DEFER);
    assert(event_count == 0u);
    assert_capability(VQEAF_CAP_PRESENT, -4);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    strcpy(info.supplies[4].name, "missing");
    assert(driver->probe(&device) == VQEAF_DRIVER_DEFER);
    assert(event_count == 0u);
    assert_capability(VQEAF_CAP_PRESENT, -1);
}

static void test_failure_cleanup(void)
{
    vqeaf_violet_display_info_t info;
    vqeaf_sm6150_display_device_t device;
    const VqeafDriver *driver = vqeaf_sm6150_display_driver();

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    dsi_version = 0x20020001u;
    assert(driver->probe(&device) != 0);
    assert_event(0, "clocks on");
    assert_event(1, "regulators on");
    assert_event(2, "dsi probe");
    assert_event(3, "regulators off");
    assert_event(4, "clocks off");
    assert(event_count == 5u);
    assert(device.active == 0u);
    assert_capability(VQEAF_CAP_ERROR, -5);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    fail_stage = 4u;
    assert(driver->probe(&device) == -40);
    assert_event(event_count - 2u, "regulators off");
    assert_event(event_count - 1u, "clocks off");
    assert(dsi_disable_count == 0u && device.active == 0u);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    dsi_wait_result = -80;
    assert(driver->probe(&device) == -80);
    assert_event(event_count - 5u, "panel deinitialize");
    assert_event(event_count - 4u, "dpu stop");
    assert_event(event_count - 3u, "dsi disable");
    assert_event(event_count - 2u, "regulators off");
    assert_event(event_count - 1u, "clocks off");
    assert(dsi_disable_count == 1u && device.active == 0u);
    assert_capability(VQEAF_CAP_ERROR, -80);

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    fail_stage = 7u;
    assert(driver->probe(&device) == -70);
    assert_event(event_count - 5u, "panel deinitialize");
    assert_event(event_count - 4u, "dpu stop");
    assert_event(event_count - 3u, "dsi disable");
    assert_event(event_count - 2u, "regulators off");
    assert_event(event_count - 1u, "clocks off");
    assert(dsi_disable_count == 1u && device.active == 0u);
    assert_capability(VQEAF_CAP_ERROR, -70);
}

static void test_success_and_remove(void)
{
    vqeaf_violet_display_info_t info;
    vqeaf_sm6150_display_device_t device;
    const VqeafDriver *driver = vqeaf_sm6150_display_driver();

    init_info(&info);
    init_device(&device, &info);
    reset_events();
    assert(driver->probe(&device) == 0);
    assert(device.active == 1u);
    assert(event_count == 15u);
    assert_event(0, "clocks on");
    assert_event(1, "regulators on");
    assert_event(2, "dsi probe");
    assert_event(3, "reset high");
    assert_event(4, "delay 1ms");
    assert_event(5, "reset low");
    assert_event(6, "delay 1ms");
    assert_event(7, "reset high");
    assert_event(8, "delay 10ms");
    assert_event(9, "dsi configure");
    assert_event(10, "panel initialize");
    assert_event(11, "dsi enable");
    assert_event(12, "dpu start");
    assert_event(13, "dsi wait video done");
    assert_event(14, "backlight on");
    assert_capability(VQEAF_CAP_READY, 0);

    reset_events();
    assert(driver->remove(&device) == 0);
    assert(device.active == 0u);
    assert(event_count == 6u);
    assert_event(0, "backlight off");
    assert_event(1, "panel deinitialize");
    assert_event(2, "dpu stop");
    assert_event(3, "dsi disable");
    assert_event(4, "regulators off");
    assert_event(5, "clocks off");
    assert(dsi_disable_count == 1u);
    assert_capability(VQEAF_CAP_PRESENT, 0);
}

int main(void)
{
    test_deferral_gates();
    test_failure_cleanup();
    test_success_and_remove();
    puts("SM6150 display lifecycle host tests: PASS");
    return 0;
}
