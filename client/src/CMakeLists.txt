set(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/application.h
    ${CMAKE_CURRENT_SOURCE_DIR}/irc_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/irc_connection.h
)
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/application.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/irc_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/irc_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SOURCES})
add_executable(client ${HEADERS} ${SOURCES} ${imguiSrc})

set_target_properties(client PROPERTIES OUTPUT_NAME imirc)

target_link_libraries(client
    ${OPENGL_LIBRARIES}
    Boost::system
    Boost::thread
    Boost::log
    Boost::log_setup
    Boost::date_time
    Boost::program_options
    OpenSSL::SSL
    OpenSSL::Crypto
    glad
    glfw
    util
)

if(WIN32)
	# OpenSSL static + Winsock / crypto helpers; GLFW needs GDI/user32.
	target_link_libraries(client
		opengl32
		gdi32
		user32
		shell32
		ole32
		uuid
		ws2_32
		crypt32
		bcrypt
		advapi32
	)
endif()

# Fully static MinGW runtime so the portable zip needs no libgcc/libstdc++ DLLs.
if(MINGW)
	target_link_options(client PRIVATE
		-static
		-static-libgcc
		-static-libstdc++
	)
endif()

add_custom_command(TARGET client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:client>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts
        $<TARGET_FILE_DIR:client>/fonts
    COMMENT "Copying fonts next to client binary"
)
