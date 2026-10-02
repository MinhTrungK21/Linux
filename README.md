# Linux
Build Yocto for BeagleBone

Dự án học Linux nhúng trên **BeagleBone Black** (AM335x) với TI Processor SDK 09.03 (kernel `6.1.119-ti`) và Yocto **kirkstone**: từ driver LED đầu tiên, character device, driver màn hình SPI **ST7789 172×320**, app Qt hiển thị lên LCD, tới image Yocto `iris-image` boot qua TFTP/NFS.

📖 **Hướng dẫn đầy đủ cho người mới:** [docs/HUONG-DAN-BBB-TU-DRIVER-DEN-YOCTO.md](docs/HUONG-DAN-BBB-TU-DRIVER-DEN-YOCTO.md)

## Cấu trúc

| Thư mục | Nội dung | Phần trong hướng dẫn |
|---|---|---|
| [`userspace/on-led`](userspace/on-led) | Bật LED trên board qua `/sys/class/leds` | 5.1 |
| [`drivers/simple-driver`](drivers/simple-driver) | Module ngoài cây kernel: `hello_kernel.c` (sysfs), `led28.c` (GPIO kiểu cũ), `hello_kernel_driver.c` (character device + ioctl) | 5.2, 5.3, 6 |
| [`userspace/led-ctrl`](userspace/led-ctrl) | Chương trình gọi ioctl vào `/dev/hello_device` | 6.4 |
| [`kernel/hello-driver`](kernel/hello-driver) | Platform driver `hello-led` build cứng vào kernel | 5.4 |
| [`kernel/patches`](kernel/patches) | Patch cho cây kernel TI: DTS + `fb_st7789v.c` (LCD), `drivers/Makefile` (hello-driver) | 5.4, 7 |
| [`kernel/st7789.cfg`](kernel/st7789.cfg) | Config fragment bật fbtft/ST7789 | 7.2 |
| [`qt/ok-dashboard`](qt/ok-dashboard) | App Qt dashboard 172×320 (giờ, IP, CPU, RAM, nhiệt độ, uptime) + service systemd | 8 |
| [`meta-iris`](meta-iris) | Layer Yocto: bbappend kernel, bbappend base-files ("Iris"), recipe `ok-dashboard`, image `iris-image` | 9 |
| [`boot`](boot) | Script minicom cho U-Boot: TFTP kernel + NFS rootfs | 2, 9.8 |

## Dùng nhanh

**Áp patch vào cây kernel của TI SDK:**
```bash
cd ~/ti-sdk-kernel/board-support/ti-linux-kernel-6.1.119+gitAUTOINC+c490f4c0fe-ti
git apply /đường/dẫn/Linux/kernel/patches/0001-bbb-st7789-lcd.patch
```

**Thêm layer vào Yocto (TI SDK, kirkstone) và build:**
```bash
cp -r meta-iris ~/tisdk/sources/
cd ~/tisdk/build && source conf/setenv && export MACHINE=am335x-evm
bitbake-layers add-layer ../sources/meta-iris
bitbake iris-image
```

## Phần cứng

| Thiết bị | Chân BBB |
|---|---|
| LCD ST7789 (SPI0) | SCL P9-22 · SDA P9-18 · CS P9-17 · DC P9-12 · RST P9-23 · BL P9-15 |
| LED cho hello driver | P8-12 (gpio1_12) |
| Console | UART0 (J1), 115200 8N1 |
| Mạng | PC `192.168.9.9` ↔ BBB `192.168.9.8` |

> Các file trong `meta-iris/recipes-qt/ok-dashboard/files/` là bản sao của `qt/ok-dashboard/`. Sửa app thì cập nhật cả hai chỗ.
