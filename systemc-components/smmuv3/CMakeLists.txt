gs_create_dymod(smmuv3)

target_link_libraries(smmuv3 PUBLIC
    gs_memory
    router
    reg_router
)
