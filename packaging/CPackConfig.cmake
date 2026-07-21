# Packaging / install rules for ImIRC
# Included from the top-level CMakeLists.txt after targets exist.

if(NOT DEFINED IMIRC_VERSION)
	set(IMIRC_VERSION "${PROJECT_VERSION}")
endif()
if(NOT IMIRC_VERSION)
	set(IMIRC_VERSION "0.1.0")
endif()

include(GNUInstallDirs)

# App lives under /opt; PATH entry + desktop metadata stay on the FHS usr tree.
set(IMIRC_APP_DIR "/opt/imirc")

install(TARGETS client
	RUNTIME DESTINATION ${IMIRC_APP_DIR}
	COMPONENT Runtime
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/client/src/fonts/"
	DESTINATION ${IMIRC_APP_DIR}/fonts
	COMPONENT Runtime
	OPTIONAL
)

install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
	DESTINATION ${IMIRC_APP_DIR}
	COMPONENT Runtime
)

install(FILES "${CMAKE_SOURCE_DIR}/packaging/imirc.desktop"
	DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
	COMPONENT Runtime
)

install(FILES "${CMAKE_SOURCE_DIR}/packaging/imirc.png"
	DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
	COMPONENT Runtime
)

# /usr/bin/imirc → /opt/imirc/imirc (absolute so it is independent of install prefix)
install(CODE "
	set(_bindir \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
	file(MAKE_DIRECTORY \"\${_bindir}\")
	set(_link \"\${_bindir}/imirc\")
	set(_target \"${IMIRC_APP_DIR}/imirc\")
	if(EXISTS \"\${_link}\" OR IS_SYMLINK \"\${_link}\")
		file(REMOVE \"\${_link}\")
	endif()
	execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"\${_target}\" \"\${_link}\")
" COMPONENT Runtime)

# ---------------------------------------------------------------------------
# CPack: deb / rpm  (portable tar.gz + AppImage are built by scripts/package.sh)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "imirc")
set(CPACK_PACKAGE_VENDOR "ImIRC")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight IRC client built with Dear ImGui")
set(CPACK_PACKAGE_DESCRIPTION "ImIRC is a desktop IRC client using Dear ImGui, GLFW, and Boost.Asio.")
set(CPACK_PACKAGE_VERSION "${IMIRC_VERSION}")
set(CPACK_PACKAGE_CONTACT "imirc@localhost")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/imirc/imirc")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_VERBATIM_VARIABLES TRUE)
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_PACKAGE_EXECUTABLES "imirc;ImIRC")
# Prefix for relative install() destinations (desktop/icons/bin symlink).
# The app itself uses absolute ${IMIRC_APP_DIR} (/opt/imirc).
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# DEB
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgl1 | libgl1-mesa-glx, libx11-6")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# RPM
set(CPACK_RPM_PACKAGE_LICENSE "Unlicense")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_REQUIRES "libX11, mesa-libGL")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

set(CPACK_GENERATOR "DEB")
if(EXISTS "/usr/bin/rpmbuild" OR EXISTS "/bin/rpmbuild")
	list(APPEND CPACK_GENERATOR "RPM")
endif()

include(CPack)
