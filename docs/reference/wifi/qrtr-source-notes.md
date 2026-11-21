<!-- Nguồn: https://github.com/linux-msm/qrtr (BSD-3-Clause, xem qrtr-LICENSE.txt) -->

# qrtr / libqrtr — ghi chú từ mã nguồn

Repo **không có README**; các dòng dưới được đọc từ `include/libqrtr.h`, `lib/qrtr.c`, `lib/qmi.c`, `src/qrtr-ns.c`.

## Socket

- `AF_QIPCRTR = 42`, `socket(AF_QIPCRTR, SOCK_DGRAM, 0)`.
- `struct sockaddr_qrtr { sq_family, sq_node, sq_port }`.
- `QRTR_NODE_BCAST = 0xffffffff`, `QRTR_PORT_CTRL = 0xfffffffe`.

## API chính

- `qrtr_open(port)`, `qrtr_publish(fd, service, version, instance)`,
  `qrtr_add_lookup`, `qrtr_send`, `qrtr_recv`, `qrtr_poll`.
- `qmi_encode_message()/qmi_decode_message()` với `struct qmi_elem_info`
  (`QMI_REQUEST`, `QMI_RESPONSE`, `QMI_INDICATION`, TLV 0 = result).
- `qrtr-ns` (trong qrtr 1.0) lắng nghe `QRTR_PORT_CTRL`, xử lý
  `NEW_SERVER / DEL_SERVER / NEW_LOOKUP / DEL_LOOKUP / HELLO / BYE`; bản 1.2
  bỏ vì kernel mới có name service nội bộ. Kernel 4.14 của violet **không có**.

## Thứ tự Khởi tạo Wi‑Fi

`qrtr-ns` phải chạy trước modem và trước `pd-mapper`/`tqftpserv`/`rmtfs`.
Khi bind control port, kernel gửi `ENETRESET` cho mọi socket QRTR hiện có —
đây là cách client QMI học rằng phải gửi lại lookup.
