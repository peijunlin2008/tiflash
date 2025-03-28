# Copyright 2023 PingCAP, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

option (USE_INTERNAL_POCO_LIBRARY "Set to FALSE to use system poco library instead of bundled" ${NOT_UNBUNDLED})

if (NOT EXISTS "${TiFlash_SOURCE_DIR}/contrib/poco/CMakeLists.txt")
   if (USE_INTERNAL_POCO_LIBRARY)
      message (WARNING "submodule contrib/poco is missing. to fix try run: \n git submodule update --init --recursive")
   endif ()
   set (USE_INTERNAL_POCO_LIBRARY 0)
   set (MISSING_INTERNAL_POCO_LIBRARY 1)
endif ()

if (NOT USE_INTERNAL_POCO_LIBRARY)
    find_package (Poco COMPONENTS Net NetSSL SQL Data Crypto DataODBC MongoDB)
endif ()

if (Poco_INCLUDE_DIRS AND Poco_Foundation_LIBRARY)
elseif (NOT MISSING_INTERNAL_POCO_LIBRARY)

    set (USE_INTERNAL_POCO_LIBRARY 1)

    set (ENABLE_ZIP 0 CACHE BOOL "")
    set (ENABLE_PAGECOMPILER 0 CACHE BOOL "")
    set (ENABLE_PAGECOMPILER_FILE2PAGE 0 CACHE BOOL "")
    set (ENABLE_REDIS 0 CACHE BOOL "")
    set (ENABLE_DATA_SQLITE 0 CACHE BOOL "")
    set (ENABLE_DATA_MYSQL 0 CACHE BOOL "")
    set (ENABLE_DATA_POSTGRESQL 0 CACHE BOOL "")
    set (ENABLE_DATA_ODBC 0 CACHE BOOL "")
    # new after 2.0.0:
    set (POCO_ENABLE_ZIP 0 CACHE BOOL "")
    set (POCO_ENABLE_PAGECOMPILER 0 CACHE BOOL "")
    set (POCO_ENABLE_PAGECOMPILER_FILE2PAGE 0 CACHE BOOL "")
    set (POCO_ENABLE_REDIS 0 CACHE BOOL "")
    set (POCO_ENABLE_SQL_SQLITE 0 CACHE BOOL "")
    set (POCO_ENABLE_SQL_MYSQL 0 CACHE BOOL "")
    set (POCO_ENABLE_SQL_POSTGRESQL 0 CACHE BOOL "")

    set (POCO_UNBUNDLED 1 CACHE BOOL "")
    set (POCO_UNBUNDLED_PCRE 0 CACHE BOOL "")
    set (POCO_UNBUNDLED_EXPAT 0 CACHE BOOL "")
    set (POCO_STATIC ${MAKE_STATIC_LIBRARIES} CACHE BOOL "")
    set (POCO_VERBOSE_MESSAGES 1 CACHE BOOL "")


    # used in internal compiler
    list (APPEND Poco_INCLUDE_DIRS
        "${TiFlash_SOURCE_DIR}/contrib/poco/Foundation/include/"
        "${TiFlash_SOURCE_DIR}/contrib/poco/Util/include/"
    )

    if (NOT DEFINED POCO_ENABLE_MONGODB OR POCO_ENABLE_MONGODB)
        set (Poco_MongoDB_FOUND 1)
        set (Poco_MongoDB_LIBRARY PocoMongoDB)
        set (Poco_MongoDB_INCLUDE_DIRS "${TiFlash_SOURCE_DIR}/contrib/poco/MongoDB/include/")
    endif ()

    set (Poco_Data_FOUND 1)
    set (Poco_Data_INCLUDE_DIRS "${TiFlash_SOURCE_DIR}/contrib/poco/Data/include")
    set (Poco_Data_LIBRARY PocoData)

    if (OPENSSL_FOUND)
        set (Poco_NetSSL_FOUND 1)
        set (Poco_NetSSL_LIBRARY PocoNetSSL)
        set (Poco_Crypto_LIBRARY PocoCrypto)
    endif ()

    if (USE_STATIC_LIBRARIES AND USE_INTERNAL_ZLIB_LIBRARY)
        list (APPEND Poco_INCLUDE_DIRS
            "${TiFlash_SOURCE_DIR}/contrib/${INTERNAL_ZLIB_NAME}/"
            "${TiFlash_BINARY_DIR}/contrib/${INTERNAL_ZLIB_NAME}/"
        )
    endif ()

    set (Poco_Foundation_LIBRARY PocoFoundation)
    set (Poco_Util_LIBRARY PocoUtil)
    set (Poco_Net_LIBRARY PocoNet)
endif ()

message(STATUS "Using Poco: ${Poco_INCLUDE_DIRS} : ${Poco_Foundation_LIBRARY},${Poco_Util_LIBRARY},${Poco_Net_LIBRARY},${Poco_NetSSL_LIBRARY},${Poco_Data_LIBRARY},${Poco_DataODBC_LIBRARY},${Poco_MongoDB_LIBRARY}; MongoDB=${Poco_MongoDB_FOUND}, DataODBC=${Poco_DataODBC_FOUND}, NetSSL=${Poco_NetSSL_FOUND}")

# How to make sutable poco:
# use branch:
#  develop  OR  poco-1.7.9-release + 6a49c94d18c654d7a20b8c8ea47071b1fdd4813b
# and merge:
# ClickHouse-Extras/clickhouse_unbundled
# ClickHouse-Extras/clickhouse_unbundled_zlib
# ClickHouse-Extras/clickhouse_task
# ClickHouse-Extras/clickhouse_misc
# ClickHouse-Extras/clickhouse_anl
# ClickHouse-Extras/clickhouse_http_header https://github.com/pocoproject/poco/pull/1574
# ClickHouse-Extras/clickhouse_socket
# ClickHouse-Extras/clickhouse_warning
# ClickHouse-Extras/clickhouse-purge-logs-on-no-space
# ClickHouse-Extras/clickhouse_freebsd
# ClickHouse-Extras/clikhouse_no_zlib
# ClickHouse-Extras/clickhouse-fix-atomic
