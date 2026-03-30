gs_create_dymod(char_backend_socket)
if(TARGET asio::asio)
    target_link_libraries(char_backend_socket PRIVATE asio::asio)
endif()
