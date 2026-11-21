# Short Video.qeapp — feed video dọc (framework ABI v5)

QEAPP native dựng theo **ABI mới nhất (v5)**: `qeaf_app_api_v5`, module `.qen`
có ABI byte 5, `min_api` 5, import đúng `qeaf_app_api_v5`.

Màn hình là một feed dọc kiểu TikTok trên canvas 720×1560:

| Vùng | Nội dung |
| --- | --- |
| dải 0..55 | một màu duy nhất, để shell vẽ đồng hồ/pin đè lên |
| video 56..1291 | khung QVF phủ kín (cover-fit, cắt phần thừa), blit từng dòng RGB565 |
| rail phải | avatar, tim (Like), bình luận, chia sẻ kèm số đã rút gọn `128.4K` |
| sheet dưới | `@handle`, caption (tự ngắt dòng), tên sound, số like, thanh tiến độ |
| tab dưới | Home / Discover / Friends / Profile, chia bằng **v5 `layout_split_weighted`** |

## API dùng

- v2 (nền): `log_utf8`, `draw_text`, `request_permission`, `read_file`, `free`
- v3: `read_file_part` (stream từng frame QVF), `blit_pixels` (RGB565)
- v4: `draw_text_weighted` (caption đậm/nhạt)
- v5: `layout_split_weighted` cho rail/tab, đúng ngữ nghĩa `weighted_row` của shell

## Nội dung thật, không giả

App stream clip thật từ app storage:

```text
apps/org.vqeaf.shortvideo/@media/CLIP1.QVF   (CLIP2, CLIP3 tuỳ chọn)
```

QVF là container video duy nhất OS giải mã (`docs/QEAF_VIDEO_FORMAT.md`): header
`QVF1` v1, codec 1 (LZ4 block + RGB565), tối đa 240×426, ≤256 frame. App kiểm
tra magic/version/codec/flags/kích thước từng frame và **từ chối** file hỏng.
Không có clip thì màn hình ghi `NO CLIP YET` kèm đường dẫn cần chép — không bao
giờ vẽ ảnh thay cho video.

## Build và kiểm chứng

```powershell
python sdk\cli\qeaf.py build examples\shortvideo-qeapp --config release
python sdk\cli\qeaf.py validate examples\shortvideo-qeapp\build\releases\shortvideo-qeapp.qeapp
```

Bản arm64 là bản phát hành. Simulator trên PC chỉ chạy module x86_64, nên muốn
chạy thử bằng driver:

```powershell
# driver host: seed clip thật, gửi DOWN rồi L, dump framebuffer
python -c "import importlib.util as u;s=u.spec_from_file_location('q','sdk/simulator/qesim.py');m=u.module_from_spec(s);s.loader.exec_module(m);print(m.build('build/sim-shortvideo','examples/shortvideo-qeapp/tests/sim_driver.c','shortvideo-sim.exe'))"
build\sim-shortvideo\shortvideo-sim.exe <package-x86_64>.qeapp --clip build\media-fixtures\video1.qvf --fb build\sim-shortvideo\fb.raw
```

`tests/sim_driver.c` seed `CLIP1.QVF` bằng QVF thật rồi chạy đủ lifecycle
`create start pause resume stop destroy`, gửi 2 sự kiện (DOWN = clip kế, L =
like) và in số blit, hash framebuffer, allocs còn treo.

## Trên máy

```text
copy build\releases\shortvideo-qeapp.qeapp F:\SHORTVI.QEAPP
```

Rồi cài bằng **Trình cài đặt** (hoặc verb `install SHORTVI.QEAPP`) và mở từ
**Ứng dụng → Đã cài**.

## Giới hạn đã biết

- `draw_text` của ABI là bitmap 8×8 ASCII: caption/handle/sound phải ASCII,
  không có dấu tiếng Việt trong nội dung app (tên app trong manifest thì có).
- Runtime native trên Violet hiện **chặn** thực thi module
  (`native QEAPP blocked: privilege separation/seccomp sandbox not ready`), nên
  app cài được nhưng chưa chạy được trên máy; bằng chứng chạy thật là
  simulator/harness arm64.
- Chưa có vòng lặp phát: mỗi lần `start()` vẽ một frame; `on_input` đổi clip.
