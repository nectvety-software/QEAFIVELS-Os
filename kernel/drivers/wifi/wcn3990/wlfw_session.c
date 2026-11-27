/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <limits.h>
#include <string.h>
#include "vqeaf/wlfw_session.h"

static int valid_mac(const uint8_t *mac)
{
    unsigned i;
    int zero = 1, ones = 1;
    if(!mac || (mac[0] & 1u)) return 0;
    for(i=0;i<6u;i++) { if(mac[i] != 0u) zero=0; if(mac[i] != 0xffu) ones=0; }
    return !zero && !ones;
}

int vqeaf_wlfw_session_validate(const vqeaf_wlfw_session_request_t *r)
{
    size_t i;
    if(!r || !r->io || !r->io->send || !r->io->recv || !r->io->now_ms ||
       !r->server.node || !r->server.port ||
       !r->msa_address || !r->msa_size ||
       r->msa_address > UINT64_MAX - r->msa_size ||
       !r->bdf || !r->bdf_size || r->bdf_size > UINT32_MAX ||
       (r->regdb_size && !r->regdb) || r->regdb_size > UINT32_MAX ||
       !r->calibration || r->calibration_count == 0u ||
       r->calibration_count > VQEAF_WLFW_CAL_MAX ||
       !valid_mac(r->factory_mac) || !r->timeout_ms)
        return -EINVAL;
    for(i=0;i<r->calibration_count;i++) {
        size_t j;
        if(!r->calibration[i].data || !r->calibration[i].size ||
           r->calibration[i].size > UINT32_MAX ||
           r->calibration[i].file_id < 0)
            return -ENODATA;
        for(j=0;j<i;j++)
            if(r->calibration[j].file_id == r->calibration[i].file_id)
                return -EEXIST;
    }
    if(r->bdf_file_id < 0 || (r->regdb_size && r->regdb_file_id < 0))
        return -EINVAL;
    return 0;
}

static void save_result(vqeaf_wlfw_session_result_t *out,
                        const vqeaf_wlfw_client_t *client, int rc,
                        uint32_t completed)
{
    if(!out || !client) return;
    out->state = client->state;
    out->caps = client->caps;
    out->completed_calibrations = completed;
    out->last_error = rc;
    out->fw_ready = (uint8_t)(client->fw_ready != 0);
}

int vqeaf_wlfw_session_run(const vqeaf_wlfw_session_request_t *r,
                           vqeaf_wlfw_client_t *c,
                           vqeaf_wlfw_session_result_t *out)
{
    int32_t ids[VQEAF_WLFW_CAL_MAX];
    size_t i;
    uint32_t done = 0;
    int rc;
    if(!c) return -EINVAL;
    rc = vqeaf_wlfw_session_validate(r);
    if(rc) { if(out) { memset(out,0,sizeof(*out)); out->last_error=rc; }
             return rc; }
    vqeaf_wlfw_client_init(c, r->io, &r->server, NULL, NULL);
    rc = vqeaf_wlfw_connect(c, r->timeout_ms, r->retries);
    if(rc) goto failed;

    if(c->fw_ready) {
        save_result(out, c, 0, 0);
        return 0;
    }
    if(!c->msa_ready) {
        rc = vqeaf_wlfw_msa_info(c, r->msa_address, r->msa_size, r->timeout_ms);
        if(rc) goto failed;
        rc = vqeaf_wlfw_msa_ready(c, r->timeout_ms);
        if(rc) goto failed;
        rc = vqeaf_wlfw_wait_msa_ready(c, r->timeout_ms);
        if(rc) goto failed;
    }
    rc = vqeaf_wlfw_cap(c, r->timeout_ms);
    if(rc) goto failed;
    rc = vqeaf_wlfw_download(c, VQEAF_WLFW_BDF_DOWNLOAD,
                             r->bdf, r->bdf_size, r->bdf_file_id,
                             VQEAF_WLFW_BDF_TYPE_BIN, r->timeout_ms);
    if(rc) goto failed;
    if(r->regdb_size) {
        rc = vqeaf_wlfw_download(c, VQEAF_WLFW_BDF_DOWNLOAD,
                                 r->regdb, r->regdb_size, r->regdb_file_id,
                                 VQEAF_WLFW_BDF_TYPE_REGDB, r->timeout_ms);
        if(rc) goto failed;
    }
    for(i=0;i<r->calibration_count;i++) ids[i] = r->calibration[i].file_id;
    rc = vqeaf_wlfw_cal_report(c, ids, r->calibration_count, r->timeout_ms);
    if(rc) goto failed;
    for(i=0;i<r->calibration_count;i++) {
        rc = vqeaf_wlfw_download(c, VQEAF_WLFW_CAL_DOWNLOAD,
                                 r->calibration[i].data,
                                 r->calibration[i].size,
                                 r->calibration[i].file_id,
                                 VQEAF_WLFW_BDF_TYPE_NONE, r->timeout_ms);
        if(rc) goto failed;
        done++;
    }
    rc = vqeaf_wlfw_send_mac(c, r->factory_mac, r->timeout_ms);
    if(rc) goto failed;
    rc = vqeaf_wlfw_wait_fw_ready(c, r->timeout_ms);
    if(rc) goto failed;
    save_result(out, c, 0, done);
    return 0;
failed:
    save_result(out, c, rc, done);
    return rc;
}
