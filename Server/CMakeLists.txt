cmake_minimum_required(VERSION 3.21)
project(PatternBreakerServer LANGUAGES CXX)

find_package(Drogon CONFIG REQUIRED)
find_package(unofficial-argon2 CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_executable(PatternBreakerServer
    src/main.cpp
    src/Registration.cpp
    src/JwtTokens.cpp
    src/RaidSessions.cpp
    src/RaidRoutes.cpp
    src/RankingRoutes.cpp
)
set_target_properties(PatternBreakerServer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_link_libraries(PatternBreakerServer PRIVATE
    Drogon::Drogon
    unofficial::argon2::libargon2
    jwt-cpp::jwt-cpp
    OpenSSL::Crypto
)

# On Windows, vcpkg installs the MySQL 8 authentication plugin separately from libmariadb.dll.
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(mariadb_package_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    set(mysql_auth_plugin "plugins/libmariadb/caching_sha2_password.dll")
    if(EXISTS "${mariadb_package_dir}/${mysql_auth_plugin}")
        add_custom_command(TARGET PatternBreakerServer POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${mariadb_package_dir}$<$<CONFIG:Debug>:/debug>/${mysql_auth_plugin}"
                "$<TARGET_FILE_DIR:PatternBreakerServer>"
            VERBATIM
        )
    endif()
endif()
