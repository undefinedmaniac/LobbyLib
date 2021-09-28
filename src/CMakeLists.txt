set(HEADER_LIST "${GameLobby_SOURCE_DIR}/include/LobbyServer.h" "${GameLobby_SOURCE_DIR}/include/LobbyLib/LobbyLib.h")

add_library(lobby_lib LobbyServer.cpp LobbyLib.cpp ${HEADER_LIST})

target_include_directories(lobby_lib PUBLIC ../include)

if (WIN32)
  target_link_libraries(lobby_lib PUBLIC ws2_32 wsock32)
endif()
target_link_libraries(lobby_lib PUBLIC Boost::boost)