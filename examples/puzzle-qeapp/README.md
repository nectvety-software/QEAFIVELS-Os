# Xếp Hình 4x4 — QEAPP native mẫu

Game xếp hình 15 ô viết bằng C freestanding, chỉ dùng bảng API của framework
(QEAPP native ABI v4). Đây là mẫu nhỏ nhất còn "chơi được": bàn 4×4, một ô
trống, trượt ô cạnh ô trống, đếm số bước, thắng thì xáo lại.

## Chơi

- Phím mũi tên hoặc `W A S D`: trượt ô **theo hướng phím** vào ô trống
  (nhấn ← thì ô nằm bên phải ô trống trượt sang trái).
- Chạm vào một ô cạnh ô trống cũng trượt được (sự kiện `QEAF_INPUT_TOUCH`).
- Sắp `1..15` từ trái sang phải, trên xuống dưới, ô trống ở cuối.
- Thắng xong bấm phím/chạm bất kỳ để xáo bàn mới.

## Build và chạy

```bash
# trong D:/QEAFIVELS-Os
python sdk/cli/qeaf.py build examples/puzzle-qeapp --config release
python sdk/cli/qeaf.py validate examples/puzzle-qeapp/build/releases/puzzle-qeapp.qeapp

# Máy ảo VQEAF (simulator host, profile Redmi Note 7 Pro / violet)
build/qeapp-simulator/qeaf-sim.exe \
    examples/puzzle-qeapp/build/releases/puzzle-qeapp.qeapp \
    --device violet --keys 105,103,106,108 \
    --fb build/screens/puzzle.raw
```

`--keys` là tuỳ chọn của `qeaf-sim`: nó gửi các sự kiện `QEAF_INPUT_KEY`
(pressed=1) lần lượt sau `start()`, để host điều khiển app không cần GUI.
Key code dùng mã Linux evdev: `103` lên, `108` xuống, `105` trái, `106` phải,
`119/115/97/100` là W/S/A/D.

Gate tự động: `python scripts/test_puzzle_qeapp.py`.

## Cấu trúc

```text
manifest.json            QEAPP v2 native, abi_version 4, entry native/x86_64/app.qen
packaging/package.json   tên gói: puzzle-qeapp.qeapp
src/main.c               toàn bộ game (không libc, không syscall, không '/')
assets/icon.rgba         icon 64x64 BGRA bắt buộc của runtime
compiler/qecompile.py    launcher tới compiler trong SDK (VQEAF_SDK)
build.py                 `python build.py --config release`
```

## API dùng

| Việc | Hàm | Ghi chú |
|---|---|---|
| Nền, ô, khung | `blit_pixels` | tô màu RGB565 theo dải 720×8 |
| Chữ | `draw_text_weighted` | 8×8 ASCII, weight regular/bold |
| Log | `log_utf8` | marker `Xep Hinh: ...` cho simulator/gate |

Số trên ô được vẽ bằng 7 đoạn (`fill_rect`), không dùng font số lớn — font
runtime chỉ có ô 8×8 nên ở canvas 720×1560 sẽ rất nhỏ.

## Ranh giới trung thực

- Không lưu điểm/tiến trình: QEAPP hiện chưa ghi được storage.
- Ảnh canvas 720×1560 là của profile `violet`; app không tự hỏi kích thước
  canvas vì ABI chưa có hàm đó, nên trên canvas khác sẽ bị cắt.
- Gói mẫu build cho `x86_64` để **Máy ảo VQEAF** chạy được trên PC. Muốn chạy
  trên thiết bị/violet (arm64), đổi `architecture` thành `["arm64"]` và `entry`
  thành `native/arm64/app.qen` rồi build lại; mã nguồn không đổi.
- Bàn được xáo bằng 80 nước đi hợp lệ từ trạng thái đã giải nên luôn giải được
  (không sinh hoán vị lẻ).

## Ghi chú Windows: tên thư mục

`qeaf-sim.exe` nhận đường dẫn qua `argv` kiểu ANSI, nên **đường dẫn có ký tự
ngoài ASCII sẽ không mở được gói** (dự án vẫn build OK). Vì vậy bản cài trong
`Documents\VQEAF Projects` dùng thư mục `Xep Hinh 4x4` (ASCII) trong khi tên
hiển thị vẫn là "Xếp Hình 4x4". Đây là hạn chế của CLI simulator, không phải
của game; build bằng SDK thì đã in được đường dẫn Unicode.

## Xem UI trong cửa sổ Máy ảo

```bash
build/qeapp-simulator/qeaf-sim.exe \
    examples/puzzle-qeapp/build/releases/puzzle-qeapp.qeapp --device violet --ui
```

Cửa sổ Win32 hiển thị đúng framebuffer 720×1560 (co theo màn hình), nhận phím
mũi tên / WASD và chạm chuột chưa hỗ trợ; đóng cửa sổ là kết thúc phiên và vẫn
in report + kiểm tra leak như chế độ headless.
