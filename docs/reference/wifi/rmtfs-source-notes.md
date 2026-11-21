<!-- Nguồn: https://github.com/linux-msm/rmtfs (BSD-3-Clause, xem rmtfs-LICENSE.txt) -->

# rmtfs — ghi chú từ mã nguồn

Repo **không có README**; các dòng dưới được đọc từ `rmtfs.c`, `storage.c`, `sharedmem.c`.

## Cờ dòng lệnh

```
-o DIR   storage root (mặc định /boot)
-P       dùng raw partition theo partlabel (mặc định /dev/disk/by-partlabel)
-r       read-only: nội dung được chép vào shadow buffer, modem ghi chỉ vào RAM
-s       bật đồng bộ với remoteproc MSS (KHÔNG nhận đối số — `-s DIR` là sai)
-S SFX   slot suffix cho thiết bị A/B
-v       verbose
```

Với violet/QEAF: `rmtfs -o /tmp/rmtfs -r` — không đụng partition EFS thật.

## Tên file EFS được hỏi

| Request path | file trong storage root | partlabel |
| --- | --- | --- |
| `/boot/modem_fs1` | `modem_fs1` | `modemst1` |
| `/boot/modem_fs2` | `modem_fs2` | `modemst2` |
| `/boot/modem_fsc` | `modem_fsc` | `fsc` |
| `/boot/modem_fsg` | `modem_fsg` | `fsg` |
| `/boot/modem_study` | `modem_study` | `study` |
| `/boot/modem_tunning` | `modem_tunning` | `tunning` |

## Shared memory

Thử theo thứ tự: `/dev/qcom_rmtfs_mem<N>` → `/dev/qcom_rmtfs_uio<N>` → `/dev/mem`.
Kernel 4.14 msm tạo UIO device tên `rmtfs` (`/sys/class/uio/uio0`), nên QEAF
tạo alias `/dev/qcom_rmtfs_uio1` và shim udev đọc `maps/map0/addr`, `maps/map0/size`.

Không có rmtfs → modem chạy một lúc rồi die vì EFS (`DOG detects stalled initialization`).
