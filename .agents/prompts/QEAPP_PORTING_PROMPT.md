# Prompt: Port code sang `.qeapp` (native QEAPP) để chạy trên QEAFIVELS OS

## Cách dùng

Copy toàn bộ file này cho agent, sau khi điền khối **TARGET** bên dưới.
Không xoá phần "Sự thật kỹ thuật" — đó là ràng buộc thật của repo này.

```text
TARGET
------
SOURCE       : <đường dẫn mã nguồn gốc, ví dụ D:\Program\android\gameapk\furry_guardian>
SOURCE_KIND  : <web/JS/Three.js | C/C++ desktop | Python | Android APK | khác>
LICENSE      : <giấy phép + bằng chứng (file LICENSE, header, README) hoặc "chưa rõ">
SCOPE        : <phần nào của source được port; phần nào cố ý bỏ>
PROJECT_DIR  : <thư mục project QEAPP mới, ví dụ examples/<ten>-qeapp>
ARCH         : <arm64 | x86_64 | cả hai>
DEVICE_TEST  : <yes/no - có được phép cài lên Redmi Note 7 Pro bằng QEAFRESCUE không>
```

---

## Vai trò

Bạn là kỹ sư porting của QEAFIVELS OS, làm việc trong `D:\QEAFIVELS-Os`.

Nhiệm vụ: chuyển phần mã nguồn trong `TARGET.SOURCE` thành một ứng dụng
`.qeapp` **native** (runtime `native`, ABI v5) đóng gói thật, chạy được trên
QEAFIVELS OS — trước hết trên host simulator (`qeaf simulate`), sau đó (nếu
`DEVICE_TEST: yes`) trên máy violet thật.

Đây là **port lại logic và giao diện**, không phải chạy lại nền tảng cũ:
QEAFIVELS OS không có browser, JavaScript engine, WebView, ART/JNI, APK, GL/WebGL,
libc desktop, không có syscall cho app. Mọi thứ app cần phải đi qua bảng API
`qeaf_app_api_v5`.

---

## Đọc trước khi viết dòng code nào

Bắt buộc, theo thứ tự:

1. `.agents/AGENTS.md` — luật kỹ thuật của repo (bằng chứng thật, không fake).
2. `docs/QEAPP_FORMAT.md` — container, manifest, chữ ký, giới hạn, cách cài.
3. `docs/QEAPP_NATIVE_ABI_V2.md` — header `.qen`, import/export, ABI v2→v5,
   load-base alignment, process model, quota, simulator, runtime có guard.
4. `framework/application_api/qeaf_api.h` + `framework/application_api/qeaf_native_abi.h`
   — hợp đồng ABI thật (đừng suy ra từ tài liệu).
5. `sdk/templates/hello-qeapp-native/` — scaffold native đang dùng
   (`src/main.c`, `manifest.json`, `packaging/package.json`).
6. `examples/furryguardian-qeapp/` — **port mẫu đã xong** (game Three.js →
   isometric software renderer). Đọc `src/main.c` phần header comment và
   `src/font8x8.h` để thấy cách xử lý canvas, input-driven stepping, text ASCII.
7. `sdk/compiler/qecompile.py` và `sdk/compiler/qenlink.py` — compiler/linker
   thật: cờ biên dịch, relocation được phép, lý do module phải PIC.

---

## Sự thật kỹ thuật (không được đoán lại, không được bỏ qua)

### Container và manifest

- `.qeapp` = ZIP **STORED**, không nén; tối đa **32 entry** và **512 KiB**.
- Manifest native: `format: "qeapp"`, `format_version: 2`, `runtime: "native"`,
  `abi_version: 5`, `entry: "native/<arch>/app.qen"`, `architecture: ["arm64"]`,
  `permissions: [...]`, `icon: "assets/icon.rgba"`, `min_os` thực tế của máy.
- `format_version: 1` (declarative `app`/`print`) vẫn phải chạy — không được
  làm hỏng nó.
- Icon runtime là **64×64 BGRA = 16 384 byte**; `scripts/png_to_qeapp_icon.py`
  đổi PNG sang đúng định dạng đó.
- Ký bằng HMAC-SHA256 với dev key, build tự ký; đây là phát hiện sửa byte,
  không phải trust root production.

### Module `.qen`

- Header 64 byte little-endian, magic `QEAF-QEN1`, module version 1, ABI byte
  **5**, arch id 1 = arm64 / 2 = x86_64, hash FNV-1a 32, reserved phải = 0.
- Export bắt buộc: `create`, `start`, `stop`, `destroy`; tuỳ chọn: `pause`,
  `resume`, `on_input`.
- Import đúng **một** tên: `qeaf_app_api_v5` (không khai cả v2/v3/v4).
- Payload `code | rodata | data | imports | exports`; module ≤ 8 MiB; linker
  `qenlink.py` chỉ nhận relocation PC-relative — **cấm** absolute/GOT/undefined
  symbol. Module không có runtime relocation.
- arm64 dùng `ADRP`+`ADD` nên **base phải căn 4096 byte** (harness QEMU và
  runtime thiết bị đã làm; nếu tự viết loader thì phải giữ).

### Bảng API v5 (thứ app được gọi)

`size`, `abi_version`, `log_utf8`, `draw_text`, `request_permission`,
`read_file`, `free`, `blit_pixels`, `read_file_part`, `draw_text_weighted`,
`layout_split_weighted`.

- Không có: timer/frame callback, thread, audio, network, write_file, syscall,
  libc, GPU, đọc cảm biến, mở socket.
- `draw_text` chỉ có face 8×8 **ASCII** → nhãn tiếng Việt viết **không dấu**.
- `blit_pixels` nhận RGB565, có clip; `read_file_part` để stream file lớn
  (dùng cho media) thay vì nạp cả file vào quota.

### Giới hạn runtime (thiết kế app phải chịu được)

- Canvas điện thoại **720×1560 XRGB8888** (dọc); ngang 1560×720.
- Arena module tối đa **2 MiB** (`code+rodata+data+bss`), quota bộ nhớ động
  **1 MiB**/app, dữ liệu riêng của app ≤ 64 KiB, registry ≤ 24 app.
- **1000 ms** cho mỗi lifecycle call, **5000 ms** cho cả lượt chạy: call quá
  lâu bị coi là fail, treo bị kill và shell phải sống.
- App chạy trong **child process** tách address space; crash/hang không được
  làm chết shell. API table là biên capability, không phải sandbox kernel.
- Không có frame callback ⇒ tiền lệ đúng là **mỗi `on_input` tiến một bước
  mô phỏng cố định rồi vẽ lại** (xem `examples/furryguardian-qeapp`). Nếu app
  cần animation liên tục, nói rõ giới hạn này thay vì giả vờ có 60 FPS.

### Quyền

Chỉ khai đúng cái dùng: `ui.display`, `storage.read`, `storage.write.app`,
`input.receive`, `media.read`, `network.client` (chỉ khi có backend mạng thật).
Permission trong manifest là **trần**: broker không cấp quá trần, và mọi lần
từ chối đều bị audit. Không xin quyền "cho chắc".

### Build, kiểm chứng, cài đặt

Từ gốc repo:

```text
python sdk/cli/qeaf.py create <PROJECT_DIR> --native --arch arm64 --id org.example.<app> --package-name <app>
python sdk/cli/qeaf.py build <PROJECT_DIR> --config all
python sdk/cli/qeaf.py validate <PROJECT_DIR>/build/releases/<app>.qeapp
python sdk/cli/qeaf.py simulate <PROJECT_DIR>/build/releases/<app>.qeapp --device violet --panel-fb build/<app>-panel.raw
```

- Compiler native cần toolchain Zig (`ZIG=...` hoặc toolchain của QeafdropOs);
  `--arch arm64` build cho máy, `--arch x86_64` để **chạy** trong simulator.
- Simulator chỉ **execute** module x86_64; arm64 chỉ build/validate trên host,
  chạy thật qua QEMU harness hoặc máy.
- Test hồi quy liên quan: `python scripts/test_qeapp_simulator.py`,
  `scripts/test_qeapp_aarch64.py`, `scripts/test_qeapp_runtime.py`,
  `scripts/test_qeapp_runtime_guard.py`, `scripts/test_qeapp_native_loader.py`.
- Cài trên máy: copy `.qeapp` vào volume `QEAFRESCUE` (ví dụ `/Install/Apps`),
  mở **Trình cài đặt** → **Cài**, hoặc rescue verb `install <path>` / `apps`;
  chạy chẩn đoán bằng `qeapp <file>`. Ghi lại `status` thật, không suy diễn.

---

## Bất biến khi viết code trong module

1. **Freestanding PIC**: không `#include` libc, không syscall, không inline asm
   đụng phần cứng, không con trỏ tuyệt đối, không bảng GOT.
2. `-mstrict-align` khiến compiler sinh `memcpy`/`memset`: tự định nghĩa hai
   hàm đó trong module (xem đầu `examples/furryguardian-qeapp/src/main.c`).
3. State đặt trong `.bss`/`.data` tĩnh; `create()` trả handle trỏ vào state đó;
   `destroy()` giải phóng mọi buffer đã cấp qua `read_file` bằng `free`.
4. Mọi truy cập OS đi qua `api->...`; không có đường vòng.
5. Mọi buffer có biên rõ ràng; kiểm tra `api`, `abi_version`, con trỏ, kích
   thước trước khi dùng; không chia cho 0, không tràn index.
6. Một bước mô phỏng = hằng số thời gian (ví dụ 1/30 s) để kết quả tất định,
   không phụ thuộc đồng hồ thật.
7. Text: ASCII, không dấu; số liệu in bằng buffer cục bộ, không `printf`.
8. Giữ chi phí mỗi call dưới 1000 ms và tổng dưới 5000 ms; vòng lặp vẽ phải
   có biên theo diện tích canvas, không theo thời gian.

---

## Quy trình bắt buộc (6 bước, mỗi bước có gate)

### B1 — Kiểm kê nguồn và giấy phép

- Liệt kê file/biến thể, số dòng, ngôn ngữ, thư viện, asset (model, ảnh, âm
  thanh, font), entry point, cách game/app khởi tạo và vòng lặp chính.
- Xác định giấy phép và ghi lại bằng chứng. Asset không có quyền phân phối thì
  **không** đóng gói; thay bằng asset tự vẽ và nói rõ.
- Gate: có danh sách tính năng + giấy phép, không còn "chưa rõ nguồn".

### B2 — Bảng ánh xạ capability (bắt buộc đưa vào báo cáo)

Lập bảng: `tính năng nguồn | cơ chế nguồn | ánh xạ sang ABI | quyết định`.
Quyết định chỉ được là một trong: **giữ**, **thay bằng** (mô tả cách), **bỏ**
(nêu lý do kỹ thuật). Ví dụ đã dùng ở port mẫu:

| Nguồn | Ánh xạ |
| --- | --- |
| Three.js/WebGL 3D | renderer software isometric vào framebuffer RGB565, upscale `blit_pixels` |
| requestAnimationFrame | mỗi `on_input` = một bước 1/30 s rồi vẽ lại |
| localStorage/save | `read_file` (chỉ đọc) — không có write ⇒ bỏ save, nêu rõ |
| WebAudio | không có audio ⇒ bỏ, nêu rõ |
| Font web | face 8×8 ASCII của OS, nhãn không dấu |

Gate: mọi tính năng nguồn đều có dòng trong bảng; không có tính năng "biến mất
im lặng".

### B3 — Scaffold, manifest, icon

- `qeaf create --native` (không sửa template dùng chung), đặt `id`, `name`,
  `version` theo source, `min_os` đúng bản đang có.
- `permissions` = đúng cái B2 cần.
- Icon: PNG 64×64 → `scripts/png_to_qeapp_icon.py` → `assets/icon.rgba`
  (16 384 byte, BGRA).
- Gate: `qeaf validate` trên package build ra phải PASS.

### B4 — Cài đặt

- Chia `src/main.c` thành: hỗ trợ freestanding (`memcpy`/`memset`), palette và
  tham số, state, mô phỏng (bảng dữ liệu thay cho JSON/JS), renderer (hình học
  phần mềm + text), input, lifecycle.
- Chép bảng số liệu (hp, speed, damage, cost, cooldown…) **nguyên bản** từ
  source, không "cân bằng lại" theo cảm tính; sai lệch nào cũng ghi vào
  "Deliberate differences".
- Giữ bố cục màn hình đọc được trên 720×1560: vùng HUD / vùng scene / vùng
  điều khiển; màu RGB565 có tương phản; text không đè lên nhau.

### B5 — Bằng chứng trên host

```text
python sdk/cli/qeaf.py build <PROJECT_DIR> --config all
python sdk/cli/qeaf.py validate <PROJECT_DIR>/build/releases/<app>.qeapp
python sdk/cli/qeaf.py simulate <PROJECT_DIR>/build/releases/<app>.qeapp --device violet --panel-fb build/<app>-panel.raw
```

Ghi lại: exit code, kích thước package, hash framebuffer, lifecycle trace
(create→start→pause→resume→stop→destroy), số allocation còn treo (phải = 0).
Build x86_64 để chạy simulator; build arm64 để đóng gói cho máy.
Gate: simulator chạy sạch, không leak; nếu lỗi thì sửa nguyên nhân, không
nới assertion.

### B6 — Bằng chứng trên thiết bị (chỉ khi `DEVICE_TEST: yes`)

- Copy `.qeapp` vào `QEAFRESCUE` → `install` → `apps` → chạy từ **Ứng dụng →
  Đã cài**; hoặc verb `qeapp <file>` cho chẩn đoán.
- Ghi lại output thật: hash framebuffer phải trùng host/QEMU cho cùng app,
  `outstanding = 0`, không crash; thử một lần chạm/phím để xác nhận
  `on_input` tiến bước.
- Nếu không có thiết bị/không được phép: **nói thẳng là chưa kiểm chứng trên
  máy**, chỉ nêu bằng chứng host/QEMU.

---

## Definition of done

- [ ] Package `.qeapp` native ABI v5 build và validate PASS, nằm trong
      `<PROJECT_DIR>/build/releases/`, có icon đúng định dạng.
- [ ] `qeaf simulate --device violet` chạy hết lifecycle, hash tất định,
      0 allocation treo.
- [ ] Bảng ánh xạ capability đầy đủ; mọi tính năng bị bỏ đều có lý do.
- [ ] Không có libc/syscall/absolute reloc; module build được cho arm64.
- [ ] `docs/STATUS.md` (hoặc tài liệu của project) ghi: nguồn + phiên bản +
      license + sha256 của `.qeapp`, lệnh build, kết quả thật, phần chưa
      kiểm chứng.
- [ ] Không commit, không push, không flash gì ngoài phạm vi được cho phép.

## Không được làm

- Không chạy/chứa APK, ELF hệ điều hành, WebView, JS engine, JNI, Java/Kotlin,
  libc, hoặc gọi syscall trực tiếp trong module.
- Không mock, không giả lập kết quả, không in "PASS" khi lệnh chưa chạy, không
  tạo screenshot giả, không tuyên bố "đã chạy trên máy" khi chưa có log thật.
- Không nới test, không bỏ test, không hạ assertion để build xanh.
- Không thêm quyền "cho chắc", không thêm network/audio như thể ABI có.
- Không đóng gói asset/model/âm thanh không rõ giấy phép.
- Không sửa template dùng chung (`sdk/templates/`) hay ABI để app dễ port hơn;
  app phải theo ABI hiện có, hoặc đề xuất ABI mới thành việc riêng.
- Không đụng p76/boot/userdata/misc/GPT/EDL/erase/format.

## Báo cáo cuối (đúng thứ tự này)

1. **Phạm vi**: source gì, port phần nào, vì sao.
2. **Bảng ánh xạ capability** (giữ / thay / bỏ + lý do).
3. **File tạo/sửa** (đường dẫn đầy đủ).
4. **Lệnh đã chạy + kết quả thật** (exit code, kích thước, hash, trace, leak).
5. **Đã kiểm chứng trên máy hay chỉ host/QEMU** — tách bạch rõ.
6. **Khác biệt có chủ ý** so với source (3D→software, save/audio bỏ, text
   không dấu…).
7. **Giới hạn còn lại + blocker** (ví dụ: không có frame callback, không có
   write_file, chưa test trên máy).
8. **Bước nhỏ tiếp theo** có thể kiểm chứng được.
