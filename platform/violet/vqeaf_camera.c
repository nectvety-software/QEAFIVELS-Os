/* platform/violet/vqeaf_camera.c - native V4L2 camera access for violet.
 *
 * This is a direct port of the camera hardware path, not a port of the vendor
 * camera HAL. The stock kernel already owns the sensors, CSID/ISP and V4L2
 * subdevices; this file only uses the stable kernel UAPI on /dev/videoN.
 * No Android runtime, vendor .so, binder service or Java code is involved.
 *
 * A node list is evidence of a driver, not of a working camera: the caller
 * must run vqeaf_camera_grab() before marking the capability READY. */
#include "vqeaf_camera.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int appendf(char *buf, int cap, int *off, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (!buf || cap <= 0 || !off || *off < 0 || *off >= cap) return -1;
    va_start(ap, fmt);
    n = vsnprintf(buf + *off, (size_t)(cap - *off), fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if (n >= cap - *off) {
        buf[cap - 1] = '\0';
        *off = cap - 1;
        return -1;
    }
    *off += n;
    return 0;
}

static void lower_copy(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!dst || cap <= 0) return;
    if (!src) { dst[0] = '\0'; return; }
    for (; src[i] && i < cap - 1; i++) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    dst[i] = '\0';
}

VqeafV4l2Role vqeaf_v4l2_role(const char *name)
{
    char low[96];
    int i;

    lower_copy(low, (int)sizeof(low), name);
    if (strstr(low, "cam-req-mgr") || strstr(low, "cam_sync") ||
        strstr(low, "cam-sync")) return VQEAF_V4L2_ROLE_CONTROL;
    if (strstr(low, "rotator")) return VQEAF_V4L2_ROLE_ROTATOR;
    if (strstr(low, "vidc") || strstr(low, "venc") || strstr(low, "vdec"))
        return VQEAF_V4L2_ROLE_CODEC;
    if (strncmp(low, "video", 5) != 0) return VQEAF_V4L2_ROLE_OTHER;
    for (i = 5; low[i]; i++)
        if (low[i] < '0' || low[i] > '9') return VQEAF_V4L2_ROLE_OTHER;
    return i > 5 ? VQEAF_V4L2_ROLE_CAPTURE : VQEAF_V4L2_ROLE_OTHER;
}

const char *vqeaf_v4l2_role_name(VqeafV4l2Role role)
{
    switch (role) {
    case VQEAF_V4L2_ROLE_CONTROL: return "control";
    case VQEAF_V4L2_ROLE_ROTATOR: return "rotator";
    case VQEAF_V4L2_ROLE_CODEC: return "codec";
    case VQEAF_V4L2_ROLE_CAPTURE: return "capture";
    default: return "other";
    }
}

int vqeaf_camera_report(const VqeafCameraNode *nodes, int count,
                        char *buf, int cap)
{
    int off = 0, n, capture = 0, control = 0, rotator = 0, codec = 0, other = 0;

    if (!buf || cap <= 0) return 0;
    if (count < 0) count = 0;
    for (int i = 0; i < count; i++) {
        if (nodes && nodes[i].role == VQEAF_V4L2_ROLE_CAPTURE) capture++;
        else if (nodes && nodes[i].role == VQEAF_V4L2_ROLE_CONTROL) control++;
        else if (nodes && nodes[i].role == VQEAF_V4L2_ROLE_ROTATOR) rotator++;
        else if (nodes && nodes[i].role == VQEAF_V4L2_ROLE_CODEC) codec++;
        else other++;
    }
    n = snprintf(buf + off, (size_t)(cap - off),
                 "cam: v4l2 nodes=%d capture=%d control=%d rotator=%d codec=%d other=%d\n",
                 count, capture, control, rotator, codec, other);
    if (n < 0 || n >= cap - off) { buf[cap - 1] = '\0'; return cap - 1; }
    off += n;
    if (!nodes) { buf[off] = '\0'; return off; }
    for (int i = 0; i < count; i++) {
        const VqeafCameraNode *p = &nodes[i];
        n = snprintf(buf + off, (size_t)(cap - off),
                     "  %-12s %-8s driver=%.31s card=%.63s caps=0x%08x\n",
                     p->node, vqeaf_v4l2_role_name(p->role),
                     p->driver, p->card, p->capabilities);
        if (n < 0 || n >= cap - off) break;
        off += n;
    }
    buf[off] = '\0';
    return off;
}

void vqeaf_cam_control_init(VqeafCamControl *ctl, int32_t op_code,
                            void *payload, int32_t payload_size)
{
    if (!ctl) return;
    memset(ctl, 0, sizeof(*ctl));
    ctl->op_code = op_code;
    ctl->size = payload ? payload_size : 8;
    ctl->handle_type = payload ? VQEAF_CAM_HANDLE_USER_PTR : 0;
    ctl->handle = payload ? (uint64_t)(uintptr_t)payload : 0u;
}

#ifdef __linux__

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

static int set_error(char *err, int cap, const char *what, int rc)
{
    int code = rc < 0 ? rc : (rc > 0 ? -rc : -EIO);
    if (err && cap > 0)
        snprintf(err, (size_t)cap, "%s (errno %d)", what, -code);
    return code;
}

int vqeaf_camera_create_session(int32_t *out, char *err, int err_cap)
{
    struct { int32_t session_hdl; int32_t reserved; } info;
    VqeafCamControl ctl;
    int fd, rc;

    if (!out) return -EINVAL;
    *out = 0;
    fd = open("/dev/video0", O_RDWR | O_CLOEXEC);
    if (fd < 0) return set_error(err, err_cap, "cam-req-mgr open failed", -errno);
    memset(&info, 0, sizeof(info));
    vqeaf_cam_control_init(&ctl, VQEAF_CAM_CREATE_SESSION, &info,
                           (int32_t)sizeof(info));
    rc = ioctl(fd, VQEAF_CAM_CONTROL_IOCTL, &ctl);
    if (rc < 0) {
        rc = -errno;
        close(fd);
        return set_error(err, err_cap, "CAM_REQ_MGR_CREATE_SESSION failed", rc);
    }
    close(fd);
    *out = info.session_hdl;
    return 0;
}

static int cam_control_call(int fd, int32_t op, void *payload, int32_t size)
{
    VqeafCamControl ctl;

    vqeaf_cam_control_init(&ctl, op, payload, size);
    return ioctl(fd, VQEAF_CAM_CONTROL_IOCTL, &ctl);
}

/* Subdev numbering on violet is not guaranteed to be contiguous, so scan by
 * index instead of trusting readdir order. */
static int subdev_find(const char *needle, int nth, char *node, int node_cap,
                       char *name, int name_cap)
{
    if (!needle || !node || node_cap <= 0) return -EINVAL;
    for (int i = 0; i < 128; i++) {
        char sp[192], raw[128] = "";
        int fd, n;

        snprintf(sp, sizeof(sp), "/sys/class/video4linux/v4l-subdev%d/name", i);
        fd = open(sp, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        n = (int)read(fd, raw, sizeof(raw) - 1);
        close(fd);
        if (n <= 0) continue;
        raw[n] = '\0';
        for (int k = 0; raw[k]; k++)
            if (raw[k] == '\n' || raw[k] == '\r') raw[k] = '\0';
        if (!strstr(raw, needle)) continue;
        if (nth-- > 0) continue;
        snprintf(node, (size_t)node_cap, "/dev/v4l-subdev%d", i);
        if (name && name_cap > 0)
            snprintf(name, (size_t)name_cap, "%s", raw);
        return 0;
    }
    return -ENOENT;
}

static int cap_query(int fd, void *caps, int caps_size, char *buf, int cap,
                     int *off, const char *label)
{
    VqeafCamQueryCap q;
    int rc;

    memset(&q, 0, sizeof(q));
    memset(caps, 0, (size_t)caps_size);
    q.size = (uint32_t)caps_size;
    q.handle_type = VQEAF_CAM_HANDLE_USER_PTR;
    q.caps_handle = (uint64_t)(uintptr_t)caps;
    rc = cam_control_call(fd, VQEAF_CAM_QUERY_CAP, &q, (int32_t)sizeof(q));
    if (rc < 0) {
        rc = -errno;
        appendf(buf, cap, off, "  %s: CAM_QUERY_CAP failed (errno %d)\n",
                label, -rc);
    } else {
        appendf(buf, cap, off, "  %s: CAM_QUERY_CAP ok (payload %d bytes)\n",
                label, caps_size);
    }
    return rc;
}

static void report_isp_caps(const VqeafCamIspCaps *c, char *buf, int cap,
                            int *off)
{
    appendf(buf, cap, off,
            "    isp iommu=0x%08x/0x%08x cdm=0x%08x/0x%08x num_dev=%d\n",
            (unsigned)c->device_iommu.non_secure,
            (unsigned)c->device_iommu.secure,
            (unsigned)c->cdm_iommu.non_secure,
            (unsigned)c->cdm_iommu.secure, (int)c->num_dev);
    for (int i = 0; i < c->num_dev && i < VQEAF_CAM_ISP_HW_MAX; i++)
        appendf(buf, cap, off, "    isp dev[%d] type=%u ver=%u.%u.%u\n", i,
                (unsigned)c->dev_caps[i].hw_type,
                (unsigned)c->dev_caps[i].hw_version.major,
                (unsigned)c->dev_caps[i].hw_version.minor,
                (unsigned)c->dev_caps[i].hw_version.incr);
}

static void report_icp_caps(const VqeafCamIcpCaps *c, char *buf, int cap,
                            int *off)
{
    appendf(buf, cap, off,
            "    icp iommu=0x%08x/0x%08x cdm=0x%08x/0x%08x fw=%u.%u.%u api=%u.%u.%u ipe=%u bps=%u\n",
            (unsigned)c->dev_iommu.non_secure, (unsigned)c->dev_iommu.secure,
            (unsigned)c->cdm_iommu.non_secure, (unsigned)c->cdm_iommu.secure,
            (unsigned)c->fw_version.major, (unsigned)c->fw_version.minor,
            (unsigned)c->fw_version.revision,
            (unsigned)c->api_version.major, (unsigned)c->api_version.minor,
            (unsigned)c->api_version.revision,
            (unsigned)c->num_ipe, (unsigned)c->num_bps);
}

static void report_sensor_caps(const VqeafCamSensorCaps *c, char *buf, int cap,
                               int *off)
{
    appendf(buf, cap, off,
            "    sensor slot=%u secure=%u pos=%u/%u/%u actuator=%u eeprom=%u ois=%u flash=%u csiphy=%u\n",
            (unsigned)c->slot_info, (unsigned)c->secure_camera,
            (unsigned)c->pos_pitch, (unsigned)c->pos_roll,
            (unsigned)c->pos_yaw, (unsigned)c->actuator_slot_id,
            (unsigned)c->eeprom_slot_id, (unsigned)c->ois_slot_id,
            (unsigned)c->flash_slot_id, (unsigned)c->csiphy_slot_id);
}

int vqeaf_camera_query_caps(char *buf, int cap)
{
    VqeafCamIspCaps isp;
    VqeafCamIcpCaps icp;
    VqeafCamSensorCaps sensor;
    char node[64], name[64], label[80];
    int off = 0, rc, fd;

    if (!buf || cap <= 0) return -EINVAL;
    buf[0] = '\0';
    appendf(buf, cap, &off, "cam caps: CAM_QUERY_CAP on real v4l-subdev nodes\n");

    rc = subdev_find("cam-isp", 0, node, (int)sizeof(node),
                     name, (int)sizeof(name));
    if (rc < 0) {
        appendf(buf, cap, &off, "  isp: no cam-isp subdev (errno %d)\n", -rc);
    } else {
        appendf(buf, cap, &off, "  isp: %s (%s)\n", node, name);
        fd = open(node, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            appendf(buf, cap, &off, "  isp: open failed (errno %d)\n", errno);
        else {
            rc = cap_query(fd, &isp, (int)sizeof(isp), buf, cap, &off, "isp");
            if (rc == 0) report_isp_caps(&isp, buf, cap, &off);
            close(fd);
        }
    }

    rc = subdev_find("cam-icp", 0, node, (int)sizeof(node),
                     name, (int)sizeof(name));
    if (rc < 0) {
        appendf(buf, cap, &off, "  icp: no cam-icp subdev (errno %d)\n", -rc);
    } else {
        appendf(buf, cap, &off, "  icp: %s (%s)\n", node, name);
        fd = open(node, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            appendf(buf, cap, &off, "  icp: open failed (errno %d)\n", errno);
        else {
            rc = cap_query(fd, &icp, (int)sizeof(icp), buf, cap, &off, "icp");
            if (rc == 0) report_icp_caps(&icp, buf, cap, &off);
            close(fd);
        }
    }

    for (int i = 0; i < 8; i++) {
        rc = subdev_find("cam-sensor-driver", i, node, (int)sizeof(node),
                         name, (int)sizeof(name));
        if (rc < 0) {
            if (i == 0)
                appendf(buf, cap, &off,
                        "  sensor: no cam-sensor-driver subdev (errno %d)\n", -rc);
            break;
        }
        snprintf(label, sizeof(label), "sensor[%d]", i);
        appendf(buf, cap, &off, "  %s: %s (%s)\n", label, node, name);
        fd = open(node, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            appendf(buf, cap, &off, "  %s: open failed (errno %d)\n",
                    label, errno);
        else {
            rc = cap_query(fd, &sensor, (int)sizeof(sensor), buf, cap, &off, label);
            if (rc == 0) report_sensor_caps(&sensor, buf, cap, &off);
            close(fd);
        }
    }
    appendf(buf, cap, &off,
            "cam caps: capability query only; no stream node is claimed\n");
    return off;
}

static int acquire_dev(const char *node, int32_t session, void *resource,
                       int resource_size, int32_t *dev_handle)
{
    VqeafCamAcquire a;
    int fd, rc;

    if (!node || !dev_handle) return -EINVAL;
    *dev_handle = 0;
    fd = open(node, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -errno;
    memset(&a, 0, sizeof(a));
    a.session_handle = session;
    a.handle_type = resource ? VQEAF_CAM_HANDLE_USER_PTR : 0;
    a.num_resources = resource ? 1u : 0u;
    a.resource_hdl = resource ? (uint64_t)(uintptr_t)resource : 0u;
    rc = cam_control_call(fd, VQEAF_CAM_ACQUIRE_DEV, &a, (int32_t)sizeof(a));
    if (rc < 0) rc = -errno;
    close(fd);
    if (rc == 0) *dev_handle = a.dev_handle;
    return rc;
}

static int acquire_isp(const char *node, int32_t session, int32_t *dev_handle)
{
    VqeafCamIspResource res;
    VqeafCamIspInPort in;

    memset(&in, 0, sizeof(in));
    in.res_type = VQEAF_CAM_ISP_IFE_IN_RES_PHY_0;
    in.lane_type = VQEAF_CAM_ISP_LANE_TYPE_DPHY;
    in.lane_num = 4;
    in.lane_cfg = 0x3210;
    in.vc = 0;
    in.dt = VQEAF_CAM_MIPI_DT_RAW10;
    in.format = VQEAF_CAM_FORMAT_MIPI_RAW_10;
    in.left_start = 0;
    in.left_stop = 1919;
    in.left_width = 1920;
    in.line_start = 0;
    in.line_stop = 1079;
    in.height = 1080;
    in.dsp_mode = VQEAF_CAM_ISP_DSP_MODE_NONE;
    in.num_out_res = 1;
    in.data[0].res_type = VQEAF_CAM_ISP_IFE_OUT_RES_RDI_0;
    in.data[0].format = VQEAF_CAM_FORMAT_MIPI_RAW_10;
    in.data[0].width = 1920;
    in.data[0].height = 1080;

    memset(&res, 0, sizeof(res));
    res.resource_id = VQEAF_CAM_ISP_RES_ID_PORT;
    res.length = (uint32_t)sizeof(in);
    res.handle_type = VQEAF_CAM_HANDLE_USER_PTR;
    res.res_hdl = (uint64_t)(uintptr_t)&in;
    return acquire_dev(node, session, &res, (int)sizeof(res), dev_handle);
}

static int acquire_icp(const char *node, int32_t session, int32_t *dev_handle)
{
    VqeafCamIcpAcquireInfo info;

    memset(&info, 0, sizeof(info));
    info.dev_type = VQEAF_CAM_ICP_DEV_TYPE_BPS;
    info.in_res.format = VQEAF_CAM_FORMAT_MIPI_RAW_10;
    info.in_res.width = 1920;
    info.in_res.height = 1080;
    info.in_res.fps = 30;
    info.num_out_res = 0;
    return acquire_dev(node, session, &info, (int)sizeof(info), dev_handle);
}

static void release_dev(const char *node, int32_t session, int32_t dev_handle,
                        const char *label, char *buf, int cap, int *off)
{
    VqeafCamRelease rel;
    int fd, rc;

    if (!node || dev_handle == 0) return;
    fd = open(node, O_RDWR | O_CLOEXEC);
    if (fd < 0) return;
    memset(&rel, 0, sizeof(rel));
    rel.session_handle = session;
    rel.dev_handle = dev_handle;
    rc = cam_control_call(fd, VQEAF_CAM_RELEASE_DEV, &rel,
                          (int32_t)sizeof(rel));
    close(fd);
    appendf(buf, cap, off, "  release %s: %s\n", label,
            rc == 0 ? "ok" : "kernel refused (cleanup only)");
}

static void destroy_session(int32_t session, char *buf, int cap, int *off)
{
    struct { int32_t session_hdl; int32_t reserved; } info;
    VqeafCamControl ctl;
    int fd, rc;

    if (session == 0) return;
    fd = open("/dev/video0", O_RDWR | O_CLOEXEC);
    if (fd < 0) return;
    memset(&info, 0, sizeof(info));
    info.session_hdl = session;
    vqeaf_cam_control_init(&ctl, VQEAF_CAM_DESTROY_SESSION, &info,
                           (int32_t)sizeof(info));
    rc = ioctl(fd, VQEAF_CAM_CONTROL_IOCTL, &ctl);
    if (rc < 0) rc = -errno;
    close(fd);
    if (rc == 0)
        appendf(buf, cap, off, "  destroy session: ok\n");
    else
        appendf(buf, cap, off, "  destroy session: failed (errno %d)\n", -rc);
}

int vqeaf_camera_acquire_link(char *buf, int cap)
{
    VqeafCamLink link;
    int32_t session = 0, sensor_h = 0, isp_h = 0, icp_h = 0;
    int32_t link_hdls[3] = {0, 0, 0}, link_hdl = 0;
    char sensor_node[64] = "", isp_node[64] = "", icp_node[64] = "";
    char name[64], err[200];
    int off = 0, count = 0, rc, fd;

    if (!buf || cap <= 0) return -EINVAL;
    buf[0] = '\0';
    appendf(buf, cap, &off,
            "cam link: CAM_ACQUIRE_DEV then CAM_REQ_MGR_LINK (no stream scheduled)\n");
    rc = vqeaf_camera_create_session(&session, err, (int)sizeof(err));
    if (rc != 0) {
        appendf(buf, cap, &off, "  session: failed: %s\n", err);
        return -1;
    }
    appendf(buf, cap, &off, "  session: 0x%08x\n", (unsigned)session);

    rc = subdev_find("cam-sensor-driver", 0, sensor_node,
                     (int)sizeof(sensor_node), name, (int)sizeof(name));
    if (rc < 0) {
        appendf(buf, cap, &off, "  sensor: subdev not found (errno %d)\n", -rc);
    } else {
        rc = acquire_dev(sensor_node, session, NULL, 0, &sensor_h);
        appendf(buf, cap, &off, "  sensor %s: acquire %s", sensor_node,
                rc == 0 ? "ok handle=0x" : "failed (errno ");
        if (rc == 0)
            appendf(buf, cap, &off, "%08x\n", (unsigned)sensor_h);
        else
            appendf(buf, cap, &off, "%d)\n", -rc);
        if (rc == 0) link_hdls[count++] = sensor_h;
    }

    rc = subdev_find("cam-isp", 0, isp_node, (int)sizeof(isp_node),
                     name, (int)sizeof(name));
    if (rc < 0) {
        appendf(buf, cap, &off, "  isp: subdev not found (errno %d)\n", -rc);
    } else {
        rc = acquire_isp(isp_node, session, &isp_h);
        if (rc == 0) {
            appendf(buf, cap, &off, "  isp %s: acquire ok handle=0x%08x\n",
                    isp_node, (unsigned)isp_h);
            link_hdls[count++] = isp_h;
        } else {
            appendf(buf, cap, &off,
                    "  isp %s: acquire failed (errno %d)\n", isp_node, -rc);
        }
    }

    rc = subdev_find("cam-icp", 0, icp_node, (int)sizeof(icp_node),
                     name, (int)sizeof(name));
    if (rc < 0) {
        appendf(buf, cap, &off, "  icp: subdev not found (errno %d)\n", -rc);
    } else {
        rc = acquire_icp(icp_node, session, &icp_h);
        if (rc == 0) {
            appendf(buf, cap, &off, "  icp %s: acquire ok handle=0x%08x\n",
                    icp_node, (unsigned)icp_h);
            link_hdls[count++] = icp_h;
        } else {
            appendf(buf, cap, &off,
                    "  icp %s: acquire failed (errno %d)\n", icp_node, -rc);
        }
    }

    if (count == 0) {
        appendf(buf, cap, &off, "  link: skipped, no device handle acquired\n");
    } else {
        fd = open("/dev/video0", O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            appendf(buf, cap, &off, "  link: cam-req-mgr open failed (errno %d)\n", errno);
        } else {
            memset(&link, 0, sizeof(link));
            link.session_handle = session;
            link.num_devices = (uint32_t)count;
            for (int i = 0; i < count; i++) link.dev_hdls[i] = link_hdls[i];
            rc = cam_control_call(fd, VQEAF_CAM_LINK, &link,
                                  (int32_t)sizeof(link));
            if (rc < 0) rc = -errno;
            close(fd);
            if (rc == 0) {
                link_hdl = link.link_hdl;
                appendf(buf, cap, &off,
                        "  link: ok devices=%d handle=0x%08x\n",
                        count, (unsigned)link_hdl);
            } else {
                appendf(buf, cap, &off,
                        "  link: failed devices=%d (errno %d)\n", count, -rc);
            }
        }
    }

    release_dev(icp_node, session, icp_h, "icp", buf, cap, &off);
    release_dev(isp_node, session, isp_h, "isp", buf, cap, &off);
    release_dev(sensor_node, session, sensor_h, "sensor", buf, cap, &off);
    destroy_session(session, buf, cap, &off);
    appendf(buf, cap, &off,
            "cam link: no buffers allocated and no request scheduled\n");
    return off;
}

int vqeaf_camera_subdevs(char *buf, int cap)
{
    DIR *dir = opendir("/sys/class/video4linux");
    struct dirent *e;
    int off = 0, n;

    if (!buf || cap <= 0) return -EINVAL;
    if (!dir) return -ENOENT;
    while ((e = readdir(dir)) != NULL && off < cap - 1) {
        char sp[192], name[128] = "";
        int i, fd;
        if (strncmp(e->d_name, "v4l-subdev", 10) != 0) continue;
        for (i = 10; e->d_name[i]; i++)
            if (e->d_name[i] < '0' || e->d_name[i] > '9') break;
        if (i == 10 || e->d_name[i]) continue;
        snprintf(sp, sizeof(sp), "/sys/class/video4linux/%s/name", e->d_name);
        fd = open(sp, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            n = (int)read(fd, name, sizeof(name) - 1);
            close(fd);
            if (n > 0) name[n] = '\0';
            for (int k = 0; name[k]; k++)
                if (name[k] == '\n' || name[k] == '\r') name[k] = '\0';
        }
        n = snprintf(buf + off, (size_t)(cap - off), "/dev/%-14s %s\n",
                     e->d_name, name);
        if (n < 0 || n >= cap - off) break;
        off += n;
    }
    closedir(dir);
    buf[off] = '\0';
    return off;
}

int vqeaf_camera_query(const char *path, VqeafCameraNode *out)
{
    struct v4l2_capability cap;
    int fd, rc;

    if (!path || !out) return -EINVAL;
    memset(out, 0, sizeof(*out));
    snprintf(out->node, sizeof(out->node), "%s", path);
    out->role = vqeaf_v4l2_role(strrchr(path, '/') ?
                                strrchr(path, '/') + 1 : path);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    memset(&cap, 0, sizeof(cap));
    rc = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (rc < 0) { rc = -errno; close(fd); return rc; }
    memcpy(out->driver, cap.driver, sizeof(out->driver) - 1);
    memcpy(out->card, cap.card, sizeof(out->card) - 1);
    memcpy(out->bus_info, cap.bus_info, sizeof(out->bus_info) - 1);
    /* A videoN node is not automatically a camera. Violet also registers
     * msm_vidc encoder/decoder nodes; they must never be counted as capture. */
    if (vqeaf_v4l2_role(out->driver) == VQEAF_V4L2_ROLE_CODEC ||
        vqeaf_v4l2_role(out->card) == VQEAF_V4L2_ROLE_CODEC)
        out->role = VQEAF_V4L2_ROLE_CODEC;
    out->version = cap.version;
    out->capabilities = cap.capabilities;
    out->device_caps = cap.device_caps ? cap.device_caps : cap.capabilities;
    close(fd);
    return 0;
}

static VqeafV4l2Role sysfs_role(const char *path)
{
    const char *base = strrchr(path, '/');
    char sp[192], name[128] = "";
    VqeafV4l2Role r;
    int fd, n;

    if (!base) return VQEAF_V4L2_ROLE_CAPTURE;
    base++;
    snprintf(sp, sizeof(sp), "/sys/class/video4linux/%s/name", base);
    fd = open(sp, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return VQEAF_V4L2_ROLE_CAPTURE;
    n = (int)read(fd, name, sizeof(name) - 1);
    close(fd);
    if (n <= 0) return VQEAF_V4L2_ROLE_CAPTURE;
    name[n] = '\0';
    r = vqeaf_v4l2_role(name);
    return (r == VQEAF_V4L2_ROLE_CONTROL || r == VQEAF_V4L2_ROLE_ROTATOR ||
            r == VQEAF_V4L2_ROLE_CODEC) ? r : VQEAF_V4L2_ROLE_CAPTURE;
}

static uint32_t frame_hash(const unsigned char *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

int vqeaf_camera_grab(const char *path, VqeafCameraFrame *out,
                      char *err, int err_cap)
{
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    struct v4l2_format fmt;
    struct v4l2_capability cap;
    struct pollfd pfd;
    void *map = MAP_FAILED;
    size_t map_len = 0;
    unsigned char *copy = NULL;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int fd = -1, rc = 0, streaming = 0;

    if (!path || !out) return -EINVAL;
    memset(out, 0, sizeof(*out));
    if (err && err_cap > 0) err[0] = '\0';
    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return set_error(err, err_cap, "open failed", -errno);

    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        rc = set_error(err, err_cap, "QUERYCAP failed", -errno);
        goto fail;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        rc = set_error(err, err_cap, "node cannot stream", -ENOTSUP);
        goto fail;
    }
    {
        VqeafCameraNode info;
        if (vqeaf_camera_query(path, &info) != 0 ||
            info.role != VQEAF_V4L2_ROLE_CAPTURE ||
            sysfs_role(path) != VQEAF_V4L2_ROLE_CAPTURE ||
            !(info.device_caps & (V4L2_CAP_VIDEO_CAPTURE |
                                  V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
            rc = set_error(err, err_cap,
                           "node is not a camera capture stream", -ENOTSUP);
            goto fail;
        }
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = type;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        rc = set_error(err, err_cap, "G_FMT failed", -errno);
        goto fail;
    }
    out->width = fmt.fmt.pix.width;
    out->height = fmt.fmt.pix.height;

    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 1) {
        rc = set_error(err, err_cap, "REQBUFS failed", -errno);
        goto fail;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type = type;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
        rc = set_error(err, err_cap, "QUERYBUF failed", -errno);
        goto fail;
    }
    map_len = buf.length;
    map = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED,
               fd, (off_t)buf.m.offset);
    if (map == MAP_FAILED) {
        rc = set_error(err, err_cap, "mmap failed", -errno);
        goto fail;
    }

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        rc = set_error(err, err_cap, "QBUF failed", -errno);
        goto fail;
    }
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        rc = set_error(err, err_cap, "STREAMON failed", -errno);
        goto fail;
    }
    streaming = 1;

    pfd.fd = fd;
    pfd.events = POLLIN;
    rc = poll(&pfd, 1, 2000);
    if (rc == 0) { rc = set_error(err, err_cap, "sample timed out", -ETIMEDOUT); goto fail; }
    if (rc < 0) { rc = set_error(err, err_cap, "poll failed", -errno); goto fail; }

    memset(&buf, 0, sizeof(buf));
    buf.type = type;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        rc = set_error(err, err_cap, "DQBUF failed", -errno);
        goto fail;
    }
    out->bytes_used = buf.bytesused;
    copy = (unsigned char *)malloc(buf.bytesused ? buf.bytesused : 1);
    if (!copy) { rc = set_error(err, err_cap, "allocation failed", -ENOMEM); goto fail; }
    memcpy(copy, map, buf.bytesused);
    out->data = copy;
    out->hash = frame_hash(copy, buf.bytesused);

    ioctl(fd, VIDIOC_STREAMOFF, &type);
    munmap(map, map_len);
    close(fd);
    return 0;

fail:
    if (streaming) ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (map != MAP_FAILED) munmap(map, map_len);
    if (fd >= 0) close(fd);
    return rc < 0 ? rc : -EIO;
}

int vqeaf_camera_probe(char *buf, int cap)
{
    VqeafCameraNode nodes[24];
    DIR *dir;
    struct dirent *e;
    int count = 0;

    if (!buf || cap <= 0) return -EINVAL;
    dir = opendir("/sys/class/video4linux");
    if (!dir) return -ENOENT;
    while ((e = readdir(dir)) != NULL && count < (int)(sizeof(nodes) / sizeof(nodes[0]))) {
        char path[192], name[128] = "";
        int n, fd;
        if (strncmp(e->d_name, "video", 5) != 0) continue;
        for (n = 5; e->d_name[n]; n++)
            if (e->d_name[n] < '0' || e->d_name[n] > '9') break;
        if (n == 5 || e->d_name[n]) continue;
        snprintf(path, sizeof(path), "/sys/class/video4linux/%s/name", e->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            n = (int)read(fd, name, sizeof(name) - 1);
            close(fd);
            if (n > 0) name[n] = '\0';
            for (int i = 0; name[i]; i++)
                if (name[i] == '\n' || name[i] == '\r') name[i] = '\0';
        }
        snprintf(path, sizeof(path), "/dev/%s", e->d_name);
        vqeaf_camera_query(path, &nodes[count]);
        /* The sysfs `name` is authoritative for control/rotator links. */
        {
            VqeafV4l2Role nr = vqeaf_v4l2_role(name);
            if (nr == VQEAF_V4L2_ROLE_CONTROL || nr == VQEAF_V4L2_ROLE_ROTATOR ||
                nr == VQEAF_V4L2_ROLE_CODEC) nodes[count].role = nr;
        }
        if (!nodes[count].driver[0]) snprintf(nodes[count].driver,
                                              sizeof(nodes[count].driver), "%.31s", name);
        count++;
    }
    closedir(dir);
    return vqeaf_camera_report(nodes, count, buf, cap);
}

#else

int vqeaf_camera_query(const char *path, VqeafCameraNode *out)
{
    (void)path;
    if (out) memset(out, 0, sizeof(*out));
    return -1;
}

int vqeaf_camera_grab(const char *path, VqeafCameraFrame *out,
                      char *err, int err_cap)
{
    (void)path;
    if (out) memset(out, 0, sizeof(*out));
    if (err && err_cap > 0) snprintf(err, (size_t)err_cap, "host has no camera");
    return -1;
}

int vqeaf_camera_probe(char *buf, int cap)
{
    if (!buf || cap <= 0) return -1;
    snprintf(buf, (size_t)cap, "cam: no device probe on this host\n");
    return (int)strlen(buf);
}

int vqeaf_camera_create_session(int32_t *out, char *err, int err_cap)
{
    if (out) *out = 0;
    if (err && err_cap > 0)
        snprintf(err, (size_t)err_cap, "host has no cam-req-mgr");
    return -1;
}

int vqeaf_camera_subdevs(char *buf, int cap)
{
    if (!buf || cap <= 0) return -1;
    snprintf(buf, (size_t)cap, "cam: no v4l-subdev on this host\n");
    return (int)strlen(buf);
}

int vqeaf_camera_query_caps(char *buf, int cap)
{
    if (!buf || cap <= 0) return -1;
    snprintf(buf, (size_t)cap, "cam caps: no v4l-subdev on this host\n");
    return -1;
}

int vqeaf_camera_acquire_link(char *buf, int cap)
{
    if (!buf || cap <= 0) return -1;
    snprintf(buf, (size_t)cap, "cam link: no cam-req-mgr on this host\n");
    return -1;
}

#endif
