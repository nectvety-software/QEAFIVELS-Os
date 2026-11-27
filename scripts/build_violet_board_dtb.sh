#!/usr/bin/env bash
set -euo pipefail
REPO=/root/src/micode-violet-q-oss
OUT=/mnt/d/QEAFIVELS-Os/build/violet-device-tree-micode-q
REV=1488fa3d05e2f9be5fc62e0bb16ad85d96db2076
TARGET_KERNEL_RELEASE=4.14.215-azure-Pixel+
TARGET_CONFIG_BASE=4.14.122
test -d "$REPO/.git"
test "$(git -C "$REPO" rev-parse HEAD)" = "$REV"
QCOM="$REPO/arch/arm64/boot/dts/qcom"
for f in "$QCOM/sm6150-idp.dts" "$QCOM/sm6150-idp.dtsi" \
 "$QCOM/dsi-panel-ft8719-tianma-fhd-video.dtsi" \
 "$QCOM/dsi-panel-nt36672a-shenchao-fhd-video.dtsi"; do
  test -s "$f" || { echo "MISSING=$f" >&2; exit 23; }
done
mkdir -p "$OUT"

# Build a Violet-labelled IDP-derived base: unlike the old generic
# violet-reference.dtb, this tree contains the two real panel descriptors,
# WLED supply and TE/reset GPIO assignments from the official Violet source.
sed -e 's/model = "Qualcomm Technologies, Inc. SM6150 PM6150 IDP";/model = "Xiaomi Redmi Note 7 Pro (Violet)";/' \
    -e 's/compatible = "qcom,sm6150-idp", "qcom,sm6150", "qcom,idp";/compatible = "xiaomi,violet", "qcom,sm6150-idp", "qcom,sm6150", "qcom,idp";/' \
    -e '/qcom,board-id = <34 0>;/a\	qcom,msm-id = <355 0>;' \
    "$QCOM/sm6150-idp.dts" > "$OUT/violet-sm6150.dts"

# Minimal, path-targeted overlay: activate both panel candidates and apply
# the upstream Violet USB0/USB1 high-speed/low-power quirks. No speculative
# GPIO, regulator or DSI clock values are introduced.
cat > "$OUT/violet-sm6150-overlay.dts" <<'DTS'
/dts-v1/;
/plugin/;
/ {
    model = "Xiaomi Redmi Note 7 Pro (Violet)";
    compatible = "xiaomi,violet", "qcom,sm6150-idp", "qcom,sm6150", "qcom,idp";
    qcom,msm-id = <355 0>;
    qcom,board-id = <34 0>;

    fragment@0 {
        target-path = "/soc/qcom,dsi-display@10";
        __overlay__ { qcom,dsi-display-active; };
    };
    fragment@1 {
        target-path = "/soc/qcom,dsi-display@11";
        __overlay__ { qcom,dsi-display-active; };
    };
    fragment@2 {
        target-path = "/soc/ssusb@a600000/dwc3@a600000";
        __overlay__ {
            snps,dis_u2_susphy_quirk;
            snps,dis_enblslpm_quirk;
            maximum-speed = "high-speed";
        };
    };
    fragment@3 {
        target-path = "/soc/hsusb@a800000/dwc3@a800000";
        __overlay__ {
            snps,dis_u2_susphy_quirk;
            snps,dis_enblslpm_quirk;
        };
    };
    fragment@4 {
        target-path = "/soc/ssphy@88e6000";
        __overlay__ { status = "disabled"; };
    };
};
DTS

CPP_FLAGS=(-E -P -undef -D__DTS__ -nostdinc -x assembler-with-cpp
  -I "$REPO/include" -I "$REPO/include/uapi"
  -I "$REPO/arch/arm64/boot/dts" -I "$QCOM")
cpp "${CPP_FLAGS[@]}" "$OUT/violet-sm6150.dts" > "$OUT/violet-sm6150.pre.dts"
dtc -q -@ -I dts -O dtb -o "$OUT/violet-sm6150.dtb" "$OUT/violet-sm6150.pre.dts"
cpp "${CPP_FLAGS[@]}" "$OUT/violet-sm6150-overlay.dts" > "$OUT/violet-sm6150-overlay.pre.dts"
dtc -q -@ -I dts -O dtb -o "$OUT/violet-sm6150-overlay.dtbo" "$OUT/violet-sm6150-overlay.pre.dts"
dtc -q -I dtb -O dts "$OUT/violet-sm6150.dtb" > "$OUT/violet-sm6150.roundtrip.dts"
dtc -q -I dtb -O dts "$OUT/violet-sm6150-overlay.dtbo" > "$OUT/violet-sm6150-overlay.roundtrip.dts"

grep -q 'Xiaomi Redmi Note 7 Pro (Violet)' "$OUT/violet-sm6150.roundtrip.dts"
grep -q 'xiaomi,violet' "$OUT/violet-sm6150.roundtrip.dts"
grep -q 'qcom,msm-id = <0x163 0x00>' "$OUT/violet-sm6150.roundtrip.dts"
grep -q 'qcom,board-id = <0x22 0x00>' "$OUT/violet-sm6150.roundtrip.dts"
grep -q 'ft8719 video mode dsi tianma panel' "$OUT/violet-sm6150.roundtrip.dts"
grep -q 'nt36672a video mode dsi shenchao panel' "$OUT/violet-sm6150.roundtrip.dts"
grep -q 'qcom,platform-te-gpio' "$OUT/violet-sm6150.roundtrip.dts"
grep -q 'qcom,platform-reset-gpio' "$OUT/violet-sm6150.roundtrip.dts"
grep -q 'qcom,dsi-display-active' "$OUT/violet-sm6150-overlay.roundtrip.dts"
grep -q 'maximum-speed = "high-speed"' "$OUT/violet-sm6150-overlay.roundtrip.dts"

fdtoverlay -i "$OUT/violet-sm6150.dtb" -o "$OUT/violet-sm6150-merged.dtb" "$OUT/violet-sm6150-overlay.dtbo"
dtc -q -I dtb -O dts "$OUT/violet-sm6150-merged.dtb" > "$OUT/violet-sm6150-merged.roundtrip.dts"
grep -q 'ft8719 video mode dsi tianma panel' "$OUT/violet-sm6150-merged.roundtrip.dts"
grep -q 'nt36672a video mode dsi shenchao panel' "$OUT/violet-sm6150-merged.roundtrip.dts"
grep -q 'qcom,dsi-display-active' "$OUT/violet-sm6150-merged.roundtrip.dts"
grep -q 'maximum-speed = "high-speed"' "$OUT/violet-sm6150-merged.roundtrip.dts"

BASE_SIZE=$(wc -c < "$OUT/violet-sm6150.dtb")
OVERLAY_SIZE=$(wc -c < "$OUT/violet-sm6150-overlay.dtbo")
MERGED_SIZE=$(wc -c < "$OUT/violet-sm6150-merged.dtb")
BASE_SHA=$(sha256sum "$OUT/violet-sm6150.dtb" | awk '{print $1}')
OVERLAY_SHA=$(sha256sum "$OUT/violet-sm6150-overlay.dtbo" | awk '{print $1}')
MERGED_SHA=$(sha256sum "$OUT/violet-sm6150-merged.dtb" | awk '{print $1}')
cat > "$OUT/violet-dtb-manifest.json" <<EOF
{
  "target_device": "violet",
  "source_repository": "MiCode/Xiaomi_Kernel_OpenSource",
  "source_branch": "violet-q-oss",
  "source_commit": "$REV",
  "source_kernel_base": "4.14.117",
  "embedded_config_header": "$TARGET_CONFIG_BASE",
  "target_kernel_release": "$TARGET_KERNEL_RELEASE",
  "kernel_source_exact_match": false,
  "kernel_compatibility": "official SM6150/Violet board DTS reference; exact 4.14.215 source commit unproven",
  "production_ready": false,
  "role": "Violet-labelled IDP-derived host-tested reference DTB/DTBO; do not flash",
  "board_id": 34,
  "msm_id": 355,
  "panels": [
    {"name": "FT8719 Tianma", "width": 1080, "height": 2340, "te_gpio": 90, "reset_gpio": 91},
    {"name": "NT36672A Shenchao", "width": 1080, "height": 2340, "te_gpio": 90, "reset_gpio": 91}
  ],
  "device_panel_identified": false,
  "base_dtb": {"file": "violet-sm6150.dtb", "size": $BASE_SIZE, "sha256": "$BASE_SHA"},
  "overlay_dtbo": {"file": "violet-sm6150-overlay.dtbo", "size": $OVERLAY_SIZE, "sha256": "$OVERLAY_SHA"},
  "merged_dtb": {"file": "violet-sm6150-merged.dtb", "size": $MERGED_SIZE, "sha256": "$MERGED_SHA"},
  "both_panel_descriptors_present": true,
  "both_panel_nodes_active": true,
  "dwc3_usb0_usb1_quirks_present": true,
  "overlay_applied_to_base": true,
  "device_boot_tested": false
}
EOF
echo "SOURCE_COMMIT=$REV"
echo "SOURCE_KERNEL=4.14.117"
echo "EMBEDDED_CONFIG_HEADER=$TARGET_CONFIG_BASE"
echo "TARGET_KERNEL=$TARGET_KERNEL_RELEASE"
echo "BASE_DTB=$BASE_SIZE sha256=$BASE_SHA"
echo "OVERLAY_DTBO=$OVERLAY_SIZE sha256=$OVERLAY_SHA"
echo "MERGED_DTB=$MERGED_SIZE sha256=$MERGED_SHA"
echo "BOTH_PANELS=PASS"
echo "DWC3_USB0_USB1=PASS"
echo "OVERLAY_APPLY=PASS"
echo "PRODUCTION_READY=false (exact source/board match and physical boot unproven)"
