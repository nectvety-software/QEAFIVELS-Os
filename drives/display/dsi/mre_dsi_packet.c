#include "mre_dsi_packet.h"

mre_status_t mre_panel_mode_validate(const mre_panel_mode_t *m)
{
    if (m == NULL) return MRE_E_INVALID;
    if (m->width == 0u || m->height == 0u) return MRE_E_INVALID;
    if (m->lanes == 0u || m->lanes > 4u) return MRE_E_INVALID;
    if (m->bpp != 18u && m->bpp != 24u && m->bpp != 30u) return MRE_E_INVALID;
    if (m->framerate == 0u || m->framerate > 240u) return MRE_E_INVALID;
    if (m->h_pulse_width == 0u || m->v_pulse_width == 0u) return MRE_E_INVALID;
    /* Totals must stay inside the 32-bit arithmetic used below. */
    if (m->width > 0xFFFFu || m->height > 0xFFFFu) return MRE_E_INVALID;
    if (m->h_front_porch > 0xFFFFu || m->h_back_porch > 0xFFFFu ||
        m->v_front_porch > 0xFFFFu || m->v_back_porch > 0xFFFFu)
        return MRE_E_INVALID;
    return MRE_OK;
}

mre_status_t mre_dsi_timing_compute(const mre_panel_mode_t *m,
                                    mre_dsi_timing_t *out)
{
    mre_status_t st;
    uint64_t h_total, v_total, pixels, bit_clock;
    if (out == NULL) return MRE_E_INVALID;
    st = mre_panel_mode_validate(m);
    if (st != MRE_OK) return st;

    h_total = (uint64_t)m->width + m->h_front_porch + m->h_back_porch +
              m->h_pulse_width;
    v_total = (uint64_t)m->height + m->v_front_porch + m->v_back_porch +
              m->v_pulse_width;
    if (h_total == 0u || v_total == 0u) return MRE_E_INVALID;

    pixels = h_total * v_total;
    if (pixels > 0xFFFFFFFFull) return MRE_E_INVALID;
    out->h_total = (uint32_t)h_total;
    out->v_total = (uint32_t)v_total;
    out->pixel_clock_hz = pixels * m->framerate;
    bit_clock = (out->pixel_clock_hz * m->bpp) / m->lanes;
    if (bit_clock == 0u) return MRE_E_INVALID;
    out->bit_clock_hz = bit_clock;
    out->byte_clock_hz = bit_clock / 8u;
    out->lane_bit_rate_hz = bit_clock;
    out->pclk_to_byte_div = (out->byte_clock_hz != 0u && m->bpp != 0u)
                                ? (uint32_t)(m->bpp / 2u) : 0u;
    return MRE_OK;
}

mre_status_t mre_dsi_encode_short_write(uint8_t di, uint8_t data0,
                                        uint8_t data1, uint8_t *out)
{
    if (out == NULL) return MRE_E_INVALID;
    if (di != MRE_DSI_DT_DCS_SHORT_WRITE_0 &&
        di != MRE_DSI_DT_DCS_SHORT_WRITE_1 &&
        di != MRE_DSI_DT_DCS_READ)
        return MRE_E_INVALID;
    out[0] = di;
    out[1] = data0;
    out[2] = data1;
    out[3] = 0u; /* checksum slot; the host controller generates it */
    return MRE_OK;
}

mre_status_t mre_dsi_encode_long_write(uint8_t di, const uint8_t *payload,
                                       size_t payload_length, uint8_t *out,
                                       size_t out_capacity)
{
    size_t i;
    if (payload == NULL || out == NULL || payload_length == 0u ||
        payload_length > 0xFFFFu)
        return MRE_E_INVALID;
    if (di != MRE_DSI_DT_DCS_LONG_WRITE) return MRE_E_INVALID;
    if (out_capacity < MRE_DSI_LONG_HEADER_BYTES + payload_length)
        return MRE_E_NOMEM;
    out[0] = di;
    out[1] = (uint8_t)(payload_length & 0xFFu);
    out[2] = (uint8_t)((payload_length >> 8) & 0xFFu);
    out[3] = 0u; /* checksum slot */
    for (i = 0u; i < payload_length; ++i)
        out[MRE_DSI_LONG_HEADER_BYTES + i] = payload[i];
    return MRE_OK;
}

mre_status_t mre_dsi_encode_entry(const mre_dsi_cmd_entry_t *entry,
                                  uint8_t *out, size_t out_capacity,
                                  size_t *encoded_length)
{
    if (entry == NULL || out == NULL || encoded_length == NULL)
        return MRE_E_INVALID;
    if (entry->payload_length == 0u || entry->payload_length > 8u)
        return MRE_E_INVALID;

    switch (entry->di) {
    case MRE_DSI_DT_DCS_SHORT_WRITE_0:
        /* DCS short write, no parameter: the parameter becomes 0x00. */
        if (entry->payload_length != 1u) return MRE_E_INVALID;
        if (mre_dsi_encode_short_write(entry->di, entry->payload[0], 0u,
                                       out) != MRE_OK)
            return MRE_E_INVALID;
        *encoded_length = MRE_DSI_SHORT_PACKET_BYTES;
        return MRE_OK;
    case MRE_DSI_DT_DCS_SHORT_WRITE_1:
        /* DCS short write with one parameter. */
        if (entry->payload_length != 2u) return MRE_E_INVALID;
        if (mre_dsi_encode_short_write(entry->di, entry->payload[0],
                                       entry->payload[1], out) != MRE_OK)
            return MRE_E_INVALID;
        *encoded_length = MRE_DSI_SHORT_PACKET_BYTES;
        return MRE_OK;
    case MRE_DSI_DT_DCS_LONG_WRITE:
        if (mre_dsi_encode_long_write(entry->di, entry->payload,
                                      entry->payload_length, out,
                                      out_capacity) != MRE_OK)
            return MRE_E_INVALID;
        *encoded_length = MRE_DSI_LONG_HEADER_BYTES + entry->payload_length;
        return MRE_OK;
    default:
        return MRE_E_UNSUPPORTED;
    }
}

mre_status_t mre_dsi_pack_words(const uint8_t *bytes, size_t length,
                                uint32_t *words, size_t word_capacity,
                                size_t *word_count)
{
    size_t groups, i;
    if (bytes == NULL || words == NULL || word_count == NULL) return MRE_E_INVALID;
    if (length == 0u) {
        *word_count = 0u;
        return MRE_OK;
    }
    groups = (length + 3u) / 4u;
    if (groups > word_capacity) return MRE_E_NOMEM;
    for (i = 0u; i < groups; ++i) {
        size_t base = i * 4u;
        uint32_t word = 0u;
        uint32_t byte;
        for (byte = 0u; byte < 4u; ++byte) {
            if (base + byte < length)
                word |= (uint32_t)bytes[base + byte] << (8u * byte);
        }
        words[i] = word;
    }
    *word_count = groups;
    return MRE_OK;
}

mre_status_t mre_dsi_command_table_validate(const mre_dsi_cmd_entry_t *table,
                                            size_t count)
{
    size_t i;
    if (table == NULL || count == 0u || count > 256u) return MRE_E_INVALID;
    for (i = 0u; i < count; ++i) {
        const mre_dsi_cmd_entry_t *e = &table[i];
        if (e->payload_length == 0u || e->payload_length > 8u)
            return MRE_E_INVALID;
        if (e->di == MRE_DSI_DT_DCS_SHORT_WRITE_0 && e->payload_length != 1u)
            return MRE_E_INVALID;
        if (e->di == MRE_DSI_DT_DCS_SHORT_WRITE_1 && e->payload_length != 2u)
            return MRE_E_INVALID;
        if (e->di == MRE_DSI_DT_DCS_LONG_WRITE && e->payload_length < 2u)
            return MRE_E_INVALID;
        if (e->di != MRE_DSI_DT_DCS_SHORT_WRITE_0 &&
            e->di != MRE_DSI_DT_DCS_SHORT_WRITE_1 &&
            e->di != MRE_DSI_DT_DCS_LONG_WRITE)
            return MRE_E_UNSUPPORTED;
    }
    return MRE_OK;
}
