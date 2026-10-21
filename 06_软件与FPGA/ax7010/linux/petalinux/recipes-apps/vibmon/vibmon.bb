# PetaLinux / Yocto recipe for vibmond, vibmonctl and carrier_test.
# Copy this folder to project-spec/meta-user/recipes-apps/vibmon/, create
# files/vibmon-src.tar.gz with ax7010/linux/vibmon/tools/make_src_tarball.sh
# and add "vibmon" to IMAGE_INSTALL (petalinux-config -c rootfs).
SUMMARY = "Vision vibration meter application (carrier board services, measurement, UI)"
LICENSE = "CLOSED"

SRC_URI = "file://vibmon-src.tar.gz \
           file://vibmon.init \
           file://vibmon.service \
          "
S = "${WORKDIR}/vibmon-src/ax7010/linux/vibmon"

inherit update-rc.d systemd
INITSCRIPT_NAME = "vibmon"
INITSCRIPT_PARAMS = "start 99 5 . stop 10 0 1 6 ."
SYSTEMD_SERVICE:${PN} = "vibmon.service"

EXTRA_OEMAKE = "BUILD=${B}/out CXX='${CXX}' CC='${CC}' AR='${AR}'"

do_compile() {
    oe_runmake -C ${S} all
}

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/vibmon ${D}${sysconfdir}/init.d ${D}${systemd_system_unitdir}
    install -m 0755 ${B}/out/vibmond ${B}/out/vibmonctl ${B}/out/carrier_test ${D}${bindir}/
    install -m 0644 ${S}/config/vibmon.json ${D}${sysconfdir}/vibmon/vibmon.json
    install -m 0644 ${S}/config/calibration.example.json ${D}${sysconfdir}/vibmon/calibration.example.json
    install -m 0755 ${WORKDIR}/vibmon.init ${D}${sysconfdir}/init.d/vibmon
    install -m 0644 ${WORKDIR}/vibmon.service ${D}${systemd_system_unitdir}/vibmon.service
}

CONFFILES:${PN} = "${sysconfdir}/vibmon/vibmon.json"
FILES:${PN} += "${systemd_system_unitdir}/vibmon.service"
