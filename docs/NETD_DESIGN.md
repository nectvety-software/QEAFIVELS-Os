# qeaf-netd — network backend cho violet (design, trước khi code)

## Mục tiêu
Cung cấp một daemon C tĩnh (`aarch64-linux-musl`) quản lý mạng cho cổng
`platform/violet/`, tách lớp rõ ràng, không phụ thuộc Android/AOSP, không giả
trạng thái. Wi-Fi hiện tại **BLOCKED**: WLFW QMI `0x45` không được modem publish
nên `wlan0` không tồn tại. Backend phải báo đúng điều đó.

## Giả định đã kiểm tra
- PID 1 là `/init` tĩnh musl trên stock kernel `4.14.215-azure-Pixel+`.
- `/proc/mounts`, `/sys/class/net`, `/sys/bus/msm_subsys/devices` có thật.
- `/vendor/firmware_mnt` (p82, vfat ro) chứa `image/wlanmdsp.mbn`.
- QRTR name service nằm trong PID 1 (`qrtr_ns.c`); không tạo server giả.
- wpa_supplicant **không có binary** trong ramdisk hiện tại; lớp wifi phải fail
  `UNSUPPORTED` khi control socket không tồn tại, không báo connected.
- Không có RTC/NTP ổn định → mọi timeout dùng monotonic clock.

## Lớp (mỗi lớp một thư mục)
1. `core/` — epoll loop, timer, logger `net:` + `verified|failed|blocked|unknown`.
2. `link/` — rtnetlink list/up/down/addr/route + hotplug, không giả tên iface.
3. `wifi/` — state machine `ABSENT→PRESENT→DISCONNECTED→SCANNING→ASSOCIATING→CONNECTED`
   và `BLOCKED(reason)`; wpa_ctrl parse `CTRL-EVENT-*` thật; config 0600, không log PSK.
4. `bringup/` — ladder a→e ghi evidence; fail thì dừng `BLOCKED(step,reason)`.
5. `ip/` — DHCPv4 tự viết + static IP + netlink apply, timeout/backoff rõ.
6. `dns/` — resolver UDP, retry, cache TTL nhỏ, giới hạn kích thước gói.
7. `api/` — Unix socket `/run/qeaf-netd.sock`, JSON line có version; `libqeafnet`
   cho LVGL; rescue `net status`, `net diag` dùng chung code.
8. `tests/` — unit host-side + fixture gắn nhãn `FIXTURE`; production không import fixture.

## Nguyên tắc dữ liệu
- Mọi buffer ngoài (netlink, DHCP, DNS, JSON) kiểm tra biên, có fuzz test.
- Không `strcpy/sprintf`; dùng `snprintf`/`memcpy` có kiểm tra.
- Không block main loop: mọi thao tác có timeout, dùng epoll non-blocking.
- Binary mục tiêu < 400 KB sau strip; allocation tĩnh ở đường nóng.
- Log ghi vào `HOMOLOG.TXT`/`QEAFBOOT.LOG` theo format `net: <state> <evidence>`.

## Kiểm thử
- Host Python unittest + C test runner cho parser/state machine/encode-decode.
- Fixture tên `FIXTURE_*`, chỉ dùng trong tests.
- Integration `ip netns`/veth nếu có; nếu không ghi SKIP + lý do.
- Gate package: ELF `/init` hoặc `/sbin/qeaf-netd` có mặt, socket path + marker
  `net:` nằm trong binary; không làm yếu gate cũ.

## Commit plan (mỗi bước build + test trước khi commit)
1. core + build/gate | 2. link | 3. bringup ladder | 4. api/lib | 5. ip | 6. dns | 7. wifi | 8. rescue/UI.

## Trạng thái mong đợi trên máy hiện tại
`wifi: BLOCKED(WLFW_0x45_NOT_REGISTERED)`, `ifaces: lo, usb/rndis nếu có`,
`status: CONNECTED=false`, không bao giờ có IP/RSSI bịa.
