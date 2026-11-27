/* SPDX-License-Identifier: MIT */
/* mre_dsi_packet.h - MIPI DSI packet encoding, panel mode validation and
 * timing maths.
 *
 * Data-type values are the MIPI DSI-1 packet types the panel tables already
 * use (0x05/0x15/0x39 writes, 0x06 read request, 0x21/0x22/0x1C responses).
 * Nothing here touches a register: encoding and timing are pure arithmetic,
 * which is why they are the part of the DSI stack a host test can prove.
 *
 * Checksum slots stay zero on purpose: the host controller generates ECC/CRC
 * when it pushes the packet, so a driver that filled them would be sending
 * bytes the panel never asked for.
 */
#ifndef MRE_DSI_PACKET_H
#define MRE_DSI_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "mre_types.h"

#define MRE_DSI_SHORT_PACKET_BYTES 4u
#define MRE_DSI_LONG_HEADER_BYTES 4u

/* Transmit data types. */
#define MRE_DSI_DT_DCS_SHORT_WRITE_0 0x05u
#define MRE_DSI_DT_DCS_SHORT_WRITE_1 0x15u
#define MRE_DSI_DT_DCS_LONG_WRITE 0x39u
#define MRE_DSI_DT_DCS_READ 0x06u

/* Receive data types a read-back window can hold. */
#define MRE_DSI_RX_DCS_SHORT_READ_1 0x21u
#define MRE_DSI_RX_DCS_SHORT_READ_2 0x22u
#define MRE_DSI_RX_DCS_LONG_READ 0x1Cu

/* One panel mode, as the device tree states it. h_sync_skew is kept even
 * though the timing maths ignores it, because dropping it would silently
 * shift every later field of a positional descriptor initializer. */
typedef struct {
    const char *name;
    uint32_t width, height;
    uint32_t h_front_porch, h_back_porch, h_pulse_width, h_sync_skew;
    uint32_t v_front_porch, v_back_porch, v_pulse_width;
    uint32_t framerate, bpp, lanes;
} mre_panel_mode_t;

/* Derived timing. The totals are the panel's, the clocks are what the DSI and
 * PHY layers must be told to output. */
typedef struct {
    uint32_t h_active, v_active;
    uint32_t h_sync_width, h_back_porch, h_front_porch;
    uint32_t v_sync_width, v_back_porch, v_front_porch;
    uint32_t h_total, v_total;
    uint64_t pixel_clock_hz;
    uint64_t bit_clock_hz;
    uint64_t byte_clock_hz;
    uint64_t lane_bit_rate_hz;
    uint32_t pclk_to_byte_div;
} mre_dsi_timing_t;

/* One command-table entry. payload_length includes the DCS command byte, so
 * a DCS short write with one parameter is {di = 0x15, length = 2,
 * payload = {cmd, param}}. delay_ms is the vendor table's post-command wait. */
typedef struct mre_dsi_cmd_entry {
    uint8_t di;
    uint32_t payload_length;
    uint8_t payload[8];
    uint32_t delay_ms;
} mre_dsi_cmd_entry_t;

/* 0 for a usable mode, MRE_E_INVALID otherwise. Rejects zero geometry, more
 * than four lanes, a bpp the encoder cannot emit, 0/over-240 Hz and any
 * timing field that would overflow the 32-bit sums below. */
mre_status_t mre_panel_mode_validate(const mre_panel_mode_t *mode);

/* Fills *out from a validated mode. Rejects totals that leave 32 bits. */
mre_status_t mre_dsi_timing_compute(const mre_panel_mode_t *mode,
                                    mre_dsi_timing_t *out);

/* Short packet, four bytes, checksum slot zero. `data1` is ignored by the
 * hardware for a no-parameter write but is still emitted as 0. */
mre_status_t mre_dsi_encode_short_write(uint8_t di, uint8_t data0,
                                        uint8_t data1, uint8_t *out);

/* Long packet: four-byte header (di, length little endian, checksum slot) and
 * the payload. out_capacity must hold header + payload. */
mre_status_t mre_dsi_encode_long_write(uint8_t di, const uint8_t *payload,
                                       size_t payload_length, uint8_t *out,
                                       size_t out_capacity);

/* Encode one command-table entry, checking that the data type and the
 * payload length agree. */
mre_status_t mre_dsi_encode_entry(const mre_dsi_cmd_entry_t *entry,
                                  uint8_t *out, size_t out_capacity,
                                  size_t *encoded_length);

/* Little-endian byte groups into 32-bit FIFO words, zero padded to a whole
 * word. */
mre_status_t mre_dsi_pack_words(const uint8_t *bytes, size_t length,
                                uint32_t *words, size_t word_capacity,
                                size_t *word_count);

/* Structural check of a whole table before any of it reaches the panel. */
mre_status_t mre_dsi_command_table_validate(const mre_dsi_cmd_entry_t *table,
                                            size_t count);

#endif
