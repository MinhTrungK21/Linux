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
