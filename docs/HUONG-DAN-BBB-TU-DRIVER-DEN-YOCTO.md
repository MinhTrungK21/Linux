# Từ driver đầu tiên đến image Yocto trên BeagleBone Black

Hướng dẫn này đi lại con đường của dự án **Iris**: từ một driver LED nhỏ, character device, driver màn hình SPI ST7789, app Qt hiển thị lên LCD, cho tới khi toàn bộ hệ thống được build lại bằng **Yocto** và boot qua mạng.

Mỗi phần có: **ý tưởng** → **làm** → **kiểm tra** → **lỗi hay gặp**. Đọc theo thứ tự; phần sau dựa vào phần trước.

---

## Mục lục

0. [Bức tranh tổng thể](#0-bức-tranh-tổng-thể)
1. [Chuẩn bị máy PC và board](#1-chuẩn-bị-máy-pc-và-board)
2. [Boot BBB qua mạng (TFTP + NFS)](#2-boot-bbb-qua-mạng-tftp--nfs)
3. [Build kernel bằng tay](#3-build-kernel-bằng-tay)
4. [Device tree và pinmux](#4-device-tree-và-pinmux)
5. [Từ user space đến driver đầu tiên](#5-từ-user-space-đến-driver-đầu-tiên)
6. [Character device: /dev/hello_device với ioctl](#6-character-device-devhello_device-với-ioctl)
7. [Driver thật: màn hình SPI ST7789](#7-driver-thật-màn-hình-spi-st7789)
8. [App Qt trên framebuffer](#8-app-qt-trên-framebuffer)
9. [Yocto: đóng gói mọi thứ thành một image](#9-yocto-đóng-gói-mọi-thứ-thành-một-image)
10. [Bảng lỗi đã gặp](#10-bảng-lỗi-đã-gặp)
11. [Phụ lục](#11-phụ-lục)

---

## 0. Bức tranh tổng thể

```
PC Ubuntu 22.04 (192.168.9.9)                         BeagleBone Black (192.168.9.8)
┌──────────────────────────────────────┐               ┌──────────────────────────────┐
│ TI Processor SDK 09.03 (AM335x)       │   UART/USB    │ U-Boot (trên eMMC)           │
│  ├─ cây kernel 6.1.119-ti            │◄─────────────►│   │ TFTP: zImage + .dtb       │
│  ├─ toolchain Arm gcc 11.3           │   Ethernet    │   ▼                           │
│  ├─ linux-devkit (Qt SDK)            │◄─────────────►│ Linux kernel                  │
│  └─ targetNFS (rootfs có sẵn)        │               │   │ NFS: rootfs                │
│ Yocto (~/tisdk): meta-iris           │               │   ▼                           │
│ /tftpboot  ·  NFS export  ·  minicom │               │ systemd → app Qt → LCD ST7789 │
└──────────────────────────────────────┘               └──────────────────────────────┘
```

Có **hai cách build** và bạn sẽ dùng cả hai:

| | Build tay (SDK) | Yocto |
|---|---|---|
| Dùng khi | Đang thử, sửa liên tục | Đã ổn, muốn một image hoàn chỉnh tái tạo được |
| Kernel | `make` trong cây kernel của SDK | recipe `linux-ti-staging` + patch của bạn |
| App | Qt Creator, deploy qua SSH | recipe Qt, cài vào rootfs |
| Rootfs | `targetNFS` TI build sẵn | bạn tự build (`iris-image`) |
| Thời gian | giây → phút | phút (có cache) → giờ (lần đầu) |

---

## 1. Chuẩn bị máy PC và board

### 1.1. Phần cứng
- BeagleBone Black (AM335x, 512 MB RAM).
- Cáp **USB–UART 3,3V** (ví dụ CP2102) nối vào header J1 (6 chân) của BBB → console `/dev/ttyUSB0`.
- Cáp **Ethernet** nối thẳng PC ↔ BBB.
- Nguồn 5V cho BBB.
- (Phần 7) Màn hình SPI ST7789 1,47" 172×320.

> ⚠️ Mọi chân trên header P8/P9 của BBB chỉ chịu **3,3V**. Không bao giờ nối tín hiệu 5V vào.

### 1.2. Cài TI Processor SDK
SDK của TI cho AM335x (bản dùng ở đây: `ti-processor-sdk-linux-am335x-evm-09.03.05.02`) chứa sẵn:

| Thư mục | Nội dung |
|---|---|
| `board-support/ti-linux-kernel-6.1.119+…` | Source kernel của TI (là một git repo) |
| `external-toolchain-dir/arm-gnu-toolchain-11.3.rel1-…` | Compiler ARM chạy trên PC |
| `linux-devkit/` | "Sysroot" + Qt SDK để build app cho board |
| `filesystem/` → `targetNFS/` | Rootfs dựng sẵn (Arago) |
| `bin/setup.sh`, `bin/setup-*.sh` | Script cấu hình TFTP, NFS, minicom, U-Boot |

Chạy một lần:
```bash
cd ~/ti-sdk-kernel
./setup.sh
```
Script hỏi vài câu và cấu hình: TFTP server (`/tftpboot`), NFS export cho `targetNFS`, file script minicom `bin/setupBoard.minicom`.

### 1.3. Đặt toolchain vào biến môi trường
Để khỏi gõ đường dẫn dài, thêm vào `~/.bashrc`:
```bash
export SDK=~/ti-sdk-kernel
export KDIR=$SDK/board-support/ti-linux-kernel-6.1.119+gitAUTOINC+c490f4c0fe-ti
export CROSS=$SDK/external-toolchain-dir/arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
```
Rồi `source ~/.bashrc`. Từ đây hướng dẫn dùng `$KDIR` và `$CROSS`.

### 1.4. Mạng PC ↔ BBB
Card mạng nối với BBB phải có IP tĩnh `192.168.9.9/24` (ở máy này là `enp44s0`):
```bash
nmcli con add type ethernet ifname enp44s0 con-name BBB ipv4.method manual ipv4.addresses 192.168.9.9/24
nmcli con up BBB
ip -br addr show enp44s0
```

---

## 2. Boot BBB qua mạng (TFTP + NFS)

### Ý tưởng
Thay vì chép kernel vào thẻ SD mỗi lần sửa, ta để **U-Boot** trên board tải kernel và device tree từ PC qua **TFTP**, rồi kernel **mount rootfs từ thư mục trên PC** qua **NFS**. Sửa một file trên PC là board thấy ngay.

```
nhấn reset → U-Boot → (minicom gõ lệnh hộ bạn) → TFTP tải zImage + am335x-boneblack.dtb
          → kernel chạy → mount 192.168.9.9:/…/targetNFS làm "/" → login
```

### 2.1. Script minicom
`bin/setupBoard.minicom` tự bấm Space để dừng autoboot rồi gõ các lệnh U-Boot:
```
setenv ipaddr 192.168.9.8
setenv serverip 192.168.9.9
setenv rootpath '/home/iris/ti-sdk-kernel/targetNFS'
setenv bootfile zImage
setenv fdtfile am335x-boneblack.dtb
setenv bootargs 'console=ttyO0,115200n8 root=/dev/nfs nfsroot=192.168.9.9:/home/iris/ti-sdk-kernel/targetNFS,nolock,v3,tcp,rsize=4096,wsize=4096 rw ip=192.168.9.8:192.168.9.9:192.168.9.9:255.255.255.0::eth0:'
setenv bootcmd 'run findfdt; run init_console; setenv autoload no; tftp ${loadaddr} zImage; tftp ${fdtaddr} ${fdtfile}; bootz ${loadaddr} - ${fdtaddr}'
boot
```
- `bootargs` là dòng lệnh truyền cho kernel: console ở UART0, rootfs là NFS, IP tĩnh.
- `bootz addr - fdt`: chạy zImage, `-` = không có initramfs.
- Không cần `saveenv`: BBB không có thẻ SD nên U-Boot không lưu được (`Card did not respond to voltage select`), và script gửi lại toàn bộ mỗi lần.

### 2.2. Quy trình boot
```bash
sudo minicom -D /dev/ttyUSB0 -b 115200 -S ~/ti-sdk-kernel/bin/setupBoard.minicom
```
Rồi **nhấn nút reset** trên BBB. Đăng nhập `root` (không mật khẩu). Thoát minicom: `Ctrl+A` rồi `X`.

### Kiểm tra
Trên board:
```bash
cat /proc/cmdline     # phải có nfsroot=192.168.9.9:…
uname -a              # kernel đang chạy
```

### Lỗi hay gặp
| Hiện tượng | Nguyên nhân | Sửa |
|---|---|---|
| Board vào Debian 4.14 | Chạy minicom **không có `-S`** → U-Boot boot theo eMMC | Luôn dùng `-S …minicom` rồi mới reset |
| `Could not get PHY for cpsw`, TFTP toàn `T T T` | Chip Ethernet (PHY) của BBB không khởi động sau reset nóng; U-Boot chuyển sang `usb_ether` | **Rút nguồn** vài giây rồi cắm lại |
| `VFS: Unable to mount root fs via NFS` | Thư mục chưa có trong `/etc/exports` | Xem 9.10, `sudo exportfs -ra` |

---

## 3. Build kernel bằng tay

### Ý tưởng
Kernel = một chương trình lớn, cấu hình bằng file `.config` (hàng nghìn dòng `CONFIG_…=y/m/n`). Build ra:
- `arch/arm/boot/zImage` – kernel nén.
- `arch/arm/boot/dts/am335x-boneblack.dtb` – mô tả phần cứng (phần 4).
- các module `.ko` (driver build rời).

### 3.1. Cấu hình
TI cấu hình kernel cho AM335x bằng `multi_v7_defconfig` cộng hai "mảnh" (fragment). Làm giống hệt để bản build tay khớp với Yocto:
```bash
cd $KDIR
make ARCH=arm multi_v7_defconfig
ARCH=arm scripts/kconfig/merge_config.sh -m .config \
    kernel/configs/ti_multi_v7_prune.config kernel/configs/no_smp.config
make ARCH=arm olddefconfig
```
- `merge_config.sh -m`: gộp các fragment vào `.config`.
- `olddefconfig`: điền giá trị mặc định cho mọi option còn thiếu.

Muốn bật thêm option, dùng giao diện menu:
```bash
make ARCH=arm menuconfig      # tìm bằng phím "/"
```

### 3.2. Build
```bash
make ARCH=arm CROSS_COMPILE=$CROSS -j$(nproc) zImage dtbs modules
```
- `ARCH=arm`: build cho ARM 32-bit.
- `CROSS_COMPILE=$CROSS`: tiền tố compiler (`…-gcc`, `…-ld`).
- `-j$(nproc)`: chạy song song theo số nhân CPU.

Lần đầu mất vài phút; lần sau chỉ build lại phần đã sửa.

### 3.3. Đưa lên board
```bash
cp arch/arm/boot/zImage /tftpboot/
cp arch/arm/boot/dts/am335x-boneblack.dtb /tftpboot/      # có thể cần sudo
```
Reset board. Trên board, `uname -a` phải hiện thời gian build vừa rồi.

> Mẹo: trước khi ghi đè, sao lưu bản đang chạy tốt: `cp /tftpboot/zImage /tftpboot/zImage.manual`.

---

## 4. Device tree và pinmux

### Ý tưởng
Trên ARM, kernel **không tự dò** phần cứng nằm ở đâu. File **device tree** (`.dts`) mô tả: có những bộ điều khiển nào, ở địa chỉ nào, thiết bị nào gắn vào bus nào, dùng chân nào. Kernel đọc file đã biên dịch (`.dtb`) lúc boot.

```
am33xx.dtsi            ← mô tả chip AM335x (chung cho mọi board)
  └ am335x-bone-common.dtsi, am335x-boneblack-common.dtsi, …-hdmi.dtsi
      └ am335x-boneblack.dts   ← board của bạn; bạn thêm node vào cuối file này
```

### 4.1. Các khái niệm cần nhớ
| Khái niệm | Ví dụ | Ý nghĩa |
|---|---|---|
| node | `st7789@0 { … };` | một thiết bị |
| `compatible` | `"sitronix,st7789v"` | tên để kernel chọn driver: driver nào có chuỗi trùng thì được gọi `probe()` |
| `status` | `"okay"` / `"disabled"` | bật/tắt node |
| `&nhãn { … }` | `&spi0 { … }` | sửa tiếp một node đã khai báo ở file khác |
| `reg` | `<0>` | địa chỉ trên bus (với SPI = số chip select) |
| `xxx-gpios` | `<&gpio1 28 GPIO_ACTIVE_HIGH>` | chân GPIO: bộ `gpio1`, chân 28 |

### 4.2. Pinmux
Mỗi chân của AM335x có **8 chức năng** (mode 0–7). Phải chọn đúng mode thì chân mới làm việc mong muốn:
```dts
AM33XX_PADCONF(AM335X_PIN_SPI0_SCLK, PIN_INPUT_PULLUP, MUX_MODE0)   /* P9-22 = spi0_sclk */
AM33XX_PADCONF(AM335X_PIN_GPMC_BEN1, PIN_OUTPUT,       MUX_MODE7)   /* P9-12 = gpio1_28  */
```
- Tên macro (`AM335X_PIN_…`) nằm trong `include/dt-bindings/pinctrl/am33xx.h` – là tên **pad** trên chip, không phải tên chân header.
- `MUX_MODE7` luôn là **GPIO**.
- Chân dùng làm SPI clock vẫn cần `PIN_INPUT` vì khối McSPI đọc lại clock của chính nó.

Bảng tra chân header P8/P9 ↔ pad ↔ mode: xem "BeagleBone Black System Reference Manual", mục Expansion Headers.

### 4.3. Build và đọc lại một DTB
```bash
cd $KDIR
make ARCH=arm CROSS_COMPILE=$CROSS am335x-boneblack.dtb        # chỉ vài giây
scripts/dtc/dtc -I dtb -O dts arch/arm/boot/dts/am335x-boneblack.dtb | less
```
Lệnh thứ hai dịch ngược `.dtb` ra văn bản — cách chắc chắn nhất để biết node của bạn có thật sự nằm trong file board đang dùng. Số hiện dạng hex: `0xac` = 172, `0x140` = 320.

Trên board, device tree đang chạy nằm ở `/proc/device-tree/`.

---

## 5. Từ user space đến driver đầu tiên

Phần này đi lại đúng các bài đã làm trong `~/BBB` (11/2025 → 01/2026), mỗi bài tiến thêm một bậc:

| Bài | File | Học được |
|---|---|---|
| 5.1 | `~/BBB/main.c` | Điều khiển LED từ **user space** qua driver có sẵn |
| 5.2 | `~/BBB/simple-driver/hello_kernel.c` | **Module** đầu tiên, thuộc tính **sysfs**, build ngoài cây kernel |
| 5.3 | `~/BBB/simple-driver/led28.c` | GPIO kiểu **cũ** (số nguyên) và kiểu **mới** (descriptor từ device tree) |
| 5.4 | `drivers/hello-driver/hello_kernel.c` | **Platform driver** khớp `compatible`, build **cứng** vào kernel |
| 6 | `~/BBB/simple-driver/hello_kernel_driver.c` + `~/BBB/led-ctrl/main.c` | **Character device** `/dev/hello_device` với **ioctl** |

### 5.1. LED từ user space (chưa viết driver)
Kernel có sẵn driver `leds-gpio` cho 4 LED trên board. Mỗi LED hiện thành một thư mục trong sysfs; ghi vào file `brightness` là bật/tắt:
```c
// ~/BBB/main.c
FILE *fp = fopen("/sys/class/leds/beaglebone:green:usr2/brightness", "w");
fputs("1", fp);          /* "1" = sáng, "0" = tắt */
fclose(fp);
```
Ngay trên board không cần biên dịch:
```bash
echo 1 > /sys/class/leds/beaglebone:green:usr2/brightness
echo 0 > /sys/class/leds/beaglebone:green:usr2/brightness
```

**Biên dịch chéo** chương trình C cho board — dùng compiler trong SDK:
```bash
source ~/ti-sdk-kernel/linux-devkit/environment-setup
$CC $CFLAGS main.c -o on-led-arm
file on-led-arm            # phải là "ELF 32-bit LSB executable, ARM"
scp on-led-arm root@192.168.9.8:/home/root/
```
> ⚠️ Với `environment-setup` của TI, `$CC` **không** chứa cờ `-mfloat-abi=hard`; cờ đó nằm trong `$CFLAGS`. Thiếu `$CFLAGS` → lỗi `gnu/stubs-soft.h: No such file or directory`.
>
> ⚠️ Gọi `$(CC)` trong Makefile **mà chưa** `source environment-setup` thì ra file cho PC (x86-64), copy lên board sẽ báo `cannot execute binary file`. File `~/BBB/led-ctrl/led-ctrl` hiện là trường hợp này.

Điều học được: driver là thứ tạo ra những file đặc biệt trong `/sys` và `/dev`; chương trình user space chỉ đọc/ghi file. Các bài sau là tự viết phía driver.

### 5.2. Module đầu tiên: thuộc tính sysfs
**Ý tưởng:** một module tạo file `/sys/class/hello-kernel/device0/test`. Ghi `on`/`off` vào file này → driver nhận được; đọc file → driver trả trạng thái.

Ba thành phần chính trong `hello_kernel.c`:
```c
/* 1. hàm đọc: được gọi khi user chạy "cat …/test" */
static ssize_t test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "LED status is : %s\n", led_status == LED_ON ? "ON" : "OFF");
}

/* 2. hàm ghi: được gọi khi user chạy "echo on > …/test" */
static ssize_t test_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	if (sysfs_streq(buf, "on"))
		led_status = LED_ON;
	else if (sysfs_streq(buf, "off"))
		led_status = LED_OFF;
	else
		return -EINVAL;
	return count;			/* báo đã xử lý hết dữ liệu */
}

/* 3. gắn hai hàm thành thuộc tính tên "test" → biến dev_attr_test */
static DEVICE_ATTR_RW(test);
```
Khi nạp module: tạo **class** (thư mục trong `/sys/class`), tạo **device** trong class đó, rồi gắn file thuộc tính:
```c
hello_kernel_class = class_create(THIS_MODULE, "hello-kernel");      /* /sys/class/hello-kernel/ */
test_device = device_create(hello_kernel_class, NULL, MKDEV(0, 0), NULL, "device0");
device_create_file(test_device, &dev_attr_test);                     /* …/device0/test */
```
`MKDEV(0, 0)` = không có số thiết bị → không tạo file trong `/dev`, chỉ có sysfs.

**Makefile build ngoài cây kernel** (`~/BBB/simple-driver/Makefile`):
```make
obj-m += hello_kernel.o
KDIR = /home/iris/ti-sdk-kernel/board-support/ti-linux-kernel-6.1.119+gitAUTOINC+c490f4c0fe-ti
CROSS = /home/iris/ti-sdk-kernel/external-toolchain-dir/arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
LOCALVERSION ?= -ti-gc490f4c0fe51

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS) modules LOCALVERSION=$(LOCALVERSION)
install:
	scp *.ko root@192.168.9.8:/home/root
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS) clean
```
| Dòng | Ý nghĩa |
|---|---|
| `obj-m` | build thành **module** `.ko` (còn `obj-y` = build cứng vào kernel) |
| `-C $(KDIR) M=$(CURDIR)` | dùng hệ thống build của cây kernel, nhưng build file trong thư mục hiện tại |
| `LOCALVERSION` | phần đuôi tên phiên bản. Module chỉ nạp được khi chuỗi phiên bản (**vermagic**) khớp với kernel đang chạy: `6.1.119-ti-gc490f4c0fe51` |

Cây kernel phải được build ít nhất một lần trước (phần 3), vì module cần các file sinh ra lúc build kernel.

**Thử trên board:**
```bash
make && make install                                 # trên PC
insmod /home/root/hello_kernel.ko                    # trên board
ls /sys/class/hello-kernel/device0/
echo on > /sys/class/hello-kernel/device0/test
cat /sys/class/hello-kernel/device0/test             # LED status is : ON
dmesg | grep DRIVER_Iris                             # các dòng printk của driver
rmmod hello_kernel
```
`printk` (hoặc `pr_info`, `dev_info`) là cách in ra log kernel; xem bằng `dmesg`.

### 5.3. Lấy GPIO: kiểu cũ và kiểu device tree
Bài `led28.c` thử **API GPIO cũ**, dùng số GPIO toàn cục:
```c
gpio_request(28, "mygpio");
gpio_direction_output(28, 0);
gpio_set_value_cansleep(28, 1);
```
Vấn đề: con số 28 phụ thuộc cách kernel đánh số các bộ GPIO, có thể khác giữa các phiên bản kernel; driver cũng không biết chân đó đã được ai dùng chưa. API này đã bị khuyến cáo bỏ.

**Cách mới:** mô tả chân trong **device tree**, driver chỉ xin "GPIO của tôi" và nhận một **descriptor** (`struct gpio_desc *`). Node DTS (thêm vào `am335x-boneblack.dts`):
```dts
/ {
	hello_led {
		compatible = "hello-led";
		pinctrl-names = "default";
		pinctrl-0 = <&hello_led_pins>;
		gpios = <&gpio1 12 GPIO_ACTIVE_HIGH>;      /* LED + điện trở 330 Ω từ P8-12 xuống GND */
	};
};

&am33xx_pinmux {
	hello_led_pins: hello_led_pins {
		pinctrl-single,pins = <
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD12, PIN_OUTPUT, MUX_MODE7)	/* P8-12 = gpio1_12 */
		>;
	};
};
```
> Bản gốc của dự án dùng GPIO số 28. Nếu đó là `gpio1_28` thì đó chính là **P9-12**, chân mà sau này được dùng làm **DC của màn hình ST7789**. Khi đã nối LCD, đặt LED sang chân khác như P8-12 ở trên. Node `hello_led` gốc không còn trong file DTS hiện tại, nên cần thêm lại.

Hai cách đọc node này trong driver:
```c
/* cách dự án đã dùng: tự tìm node theo đường dẫn */
np = of_find_node_by_path("/hello_led");
g = gpiod_get_from_of_node(np, "gpios", 0, GPIOD_OUT_LOW, "hello_led");
of_node_put(np);

/* cách chuẩn trong platform driver (5.4): kernel đã đưa sẵn node cho probe() */
g = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);    /* đọc thuộc tính "gpios" */
gpiod_set_value_cansleep(g, 1);
```
`GPIO_ACTIVE_HIGH/LOW` trong DTS quyết định "1" nghĩa là mức điện áp nào; driver luôn làm việc với giá trị **logic** (1 = bật).

### 5.4. Platform driver: để kernel tự gọi driver
**Ý tưởng:** module ở 5.2 chạy `init` ngay khi nạp, kể cả khi phần cứng không có. Platform driver thì khác: driver khai báo một chuỗi `compatible`; kernel chỉ gọi `probe()` khi device tree **có node khớp**. Đây là kiểu của mọi driver thật (ST7789 ở phần 7 cũng vậy).

`drivers/hello-driver/hello_kernel.c` trong cây kernel:
```c
static const struct of_device_id hello_of_driver[] = {
	{ .compatible = "hello-led" },          /* trùng với node DTS ở 5.3 */
	{ }
};

static struct platform_driver hello_platform_driver = {
	.driver = {
		.name = "hello-driver",
		.of_match_table = hello_of_driver,
	},
	.probe  = hello_driver_init,     /* lấy GPIO, tạo class/device/thuộc tính "test" */
	.remove = hello_driver_exit,     /* gỡ theo thứ tự ngược lại */
};
module_platform_driver(hello_platform_driver);   /* thay cho cặp module_init/module_exit */
```
Hàm `test_store` lúc này vừa cập nhật biến vừa điều khiển chân thật:
```c
gpiod_set_value_cansleep(g, 1);   /* "on" */
gpiod_set_value_cansleep(g, 0);   /* "off" */
```

**Build cứng vào kernel** (driver nằm trong `zImage`, không cần `insmod`):
```make
# drivers/hello-driver/Makefile
obj-y += hello_kernel.o
```
```make
# drivers/Makefile — thêm vào cuối
obj-y += hello-driver/
```
Build lại `zImage` (phần 3.2) và `am335x-boneblack.dtb` có node `hello_led`. Sau khi boot:
```bash
dmesg | grep -i 'Driver_Iris'                   # [Driver_Iris]: LOAD OK
echo on  > /sys/class/hello-kernel/device0/test
echo off > /sys/class/hello-kernel/device0/test
```
Không có node `hello_led` trong DTB → `probe()` **không bao giờ chạy** và cũng không có log nào. Đó là hành vi đúng của platform driver, không phải lỗi.

Nên sửa thêm trong bản này khi viết lại:
- Dùng `devm_gpiod_get(&pdev->dev, …)` thay cho `of_find_node_by_path` (5.3).
- `sysfs_emit` thay cho `sprintf`; `sysfs_streq` thay cho `strstr` (vì `strstr` coi `"hon"` cũng là `"on"`).
- Kiểm tra lỗi của `device_create_file` và trả mã lỗi đúng (`-ENODEV`, `PTR_ERR(...)`) thay vì `-1`.

---

## 6. Character device: `/dev/hello_device` với ioctl

### Ý tưởng
sysfs hợp với vài giá trị cấu hình. Khi chương trình cần **gọi lệnh** vào driver (đọc/ghi dữ liệu, gửi lệnh có tham số), Linux dùng **character device**: một file đặc biệt trong `/dev/`. Mỗi lời gọi `open/read/write/ioctl/close` của chương trình được kernel chuyển tới **hàm tương ứng trong driver**.

```
led-ctrl:  ioctl(fd, IOCTL_LED_SET, &led)
   └─► kernel VFS ─► số (major, minor) của /dev/hello_device ─► cdev ─► hello_ioctl(file, cmd, arg)
```
| Khái niệm | Vai trò |
|---|---|
| `dev_t` (major, minor) | "số nhà" của thiết bị. `alloc_chrdev_region` xin kernel cấp |
| `struct file_operations` | bảng con trỏ hàm: `.open`, `.read`, `.write`, `.unlocked_ioctl`… |
| `struct cdev` | gắn bảng hàm đó với số (major, minor) — `cdev_init` + `cdev_add` |
| `class_create` + `device_create` | để udev/devtmpfs **tự tạo** file `/dev/hello_device` |
| `ioctl` | "lệnh tuỳ ý": mỗi lệnh là một con số mã hoá chiều dữ liệu + kích thước |

### 6.1. Mã lệnh ioctl
Dự án định nghĩa hai lệnh, dùng chung cho driver và chương trình (nên đặt trong một header chung, ví dụ `hello_led_ioctl.h`):
```c
#include <linux/ioctl.h>

#define HELLO_MAGIC	'H'
#define IOCTL_LED_SET	_IOW(HELLO_MAGIC, 1, int)	/* user → driver: một int */
#define IOCTL_LED_GET	_IOR(HELLO_MAGIC, 2, int)	/* driver → user: một int */
```
`_IOW`/`_IOR` gói **chữ "magic"** (nhóm lệnh của driver), **số thứ tự**, **chiều** và **kích thước** dữ liệu vào một con số 32 bit, để hai driver khác nhau khó trùng lệnh.

### 6.2. Bản đầu tiên của dự án và những chỗ cần sửa
`~/BBB/simple-driver/hello_kernel_driver.c` đã làm được phần chính: xin số thiết bị, `cdev`, class `hello_class`, file `/dev/hello_device`, `ioctl` bật/tắt và đọc LED. Chương trình `~/BBB/led-ctrl/main.c` gọi hai lệnh đó.

Khi học lại, đây là những lỗi đáng chú ý trong bản đầu — đều là lỗi rất hay gặp:

| Chỗ trong code | Vấn đề | Sửa |
|---|---|---|
| `if (*(int *)data == 0)` trong `IOCTL_LED_SET` | `data` là **địa chỉ trong user space**. Kernel đọc thẳng → sai bảo mật, có thể crash | `get_user(val, (int __user *)arg)` |
| `copy_to_user(...)` bỏ qua kết quả | Địa chỉ sai sẽ không được báo lỗi | `return put_user(val, uarg);` |
| `switch` không có `default` | Lệnh lạ vẫn trả 0 (thành công) | `default: return -ENOTTY;` |
| `hello_read` luôn trả 0 | `cat /dev/hello_device` không đọc được gì | Trả trạng thái bằng `simple_read_from_buffer` |
| `hello_exit` thiếu `cdev_del` | Gỡ module xong, kernel vẫn giữ con trỏ tới hàm đã bị xoá → crash khi có ai mở file | `cdev_del` trước `unregister_chrdev_region` |
| Không kiểm tra `class_create`/`device_create`; lỗi giữa chừng trả `-1` mà không dọn | Rò rỉ số thiết bị; lần nạp sau báo lỗi | Kiểm tra `IS_ERR`, dọn bằng nhãn `goto` |
| `module_init` + `of_find_node_by_path` | Chạy cả khi không có phần cứng | Chuyển thành platform driver (5.4) |
| `gpiod_set_value_cansleep(g, 1)` ngay khi nạp | LED sáng khi vừa `insmod`, trạng thái lệch với `led_status` | Bắt đầu ở 0 (`GPIOD_OUT_LOW`) |

### 6.3. Bản đã sửa
Gộp tất cả: platform driver (5.4) + character device + ioctl + read/write. Tên file và mã lệnh giữ nguyên như dự án, nên `led-ctrl` dùng lại được.

`hello_led_ioctl.h` — như ở 6.1.

`hello_kernel_driver.c`:
```c
// hello_kernel_driver.c — /dev/hello_device: bat/tat LED bang ioctl, read, write (kernel 6.1)
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>
#include <linux/mutex.h>

#include "hello_led_ioctl.h"

#define DRV_NAME "hello_device"

struct hello {
	struct gpio_desc *gpio;
	dev_t devt;			/* (major, minor) */
	struct cdev cdev;
	struct class *class;
	struct mutex lock;
	int state;
};

static int hello_open(struct inode *inode, struct file *file)
{
	/* tu cdev tim nguoc ra struct hello chua no */
	file->private_data = container_of(inode->i_cdev, struct hello, cdev);
	return 0;
}

static void hello_set(struct hello *h, int on)
{
	mutex_lock(&h->lock);
	h->state = !!on;
	gpiod_set_value_cansleep(h->gpio, h->state);
	mutex_unlock(&h->lock);
}

static long hello_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hello *h = file->private_data;
	int __user *uarg = (int __user *)arg;	/* dia chi trong user space */
	int val;

	switch (cmd) {
	case IOCTL_LED_SET:
		if (get_user(val, uarg))	/* KHONG duoc viet *(int *)arg */
			return -EFAULT;
		hello_set(h, val);
		return 0;
	case IOCTL_LED_GET:
		mutex_lock(&h->lock);
		val = h->state;
		mutex_unlock(&h->lock);
		return put_user(val, uarg);	/* 0 hoac -EFAULT */
	default:
		return -ENOTTY;			/* lenh ioctl khong ho tro */
	}
}

static ssize_t hello_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	struct hello *h = file->private_data;
	char msg[4];
	int n;

	mutex_lock(&h->lock);
	n = scnprintf(msg, sizeof(msg), "%d\n", h->state);
	mutex_unlock(&h->lock);

	/* copy ra user space va tang *off, nen "cat" dung lai sau 1 lan */
	return simple_read_from_buffer(buf, len, off, msg, n);
}

static ssize_t hello_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	struct hello *h = file->private_data;
	char c;

	if (len == 0)
		return 0;
	if (copy_from_user(&c, buf, 1))
		return -EFAULT;
	if (c != '0' && c != '1')
		return -EINVAL;

	hello_set(h, c == '1');
	return len;
}

static const struct file_operations hello_fops = {
	.owner		= THIS_MODULE,
	.open		= hello_open,
	.read		= hello_read,
	.write		= hello_write,
	.unlocked_ioctl	= hello_ioctl,
};

static int hello_probe(struct platform_device *pdev)
{
	struct hello *h;
	struct device *dev;
	int ret;

	h = devm_kzalloc(&pdev->dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	mutex_init(&h->lock);

	/* thuoc tinh "gpios" cua chinh node DTS da khop compatible */
	h->gpio = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(h->gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(h->gpio), "cannot get gpio\n");

	ret = alloc_chrdev_region(&h->devt, 0, 1, DRV_NAME);
	if (ret)
		return ret;

	cdev_init(&h->cdev, &hello_fops);
	h->cdev.owner = THIS_MODULE;
	ret = cdev_add(&h->cdev, h->devt, 1);
	if (ret)
		goto err_region;

	h->class = class_create(THIS_MODULE, "hello_class");	/* kernel >= 6.4: class_create("hello_class") */
	if (IS_ERR(h->class)) {
		ret = PTR_ERR(h->class);
		goto err_cdev;
	}

	dev = device_create(h->class, &pdev->dev, h->devt, NULL, DRV_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_class;
	}

	platform_set_drvdata(pdev, h);
	dev_info(&pdev->dev, "/dev/%s ready (major %d, minor %d)\n",
		 DRV_NAME, MAJOR(h->devt), MINOR(h->devt));
	return 0;

err_class:
	class_destroy(h->class);
err_cdev:
	cdev_del(&h->cdev);
err_region:
	unregister_chrdev_region(h->devt, 1);
	return ret;
}

static int hello_remove(struct platform_device *pdev)
{
	struct hello *h = platform_get_drvdata(pdev);

	/* go theo thu tu nguoc voi probe */
	device_destroy(h->class, h->devt);
	class_destroy(h->class);
	cdev_del(&h->cdev);
	unregister_chrdev_region(h->devt, 1);
	gpiod_set_value_cansleep(h->gpio, 0);
	return 0;
}

static const struct of_device_id hello_of_match[] = {
	{ .compatible = "hello-led" },
	{ }
};
MODULE_DEVICE_TABLE(of, hello_of_match);

static struct platform_driver hello_driver = {
	.driver = {
		.name		= "hello-driver",
		.of_match_table	= hello_of_match,
	},
	.probe	= hello_probe,
	.remove	= hello_remove,
};
module_platform_driver(hello_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IRIS");
MODULE_DESCRIPTION("LED character device with ioctl");
```
Bản này đã được biên dịch thử với cây kernel 6.1 của dự án, không lỗi, không cảnh báo.

Những điểm then chốt:
- **Không bao giờ** đọc/ghi trực tiếp một con trỏ đến từ user space. Dùng `get_user`/`put_user` (một biến) hoặc `copy_from_user`/`copy_to_user` (một vùng nhớ).
- Hàm trả **số byte đã xử lý** hoặc **mã lỗi âm** (`-EFAULT`, `-EINVAL`, `-ENOTTY`); `read` trả 0 nghĩa là "hết dữ liệu".
- Mọi thứ tạo trong `probe` được gỡ **theo thứ tự ngược** trong `remove` và trong các nhãn `goto err_…`.
- `mutex` bảo vệ `state`, vì hai chương trình có thể gọi cùng lúc.
- Một struct `hello` cho mỗi thiết bị thay vì biến toàn cục: nếu DTS có hai node `hello-led`, driver tạo được hai thiết bị (khi đó cần xin 2 minor và đặt tên khác nhau).

Không muốn đụng cây kernel? Để file này cùng `hello_led_ioctl.h` trong `~/BBB/simple-driver/`, đổi dòng đầu Makefile ở 5.2 thành `obj-m += hello_kernel_driver.o`. Nhớ **bỏ** bản built-in ở 5.4 khỏi kernel (xoá dòng `obj-y += hello-driver/`) để hai driver không cùng tranh node `hello-led`.

### 6.4. Chương trình user space `led-ctrl`
```c
// led-ctrl/main.c — bat LED 2 giay qua /dev/hello_device
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hello_led_ioctl.h"

int main(void)
{
	int led;
	int fd = open("/dev/hello_device", O_RDWR);

	if (fd < 0) {
		perror("open /dev/hello_device");
		return 1;
	}

	if (ioctl(fd, IOCTL_LED_GET, &led) < 0) {
		perror("IOCTL_LED_GET");
		close(fd);
		return 1;
	}
	printf("LED hien tai: %s\n", led ? "ON" : "OFF");

	led = 1;
	if (ioctl(fd, IOCTL_LED_SET, &led) < 0)
		perror("IOCTL_LED_SET");
	sleep(2);

	led = 0;
	if (ioctl(fd, IOCTL_LED_SET, &led) < 0)
		perror("IOCTL_LED_SET");

	close(fd);
	return 0;
}
```
So với bản đầu: dùng header chung thay vì định nghĩa lại mã lệnh; bỏ `#include` đường dẫn tương đối vào sysroot (compiler của SDK đã tự tìm header qua `--sysroot`); dùng `perror` để in đúng lý do lỗi (`ioctl` trả `-1`, mã lỗi nằm trong `errno`).

Makefile, biên dịch **cho ARM**:
```make
PROGRAM_NAME = led-ctrl

all:
	$(CC) $(CFLAGS) -Wall main.c -o $(PROGRAM_NAME)
install:
	scp $(PROGRAM_NAME) root@192.168.9.8:/home/root
```
```bash
source ~/ti-sdk-kernel/linux-devkit/environment-setup     # đặt CC, CFLAGS cho ARM
make && file led-ctrl && make install
```

### 6.5. Thử trên board
```bash
insmod /home/root/hello_kernel_driver.ko        # bỏ qua nếu build cứng
dmesg | tail -1          # hello-driver hello_led: /dev/hello_device ready (major 2xx, minor 0)
ls -l /dev/hello_device  # crw------- … 2xx, 0   ← chữ "c" = character device
cat /proc/devices | grep hello_device

/home/root/led-ctrl      # LED hien tai: OFF → sáng 2 giây → tắt

echo 1 > /dev/hello_device   # cũng bật được bằng write
cat /dev/hello_device        # 1
echo 0 > /dev/hello_device
echo x > /dev/hello_device   # Invalid argument (-EINVAL)
rmmod hello_kernel_driver
```

### 6.6. sysfs hay character device?
| | sysfs (5.2, 5.4) | character device (6) |
|---|---|---|
| Hợp với | một giá trị đơn giản, cấu hình | luồng dữ liệu, lệnh có tham số, `poll` |
| Tạo | `DEVICE_ATTR_RW` | `cdev` + `file_operations` |
| Đường dẫn | `/sys/class/…/thuộc-tính` | `/dev/tên` |
| Dùng từ shell | `echo`/`cat` | `echo`/`cat` cho read/write; `ioctl` cần chương trình C |

Thực tế với LED, kernel đã có sẵn khung `leds-gpio` (bài 5.1). Viết tay ở đây là để học cơ chế mà mọi driver đều dùng.

---

## 7. Driver thật: màn hình SPI ST7789

### Ý tưởng
Kernel có sẵn **fbtft** (`drivers/staging/fbtft/`) – một khung cho màn hình SPI nhỏ, tạo **framebuffer** `/dev/fb0`. Mọi chương trình vẽ vào `/dev/fb0` (kể cả Qt) sẽ hiện lên LCD. Việc của bạn: nối dây, khai báo device tree, bật config, và **sửa driver `fb_st7789v.c`** cho đúng tấm 1,47" 172×320.

### 7.1. Nối dây
| LCD | BBB | Chức năng |
|---|---|---|
| VCC | P9-3 | 3,3V |
| GND | P9-1 | GND |
| SCL | P9-22 | spi0_sclk |
| SDA | P9-18 | spi0_d1 (MOSI) |
| CS | P9-17 | spi0_cs0 |
| DC | P9-12 | gpio1_28 |
| RST | P9-23 | gpio1_17 |
| BL | P9-15 | gpio1_16 |

### 7.2. Config kernel
```
CONFIG_STAGING=y                 # fbtft nằm trong nhóm "staging"
CONFIG_FB=y
CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_FRAMEBUFFER_CONSOLE=y     # hiện console chữ lên LCD lúc boot
CONFIG_FB_TFT=y
CONFIG_FB_TFT_ST7789V=y
```
Bật bằng `menuconfig`, hoặc ghi vào một file `st7789.cfg` rồi gộp bằng `merge_config.sh` (phần 3.1) — cùng file này sẽ dùng lại ở Yocto.

### 7.3. Device tree
Cuối `am335x-boneblack.dts`:
```dts
&am33xx_pinmux {
	bbb_spi0_pins: bbb_spi0_pins {
		pinctrl-single,pins = <
			AM33XX_PADCONF(AM335X_PIN_SPI0_SCLK, PIN_INPUT_PULLUP, MUX_MODE0)	/* P9-22 */
			AM33XX_PADCONF(AM335X_PIN_SPI0_D0,   PIN_INPUT_PULLUP, MUX_MODE0)	/* P9-21 */
			AM33XX_PADCONF(AM335X_PIN_SPI0_D1,   PIN_INPUT_PULLUP, MUX_MODE0)	/* P9-18 */
			AM33XX_PADCONF(AM335X_PIN_SPI0_CS0,  PIN_INPUT_PULLUP, MUX_MODE0)	/* P9-17 */
			AM33XX_PADCONF(AM335X_PIN_GPMC_BEN1, PIN_OUTPUT, MUX_MODE7)		/* P9-12 DC  gpio1_28 */
			AM33XX_PADCONF(AM335X_PIN_GPMC_A1,   PIN_OUTPUT, MUX_MODE7)		/* P9-23 RST gpio1_17 */
			AM33XX_PADCONF(AM335X_PIN_GPMC_A0,   PIN_OUTPUT, MUX_MODE7)		/* P9-15 BL  gpio1_16 */
		>;
	};
};

&spi0 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&bbb_spi0_pins>;

	st7789@0 {
		reg = <0>;
		compatible = "sitronix,st7789v";
		spi-max-frequency = <16000000>;		/* không có dòng này → SPI chạy tốc độ tối đa, dễ lỗi */
		width = <172>;
		height = <320>;
		buswidth = <8>;
		dc-gpios = <&gpio1 28 GPIO_ACTIVE_HIGH>;
		reset-gpios = <&gpio1 17 GPIO_ACTIVE_LOW>;
		led-gpios = <&gpio1 16 GPIO_ACTIVE_HIGH>;	/* đèn nền */
	};
};
```

### 7.4. Sửa driver `drivers/staging/fbtft/fb_st7789v.c`
Driver gốc viết cho tấm 240×320. Ba chỗ cần sửa:

**a) Bật đèn nền.** Thiếu `.backlight = 1`, fbtft không đăng ký backlight → chân `led-gpios` giữ mức thấp → **màn hình đen dù driver chạy bình thường**.

**b) Kích thước** `.width = 172`.

**c) Lệch cột.** Chip ST7789 có RAM 240 cột, nhưng tấm 172 cột chỉ nối với **cột 34 → 205**. Phải dời cửa sổ ghi:
```c
#define COL_OFFSET 34

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	switch (par->info->var.rotate) {
	case 90:
	case 270:
		ys += COL_OFFSET;	/* xoay 90° thì trục đổi chỗ, offset chuyển sang hàng */
		ye += COL_OFFSET;
		break;
	default:
		xs += COL_OFFSET;
		xe += COL_OFFSET;
		break;
	}
	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
		  (xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);
	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
		  (ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);
	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = 172,
	.height = 320,
	/* … */
	.backlight = 1,
	.fbtftops = {
		/* … */
		.set_addr_win = set_addr_win,
	},
};
```

### 7.5. Build, deploy, kiểm tra
Build lại `zImage` và `am335x-boneblack.dtb` (phần 3.2), copy sang `/tftpboot`, reset. Trên board:
```bash
dmesg | grep -iE 'fbtft|st7789'
#   graphics fb0: fb_st7789v frame buffer, 172x320, 107 KiB video memory, … spi0.0 at 16 MHz
ls /dev/fb*
cat /dev/urandom > /dev/fb0      # LCD đầy nhiễu màu; "File too large" ở cuối là bình thường
cat /dev/zero > /dev/fb0         # xoá đen
```
Dòng `SPI driver fb_st7789v has no spi_device_id for sitronix,st7789v` chỉ là cảnh báo, bỏ qua được khi driver build cứng.

### 7.6. Debug từ dưới lên
Khi màn hình không lên, kiểm tra theo đúng thứ tự, **đừng nhảy cóc**:
1. `uname -a` – board có chạy **đúng kernel vừa build** không? (TFTP: xem `Bytes transferred` của dtb có đúng kích thước file mới.)
2. `dtc -I dtb -O dts` trên file trong `/tftpboot` – node có đủ thuộc tính không?
3. `dmesg` – driver có probe không?
4. `/dev/urandom > /dev/fb0` – phần cứng + driver có vẽ được không?
5. Rồi mới tới app.

---

## 8. App Qt trên framebuffer

### 8.1. Dựng môi trường build chéo
App build trên PC nhưng chạy trên ARM → dùng **Qt SDK** trong `linux-devkit`:
```bash
source ~/ti-sdk-kernel/linux-devkit/environment-setup
qtcreator ~/ok/ok.pro &
```
**Bắt buộc** `source` trước khi mở Qt Creator: `qmake` của SDK cần các biến này để tìm thư viện Qt cho ARM. Mở Qt Creator từ menu → kit bị mờ, báo "No suitable kits found".

Trong Qt Creator (bản 6.0.2 của Ubuntu, `/usr/bin/qtcreator`):
- **Devices**: Generic Linux, `root@192.168.9.8` → nút **Test** (cảnh báo thiếu `rsync` là bình thường, sẽ dùng SFTP).
- **Kit "BBB Kit"**: Qt version = `linux-devkit/sysroots/x86_64-arago-linux/usr/bin/qmake`, device = BBB.
- **Projects → Run → Environment**: thêm
  ```
  QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=172x320
  ```
- `Ctrl+R`: build → copy lên `/opt/ok/bin/ok` → chạy.

### 8.2. Vì sao `linuxfb`?
Rootfs của TI mặc định cho Qt dùng `eglfs_kms` (GPU + DRM). fbtft chỉ là framebuffer thuần, không có DRM → phải dùng plugin **linuxfb**.

### 8.3. Giao diện vừa 172×320
- `main.cpp`: `w.showFullScreen()` khi chạy trên `linuxfb`, `setFixedSize(172, 320)` khi chạy trên PC (để xem trước bằng kit Desktop).
- Thiết kế trong `mainwindow.ui` (tab **Design**) với layout dọc; màu và cỡ chữ đặt trong **styleSheet của MainWindow** (cỡ chữ đặt ở đây ghi đè thuộc tính `font` của từng widget).
- Code chỉ cập nhật số liệu mỗi giây: đọc `/proc/stat`, `/proc/meminfo`, `/proc/uptime`, `QNetworkInterface` (cần `QT += network` trong `ok.pro`).

### 8.4. Tự chạy khi boot (systemd)
```ini
# /etc/systemd/system/ok-dashboard.service
[Unit]
Description=BBB ST7789 LCD dashboard

[Service]
Environment=QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=172x320
Environment=XDG_RUNTIME_DIR=/tmp
ExecStartPre=-/bin/sh -c 'echo 0 > /sys/class/graphics/fbcon/cursor_blink'
ExecStart=/opt/ok/bin/ok
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```
```bash
systemctl enable --now ok-dashboard
systemctl stop ok-dashboard        # trước khi Ctrl+R trong Qt Creator, tránh hai app cùng vẽ
```
Cải tiến: thay `XDG_RUNTIME_DIR=/tmp` bằng `RuntimeDirectory=ok-dashboard`, `RuntimeDirectoryMode=0700`, `Environment=XDG_RUNTIME_DIR=/run/ok-dashboard` để hết cảnh báo quyền `0777`.

---

## 9. Yocto: đóng gói mọi thứ thành một image

### 9.1. Yocto làm gì?
Đến phần 8, rootfs bạn dùng là của TI dựng sẵn; kernel và app build tay; cấu hình nằm rải rác. **Yocto** build **toàn bộ** hệ thống từ source theo "công thức", ra một image tái tạo được.

| Thuật ngữ | Nghĩa |
|---|---|
| **recipe** (`.bb`) | công thức build một phần mềm |
| **bbappend** | "nối thêm" vào recipe của người khác mà không sửa file gốc |
| **layer** (`meta-…`) | thư mục chứa recipe; hệ thống ghép nhiều layer |
| **bitbake** | công cụ chạy recipe |
| **task** | một bước của recipe: `do_fetch`, `do_patch`, `do_compile`… |
| **image** | recipe đặc biệt: gom các gói thành rootfs |
| **sstate-cache** | cache kết quả task; lần sau không phải build lại |

```
layers + local.conf ─► bitbake: gộp recipe + bbappend ─► đồ thị task
   ─► mỗi recipe: fetch → unpack → patch → configure → compile → install → package → deploy
   ─► gói .ipk ─► image: do_rootfs ─► tar.xz / wic.xz ─► /tftpboot + thư mục NFS ─► BBB
```

### 9.2. Môi trường
Cây Yocto của TI ở `~/tisdk` (dựng bằng `oe-layersetup` theo config của SDK, nhánh **kirkstone**). Mỗi terminal mới:
```bash
cd ~/tisdk/build
source conf/setenv
export MACHINE=am335x-evm      # cấu hình chung AM335x, dùng được cho BBB
```
Đặt alias cho nhanh:
```bash
echo "alias yocto='cd ~/tisdk/build && source conf/setenv && export MACHINE=am335x-evm'" >> ~/.bashrc
```
> ⚠️ Dùng **terminal riêng** cho Yocto — đừng dùng terminal đã `source linux-devkit/environment-setup` (biến CC/PATH sẽ lẫn).

Thêm vào cuối `conf/local.conf` để tiết kiệm đĩa:
```
INHERIT += "rm_work"
RM_WORK_EXCLUDE += "ok-dashboard linux-ti-staging base-files"
```
`rm_work` xoá thư mục làm việc của mỗi recipe sau khi build xong; `RM_WORK_EXCLUDE` giữ lại recipe của bạn để còn xem log.

### 9.3. Tạo layer riêng
```bash
bitbake-layers create-layer ../sources/meta-iris
bitbake-layers add-layer ../sources/meta-iris
rm -r ../sources/meta-iris/recipes-example
```
Trong `meta-iris/conf/layer.conf`, đặt priority cao hơn meta-arago (10):
```
BBFILE_PRIORITY_meta-iris = "15"
```
Priority quyết định layer nào thắng khi hai layer cùng cung cấp một file (xem 9.5).

### 9.4. Kernel: bbappend + patch + config fragment
Yocto **không dùng** cây kernel trong SDK; nó tự tải source TI cùng commit (`c490f4c0fe`). Thay đổi của bạn đi vào dưới dạng **patch**:
```bash
cd $KDIR
git diff -- arch/arm/boot/dts/am335x-boneblack.dts drivers/staging/fbtft/fb_st7789v.c \
    > ~/0001-bbb-st7789-lcd.patch
mkdir -p ~/tisdk/sources/meta-iris/recipes-kernel/linux/files
cp ~/0001-bbb-st7789-lcd.patch st7789.cfg ~/tisdk/sources/meta-iris/recipes-kernel/linux/files/
```
`meta-iris/recipes-kernel/linux/linux-ti-staging_%.bbappend`:
```
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://0001-bbb-st7789-lcd.patch \
    file://st7789.cfg \
"

KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/st7789.cfg"
```
| Dòng | Ý nghĩa |
|---|---|
| tên `linux-ti-staging_%` | trùng tên recipe `linux-ti-staging_6.1.bb` trong meta-ti; `%` = mọi phiên bản |
| `FILESEXTRAPATHS:prepend :=` | tìm `file://` trong `files/` của layer này trước; `:=` tính ngay để `THISDIR` đúng |
| `SRC_URI +=` | file `.patch` tự được áp ở `do_patch`; `.cfg` được copy vào `WORKDIR` |
| `KERNEL_CONFIG_FRAGMENTS` | biến của meta-ti; gộp fragment vào `multi_v7_defconfig` + prune + no_smp. Nhớ **dấu cách đầu chuỗi** khi dùng `:append` |

Kiểm tra trước, build sau:
```bash
bitbake-layers show-appends | grep -A2 'linux-ti-staging_6.1'
bitbake -e linux-ti-staging > ~/kernel-env.txt
grep -E '^(SRC_URI|KERNEL_CONFIG_FRAGMENTS|S|B)=' ~/kernel-env.txt
bitbake linux-ti-staging -c patch        # chỉ tới bước áp patch: biết ngay patch có khớp
bitbake linux-ti-staging                  # build thật
```
`bitbake -e` in mọi biến sau khi gộp — công cụ debug quan trọng nhất của Yocto. `S` = thư mục source đã vá, `B` = thư mục build (chứa `.config`).

Kết quả trong `deploy-ti/images/am335x-evm/`: `zImage`, `am335x-boneblack.dtb` (lớn hơn bản build tay vì có bảng symbol `-@` cho overlay).

Trên board, kernel Yocto nhận ra qua `cat /proc/version`: `oe-user@oe-host`, `arm-oe-linux-gnueabi-gcc 11.5`. Ngày build là ngày commit kernel (reproducible build), không phải hôm nay.

### 9.5. Đổi "Arago Project" thành "Iris": bbappend cho base-files
`/etc/issue` (chữ trước dấu nhắc login) do recipe `base-files` tạo: meta-arago cung cấp file `issue`, oe-core nối thêm `${DISTRO_NAME} ${DISTRO_VERSION} \n \l`.

```
meta-iris/recipes-core/base-files/
├── base-files_%.bbappend
└── files/issue, files/issue.net
```
`base-files_%.bbappend`:
```
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
DISTRO_NAME = "Iris"          # chỉ có tác dụng trong recipe này
```
`files/issue` (không dùng dấu `\` trong hình vì agetty coi `\` là ký hiệu đặc biệt):
```

 ___       _
|_ _| _ _ (_) ___
 | | | '_|| |(_-<
|___||_|  |_|/__/

```
Kiểm tra `bitbake -e base-files | grep ^FILESPATH=`: thư mục của meta-iris phải đứng **trước** meta-arago (nhờ priority 15).

### 9.6. Recipe cho app Qt
```
meta-iris/recipes-qt/ok-dashboard/
├── ok-dashboard_1.0.bb
└── files/ ok.pro main.cpp mainwindow.cpp mainwindow.h mainwindow.ui ok-dashboard.service
```
`ok-dashboard_1.0.bb`:
```
SUMMARY = "Dashboard he thong cho LCD ST7789 172x320 tren BeagleBone Black"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://ok.pro \
    file://main.cpp \
    file://mainwindow.cpp \
    file://mainwindow.h \
    file://mainwindow.ui \
    file://ok-dashboard.service \
"
S = "${WORKDIR}"

DEPENDS = "qtbase"
inherit qmake5 systemd

SYSTEMD_SERVICE:${PN} = "ok-dashboard.service"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/ok-dashboard.service ${D}${systemd_system_unitdir}/
}

FILES:${PN} += "/opt/ok"
RDEPENDS:${PN} += "qtbase-plugins liberation-fonts"
```
| Dòng | Ý nghĩa |
|---|---|
| `LICENSE` + `LIC_FILES_CHKSUM` | bắt buộc với mọi recipe |
| `S = "${WORKDIR}"` | source là các file `file://` được copy thẳng vào `WORKDIR` |
| `DEPENDS` | cần lúc **build** (header, thư viện Qt) |
| `inherit qmake5` | tự làm configure/compile/install theo `ok.pro` (`target.path = /opt/ok/bin`) |
| `inherit systemd` + `SYSTEMD_SERVICE` | enable service khi cài gói |
| `FILES:${PN} += "/opt/ok"` | `/opt` không thuộc đường dẫn mặc định → phải khai báo, nếu không lỗi *installed-vs-shipped* |
| `RDEPENDS` | cần lúc **chạy**: plugin `libqlinuxfb.so`, font |

Lần đầu `bitbake ok-dashboard` build cả Qt5 (30–90 phút). Kiểm tra:
```bash
file arago-tmp-default-glibc/work/*/ok-dashboard/*/image/opt/ok/bin/ok   # ELF 32-bit … ARM
```
Source trong `files/` là **bản sao** của `~/ok`: sửa app xong phải copy lại.

### 9.7. Recipe image
`meta-iris/recipes-core/images/iris-image.bb`:
```
SUMMARY = "Iris image: TI base image + dashboard tren LCD ST7789"

require recipes-core/images/tisdk-base-image.bb

IMAGE_INSTALL += "ok-dashboard resize-rootfs"
export IMAGE_BASENAME = "iris-image"

# bo logo TI luc boot (psplash ve de len /dev/fb0)
IMAGE_FEATURES:remove = "splash"

# BBB khong co NAND -> bo ubi/ubifs
IMAGE_FSTYPES = "tar.xz wic.xz wic.bmap"
```
- `require`: kế thừa toàn bộ `tisdk-base-image` (packagegroup-arago-base, -console).
- Chỉ cần thêm `ok-dashboard`; Qt, font, glibc tự được kéo theo qua `RDEPENDS`.
- bbappend kernel và base-files tự áp dụng cho image.

```bash
bitbake iris-image
ls -lhL deploy-ti/images/am335x-evm/iris-image-am335x-evm.{tar.xz,wic.xz}
tar -tJf deploy-ti/images/am335x-evm/iris-image-am335x-evm.tar.xz | grep -E 'opt/ok/bin/ok|ok-dashboard.service|etc/issue'
```

### 9.8. Boot image Yocto qua mạng
```bash
# rootfs → thư mục NFS mới (sudo để giữ chủ sở hữu root, file thiết bị, setuid)
sudo mkdir -p ~/ti-sdk-kernel/irisNFS
sudo tar -xJf deploy-ti/images/am335x-evm/iris-image-am335x-evm.tar.xz -C ~/ti-sdk-kernel/irisNFS

# khai báo NFS
echo '/home/iris/ti-sdk-kernel/irisNFS *(rw,nohide,insecure,no_subtree_check,async,no_root_squash)' | sudo tee -a /etc/exports
sudo exportfs -ra

# kernel + dtb (khớp module trong rootfs)
cp -L deploy-ti/images/am335x-evm/zImage /tftpboot/
cp -L deploy-ti/images/am335x-evm/am335x-boneblack.dtb /tftpboot/

# script minicom mới, đổi rootpath/nfsroot sang irisNFS
cd ~/ti-sdk-kernel/bin
cp setupBoard.minicom setupBoard-iris.minicom
sed -i 's|/home/iris/ti-sdk-kernel/targetNFS|/home/iris/ti-sdk-kernel/irisNFS|' setupBoard-iris.minicom
```
- `sudo tee -a` thay cho `sudo echo … >>` (phép `>>` chạy bằng quyền user thường).
- Cập nhật rootfs lần sau: giải nén vào thư mục **trống** — `tar` không xoá file đã bị bỏ khỏi image.

Boot: `sudo minicom … -S …/setupBoard-iris.minicom`, nhấn reset. Kết quả: login có chữ **Iris**, LCD tự hiện dashboard, `systemctl status ok-dashboard` = `active (running)`.

### 9.9. Đọc lỗi bitbake
1. Đừng đọc từ trên xuống — nhiều dòng đỏ thường là cùng một lỗi lặp lại.
2. Tìm `ERROR: Task (…:do_xxx) failed` → recipe nào, task nào.
3. Mở `Logfile of failure stored in: …/temp/log.do_xxx.<pid>` bằng `less`, gõ `/error`.
4. Sửa xong chạy lại cùng lệnh; bitbake tiếp tục từ chỗ hỏng.

| Lệnh | Khi nào |
|---|---|
| `bitbake <r> -c clean` | xoá thư mục làm việc của recipe |
| `bitbake <r> -c cleansstate` | xoá thêm cache của recipe |
| `bitbake -f -c <task> <r>` | ép chạy lại một task |
| `bitbake -e <r> \| grep ^VAR=` | xem giá trị cuối của biến |

### 9.10. Thời gian thực tế
| Việc | Thời gian |
|---|---|
| Build kernel tay (lần sau) | 1–3 phút |
| Build DTB tay | vài giây |
| `bitbake linux-ti-staging` sau khi sửa patch | 5–15 phút |
| `bitbake ok-dashboard` lần đầu (gồm Qt) | 30–90 phút |
| `bitbake iris-image` khi đã có cache | vài phút |
| Build lại toàn bộ sau khi xoá TMPDIR | 1–3 giờ |

---

## 10. Bảng lỗi đã gặp

| Hiện tượng | Nguyên nhân | Cách xử lý |
|---|---|---|
| Board vào Debian 4.14 thay vì kernel của mình | minicom không có `-S` | Luôn chạy minicom với script rồi mới reset |
| LCD đen, `dmesg` báo driver OK | Driver thiếu `.backlight = 1`; DTB thiếu `led-gpios` | Sửa driver + DTS (7.3, 7.4) |
| Hình lệch, mép có rác | Tấm 172 cột nằm ở cột 34–205 | `set_addr_win` + `COL_OFFSET 34` |
| `cat > /dev/fb0`: File too large | Framebuffer chỉ 110080 byte | Bình thường |
| Kit Qt mờ, "No suitable kits found" | Chưa `source linux-devkit/environment-setup` | Mở Qt Creator từ terminal đã source |
| App Qt chạy mà không hiện | Dùng `eglfs` thay vì `linuxfb`; cửa sổ 800×600 | `QT_QPA_PLATFORM=linuxfb:…`, `showFullScreen()` |
| Hết dung lượng đĩa khi build | Cache Qt Creator / pip / Yocto | `df -h /`, `du -sh ~/* \| sort -rh`, `ncdu ~`; bật `rm_work` |
| `do_package`: *unknown base path for fd*, *Bad address* | Tar Ubuntu (bản vá 07/2026) dùng syscall `openat2`; pseudo kirkstone không hỗ trợ | Tải tar gốc `1.34+dfsg-1build3` (`apt download`, `dpkg-deb -x`) vào `~/tisdk/oldtar`, rồi `ln -sfn ~/tisdk/oldtar/bin/tar arago-tmp-default-glibc/hosttools/tar` (làm lại nếu xoá TMPDIR) |
| glibc: *version-going-backwards* 2.39 → 2.35 | Cây layer từng chuyển sang scarthgap rồi về kirkstone, dùng chung thư mục build | Xoá TMPDIR, đổi tên `buildhistory`; mỗi phiên bản Yocto một thư mục build riêng |
| *trying to install files into a shared area* | `deploy-ti` còn file cũ không có manifest | Đổi tên `deploy-ti` → `deploy-ti.old` |
| `do_image_wic`: thiếu `zImage` | Stamp nói đã deploy nhưng file đã bị dời đi | `bitbake -f -c deploy linux-ti-staging` |
| `do_image_ubi` lỗi | Định dạng NAND của board EVM | `IMAGE_FSTYPES = "tar.xz wic.xz wic.bmap"` |
| LCD đứng ở logo Texas Instruments | Boot nhầm `targetNFS` (không có dashboard; psplash vẽ logo) | Dùng script `-iris`; bỏ `splash` khỏi image |
| `Could not get PHY for cpsw`, TFTP `T T T` | PHY Ethernet không khởi động sau reset nóng | Rút nguồn rồi cắm lại |
| Dashboard hiện năm 2000 | BBB không có pin giữ giờ, mạng không ra Internet | `date -s "…"` tạm thời; PC làm NTP server |

---

## 11. Phụ lục

### 11.1. Chân BBB đã dùng
| Chân | Pad / chức năng | Dùng cho |
|---|---|---|
| P9-17 | spi0_cs0 | LCD CS |
| P9-18 | spi0_d1 | LCD SDA (MOSI) |
| P9-21 | spi0_d0 | (MISO, LCD không dùng) |
| P9-22 | spi0_sclk | LCD SCL |
| P9-12 | gpio1_28 | LCD DC |
| P9-23 | gpio1_17 | LCD RST |
| P9-15 | gpio1_16 | LCD backlight |
| P8-12 | gpio1_12 | LED của hello driver (phần 5–6) |
| P9-27…31 | spi1 + gpio3_19 | dự kiến cho MCP2515 (CAN), cần tắt âm thanh HDMI |

### 11.2. Cấu trúc thư mục
```
~/ti-sdk-kernel/                       TI Processor SDK
├── board-support/ti-linux-kernel-…    cây kernel (git) — build tay ở đây
├── external-toolchain-dir/            gcc Arm 11.3
├── linux-devkit/                      Qt SDK (environment-setup)
├── targetNFS/                         rootfs TI
├── irisNFS/                           rootfs Yocto (iris-image)
├── bin/setupBoard*.minicom            script U-Boot
└── docs/                              tài liệu này
~/ok/                                  project Qt (Qt Creator)
~/BBB/                                 các bài driver: main.c (5.1), simple-driver/ (5.2, 5.3, 6), led-ctrl/ (6.4)
~/tisdk/                               Yocto
├── sources/                           các layer (oe-core, meta-ti, meta-arago, meta-qt5, …, meta-iris)
├── downloads/                         source đã tải
├── oldtar/                            tar 1.34 cho hosttools
└── build/
    ├── conf/{local.conf,bblayers.conf,setenv}
    ├── sstate-cache/
    ├── arago-tmp-default-glibc/       TMPDIR: work/, deploy/ipk/, log/
    └── deploy-ti/images/am335x-evm/   zImage, dtb, iris-image-*.tar.xz/.wic.xz
/tftpboot/                             zImage, am335x-boneblack.dtb cho U-Boot
```

### 11.3. Lệnh hay dùng
| Mục đích | Lệnh |
|---|---|
| Còn bao nhiêu đĩa | `df -h /` |
| Thư mục nào lớn | `du -sh ~/* \| sort -rh \| head` |
| Log kernel | `dmesg \| grep -i <tên>` |
| Device tree đang chạy | `ls /proc/device-tree/` |
| Đọc DTB | `dtc -I dtb -O dts file.dtb` |
| Tham số boot | `cat /proc/cmdline` |
| Trạng thái service | `systemctl status <tên>` |
| Theo dõi log build | `tail -f file.log` |

### 11.4. Việc nên làm tiếp
- Đưa `meta-iris` và các thay đổi kernel vào **git** (`git init`, `git add`, `git commit`).
- Lấy source app bằng `SRC_URI = "git://…"` thay vì copy tay.
- Driver CAN **MCP2515** trên SPI1 (kernel đã có `CONFIG_CAN_MCP251X=y`), thử `can0` ở chế độ loopback.
- Hello driver chưa có trong Yocto: patch `0001-bbb-st7789-lcd.patch` chỉ gồm DTS + `fb_st7789v.c`. Muốn đưa vào image, viết recipe `hello-led-mod_1.0.bb` với `inherit module` (build ngoài cây kernel như 5.2), thêm node `hello_led` vào patch DTS, rồi thêm gói vào `IMAGE_INSTALL`.
