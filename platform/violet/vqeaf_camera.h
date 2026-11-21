#ifndef VQEAF_CAMERA_H
#define VQEAF_CAMERA_H
#include <stdint.h>

/* platform/violet/vqeaf_camera.h - native V4L2 camera access.
 *
 * The violet stock kernel already contains the camera sensor/ISP drivers and
 * registers V4L2 nodes. This module talks to those kernel nodes directly, so
 * no vendor camera HAL, Android runtime, binder service or vendor .so is
 * needed. It never claims a capability READY from a node list alone: READY
 * requires a real successful capture.
 *
 * The role classifier and report formatter are pure and host-testable. The
 * ioctl paths are compiled only for Linux targets. */

typedef enum {
    VQEAF_V4L2_ROLE_OTHER = 0,
    VQEAF_V4L2_ROLE_CONTROL,  /* cam-req-mgr, cam_sync: control, not a stream */
    VQEAF_V4L2_ROLE_ROTATOR,  /* sde_rotator: display path, not a camera */
    VQEAF_V4L2_ROLE_CODEC,    /* msm_vidc venc/vdec: codec, not a camera */
    VQEAF_V4L2_ROLE_CAPTURE,  /* videoN stream candidate */
} VqeafV4l2Role;

VqeafV4l2Role vqeaf_v4l2_role(const char *name);
const char *vqeaf_v4l2_role_name(VqeafV4l2Role role);

/* Qualcomm cam-req-mgr kernel UAPI (include/uapi/media/cam_req_mgr.h).
 * VIDIOC_CAM_CONTROL = _IOWR('V', 192, struct cam_control) = 0xc01856c0.
 * Opcode 267 = CREATE_SESSION; verified against the kernel header where
 * CREATE_DEV_NODES is CAM_COMMON_OPCODE_MAX+1 and CREATE_SESSION is +2. */
#define VQEAF_CAM_CONTROL_IOCTL   0xc01856c0u
#define VQEAF_CAM_CREATE_DEV_NODES 266
#define VQEAF_CAM_CREATE_SESSION   267
#define VQEAF_CAM_DESTROY_SESSION  268
#define VQEAF_CAM_QUERY_CAP        257
#define VQEAF_CAM_ACQUIRE_DEV      258
#define VQEAF_CAM_RELEASE_DEV      262
#define VQEAF_CAM_LINK             269
#define VQEAF_CAM_HANDLE_USER_PTR  1

/* Device-specific UAPI values from cam_isp.h, cam_icp.h and cam_sensor.h. */
#define VQEAF_CAM_ISP_HW_MAX           5
#define VQEAF_CAM_ISP_RES_ID_PORT      0
#define VQEAF_CAM_ISP_LANE_TYPE_DPHY   0
#define VQEAF_CAM_ISP_IFE_IN_RES_PHY_0 0x4001u
#define VQEAF_CAM_ISP_IFE_OUT_RES_RDI_0 0x3006u
#define VQEAF_CAM_ISP_DSP_MODE_NONE    0
#define VQEAF_CAM_FORMAT_MIPI_RAW_10   3
#define VQEAF_CAM_MIPI_DT_RAW10        0x2au
#define VQEAF_CAM_ICP_RES_TYPE_BPS     1
#define VQEAF_CAM_ICP_DEV_TYPE_BPS     3

typedef struct {
    int32_t op_code;
    int32_t size;
    int32_t handle_type;
    int32_t reserved;
    uint64_t handle;
} VqeafCamControl;

/* Fill the ioctl wrapper. Pure and host-testable. */
void vqeaf_cam_control_init(VqeafCamControl *ctl, int32_t op_code,
                            void *payload, int32_t payload_size);

/* Kernel UAPI layouts used by CAM_QUERY_CAP / CAM_ACQUIRE_DEV / CAM_REQ_MGR_LINK.
 * Sizes are fixed by the kernel ABI and asserted in the gate. */
typedef struct { uint32_t size; uint32_t handle_type; uint64_t caps_handle; } VqeafCamQueryCap;
typedef struct { int32_t non_secure; int32_t secure; } VqeafCamIommu;
typedef struct { uint32_t major, minor, incr, reserved; } VqeafCamHwVersion;
typedef struct {
    VqeafCamIommu device_iommu, cdm_iommu;
    int32_t num_dev; uint32_t reserved;
    struct { uint32_t hw_type, reserved; VqeafCamHwVersion hw_version; } dev_caps[5];
} VqeafCamIspCaps;
typedef struct {
    VqeafCamIommu dev_iommu, cdm_iommu;
    struct { uint32_t major, minor, revision, reserved; } fw_version, api_version;
    uint32_t num_ipe, num_bps;
    struct { uint32_t dev_type, reserved; VqeafCamHwVersion hw_ver; } dev_ver[5];
} VqeafCamIcpCaps;
typedef struct {
    int32_t session_handle, dev_handle;
    uint32_t handle_type, num_resources;
    uint64_t resource_hdl;
} VqeafCamAcquire;
typedef struct {
    int32_t session_handle; uint32_t num_devices;
    int32_t dev_hdls[64]; int32_t link_hdl;
} VqeafCamLink;
typedef struct {
    uint32_t resource_id, length, handle_type, reserved; uint64_t res_hdl;
} VqeafCamIspResource;
typedef struct {
    uint32_t res_type, lane_type, lane_num, lane_cfg;
    uint32_t vc, dt, format, test_pattern, usage_type;
    uint32_t left_start, left_stop, left_width;
    uint32_t right_start, right_stop, right_width;
    uint32_t line_start, line_stop, height;
    uint32_t pixel_clk, batch_size, dsp_mode, hbi_cnt, reserved, num_out_res;
    struct { uint32_t res_type, format, width, height,
                     comp_grp_id, split_point, secure_mode, reserved; } data[1];
} VqeafCamIspInPort;
typedef struct {
    uint32_t slot_info, secure_camera, pos_pitch, pos_roll, pos_yaw;
    uint32_t actuator_slot_id, eeprom_slot_id, ois_slot_id, flash_slot_id;
    uint32_t csiphy_slot_id;
} VqeafCamSensorCaps;
typedef struct { uint32_t format, width, height, fps; } VqeafCamIcpResInfo;
typedef struct {
    uint32_t scratch_mem_size, dev_type, io_config_cmd_size;
    int32_t io_config_cmd_handle;
    uint32_t secure_mode;
    int32_t chain_info;
    VqeafCamIcpResInfo in_res;
    uint32_t num_out_res;
    VqeafCamIcpResInfo out_res[1];
} VqeafCamIcpAcquireInfo;
typedef struct { int32_t session_handle, dev_handle; } VqeafCamRelease;

/* Query ISP/ICP/sensor capabilities and try acquire+link. Both report real
 * kernel results; neither claims a usable stream node. */
int vqeaf_camera_query_caps(char *buf, int cap);
int vqeaf_camera_acquire_link(char *buf, int cap);

/* Create a real cam-req-mgr session on /dev/video0. Returns 0 on success. */
int vqeaf_camera_create_session(int32_t *session_handle,
                                char *err, int err_cap);

/* List the real v4l-subdev nodes and their kernel names (cam-isp, cam-icp,
 * cam-sensor-driver, ...). Returns bytes written or negative errno. */
int vqeaf_camera_subdevs(char *buf, int cap);

typedef struct {
    char node[64];
    char driver[32];
    char card[64];
    char bus_info[64];
    uint32_t version;
    uint32_t capabilities;
    uint32_t device_caps;
    VqeafV4l2Role role;
} VqeafCameraNode;

/* Pure: render a census of already-collected node information. */
int vqeaf_camera_report(const VqeafCameraNode *nodes, int count,
                        char *buf, int cap);

/* Linux only. Returns 0 on success, negative errno otherwise. */
int vqeaf_camera_query(const char *path, VqeafCameraNode *out);

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bytes_used;
    uint32_t hash;
    unsigned char *data;
} VqeafCameraFrame;

/* Capture one frame from a V4L2 capture node into heap memory. Caller must
 * free(frame->data). Returns 0 on success, negative errno otherwise. */
int vqeaf_camera_grab(const char *path, VqeafCameraFrame *out,
                      char *err, int err_cap);

/* Full live census: enumerates /sys/class/video4linux, queries each node and
 * renders the report. Returns bytes written, or negative errno. */
int vqeaf_camera_probe(char *buf, int cap);

#endif
