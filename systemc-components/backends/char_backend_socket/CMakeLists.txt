gs_create_dymod(char_backend_socket)

if(TARGET asio::asio)
    target_link_libraries(char_backend_socket PUBLIC asio::asio)
else()
    find_path(ASIO_INCLUDE_DIR asio.hpp
        HINTS
            $ENV{HOMEBREW_PREFIX}/include
            /opt/homebrew/include
            /usr/local/include
    )
    if(ASIO_INCLUDE_DIR)
        target_include_directories(char_backend_socket PUBLIC ${ASIO_INCLUDE_DIR})
        target_compile_definitions(char_backend_socket PUBLIC ASIO_STANDALONE)
        target_link_libraries(char_backend_socket PUBLIC Threads::Threads)
    endif()
endif()
