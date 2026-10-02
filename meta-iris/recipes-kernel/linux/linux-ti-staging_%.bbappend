# tim file:// trong thu muc files/ cua layer nay truoc
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://0001-bbb-st7789-lcd.patch \
    file://st7789.cfg \
"

# meta-ti gop fragment nay vao multi_v7_defconfig o buoc do_configure
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/st7789.cfg"
