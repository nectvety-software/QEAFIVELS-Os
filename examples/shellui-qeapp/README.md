# Layout mẫu QEAPP

Ứng dụng mẫu vẽ đúng bốn vùng của một giao diện điện thoại lên canvas 720×1560
của QEAPP:

| Vùng | y | Nội dung |
| --- | --- | --- |
| status bar | 0..55 | dải đặc, **không có chữ** — đây là chỗ shell vẽ đồng hồ/pin đè lên |
| title bar | 56..195 | tên app và dòng phụ |
| main | 196..1371 | nội dung: bốn vùng, kích thước bảng API đo tại chỗ |
| nav bar | 1372..1559 | bốn nhãn tab, dấu chọn trên tab đang mở |

Chỉ dùng hai primitive của ABI v4: `blit_pixels` (RGB565) cho các dải và
`draw_text_weighted` cho nhãn. Không libc, không syscall.

```powershell
python sdk\cli\qeaf.py build examples\shellui-qeapp --config release
python sdk\cli\qeaf.py validate examples\shellui-qeapp\build\releases\shellui-qeapp.qeapp
python -m unittest discover -s examples\shellui-qeapp\tests -v
```

## Trên máy

Chép gói lên QEAFRESCUE với tên 8.3, cài rồi mở:

```text
copy examples\shellui-qeapp\build\releases\shellui-qeapp.qeapp F:\LAYOUT.QEAPP
# rescue verb: install LAYOUT.QEAPP
```

Ứng dụng → Đã cài → chạm **Layout mẫu**. Vì dải 0..55 là một màu duy nhất,
`vqeaf_pages_frame_top_band_empty()` trả 1 và shell vẽ đồng hồ/pin đè nhẹ lên
dải đó (`ui: qeapp frame leaves the top band blank, status overlaid` trong
`kmsg`).

## Giới hạn

- Chữ trong app là ASCII 8×8 (`draw_text_weighted`), nên nội dung trên màn hình
  không có dấu tiếng Việt; tên app trong manifest thì có.
- Runtime chưa chuyển sự kiện chạm vào app, nên nav bar chỉ là hình: bốn nhãn
  và một dấu chọn, không bấm được. Chip quay lại ở góc dưới-trái do shell vẽ.
