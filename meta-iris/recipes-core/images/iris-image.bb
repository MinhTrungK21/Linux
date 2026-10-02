SUMMARY = "Iris image: TI base image + dashboard tren LCD ST7789"

# lay toan bo noi dung cua image co san cua Arago
require recipes-core/images/tisdk-base-image.bb

# them app Qt (kem service tu chay) va resize-rootfs (mo rong rootfs cho het the SD)
IMAGE_INSTALL += "ok-dashboard resize-rootfs"

# ten file output: iris-image-am335x-evm.wic.xz ...
export IMAGE_BASENAME = "iris-image"

# bo psplash (logo TI luc boot): no ve len /dev/fb0 va de len dashboard
IMAGE_FEATURES:remove = "splash"

# BBB khong co NAND -> bo ubi/ubifs; chi can tar.xz (NFS) va wic (the SD)
IMAGE_FSTYPES = "tar.xz wic.xz wic.bmap"
