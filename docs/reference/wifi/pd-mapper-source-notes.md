<!-- Nguồn: https://github.com/linux-msm/pd-mapper (BSD-3-Clause, xem pd-mapper-LICENSE.txt) -->

# pd-mapper — ghi chú từ mã nguồn

Repo **không có README**; các dòng dưới được đọc từ source `pd-mapper.c`, `servreg_loc.qmi`, `json.c`.

## Dịch vụ

- QMI service `SERVREG_QMI_SERVICE = 0x40`, version `0x101`, instance `0`.
- Trả lời `GET_DOMAIN_LIST (0x21)` và `PFR (0x24)`.
- `handle_get_domain_list()` so khớp **chính xác** tên service trong request với
  `pd_map[].service`; mỗi entry trả về `domain = "soc/domain/subdomain"`,
  `instance_id` từ JSON.
- Nếu không có map nào → in `no pd maps available` và thoát mã 1.

## Định dạng file `*.jsn`

```json
{
  "sr_domain": {
    "soc": "msm",
    "domain": "modem",
    "subdomain": "wlan_pd",
    "qmi_instance_id": 180
  },
  "sr_service": [
    { "provider": "tms",    "service": "servreg" },
    { "provider": "wlan",   "service": "fw" },
    { "provider": "kernel", "service": "elf_loader" }
  ]
}
```

- Tên service kết quả = `"provider/service"`, domain = `"soc/domain/subdomain"`.
- File `.jsn.xz` cần liblzma; bản QEAF dùng stub nên các file nén bị bỏ qua.
- Mặc định đọc theo `firmware_class.path` rồi `/lib/firmware/`; bản QEAF thêm
  `/tmp/qeaf-pd` và fallback quét thư mục khi không có `/sys/class/remoteproc`.

## Bảng domain chuẩn (đối chiếu)

Xem `qcom_pd_mapper.c.txt`: `mpss_wlan_pd = msm/modem/wlan_pd`, instance **180**,
services `kernel/elf_loader` + `wlan/fw`; `mpss_root_pd_gps_pdr` bổ sung
`tms/pdr_enabled`. Thiếu các entry này làm modem treo ~40 s rồi reset.
