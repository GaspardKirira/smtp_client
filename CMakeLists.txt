cmake_minimum_required(VERSION 3.20)
project(smtp_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SMTP_CLIENT_ENABLE_OPENSSL "Enable STARTTLS support via OpenSSL" OFF)

add_library(smtp_client INTERFACE)
add_library(smtp_client::smtp_client ALIAS smtp_client)

target_include_directories(smtp_client INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(MSVC)
  target_compile_options(smtp_client INTERFACE /W4 /permissive-)
else()
  target_compile_options(smtp_client INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Optional TLS support
if(SMTP_CLIENT_ENABLE_OPENSSL)

  find_package(OpenSSL REQUIRED)

  target_compile_definitions(smtp_client INTERFACE
    SMTP_CLIENT_ENABLE_OPENSSL
  )

  target_link_libraries(smtp_client INTERFACE
    OpenSSL::SSL
    OpenSSL::Crypto
  )

endif()

include(CTest)
enable_testing()

add_executable(smtp_client_basic_test tests/test_basic.cpp)

target_link_libraries(
  smtp_client_basic_test
  PRIVATE smtp_client::smtp_client
)

add_test(NAME smtp_client.basic COMMAND smtp_client_basic_test)
