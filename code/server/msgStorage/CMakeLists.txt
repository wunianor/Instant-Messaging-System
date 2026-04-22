cmake_minimum_required(VERSION 3.10)

# 项目名称
project(msgStorageServer)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)

# 设置编译目标
set(target "msgStorageServer")
set(test_target "msgStorage_integration_test")

set(proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../proto)
set(proto_files base.proto user.proto file.proto msgStorage.proto)

set(odb_path ${CMAKE_CURRENT_SOURCE_DIR}/../odb)
set(odb_files message.hxx)

set(proto_src_files "")
set(proto_src_file "")

# 遍历proto文件列表，生成对应的C++源文件
foreach(proto_file ${proto_files})

    # 生成对应的.cc源文件名
    string(REPLACE ".proto" ".pb.cc" proto_src_file ${proto_file})

    # 检查目标.cc源文件是否存在，若不存在则添加自定义命令生成
    if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${proto_src_file})
        add_custom_command(
            PRE_BUILD
            COMMAND protoc
            ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR} -I ${proto_path} ${proto_path}/${proto_file}
            DEPENDS ${proto_path}/${proto_file}
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${proto_src_file}
            COMMENT "生成的protobuf框架的.cc文件:" ${CMAKE_CURRENT_BINARY_DIR}/${proto_src_file}
        )
    endif()

    # 将生成的源文件添加到.cc源文件列表中
    list(APPEND proto_src_files ${CMAKE_CURRENT_BINARY_DIR}/${proto_src_file})
endforeach()

set(odb_src_files "")
set(odb_src_file "")

# 遍历odb文件列表，生成对应的C++源文件
foreach(odb_file ${odb_files})

    # 生成对应的.cxx源文件名
    string(REPLACE ".hxx" "-odb.cxx" odb_src_file ${odb_file})

    # 检查目标.cxx源文件是否存在，若不存在则添加自定义命令生成
    if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${odb_src_file})
        add_custom_command(
            PRE_BUILD
            COMMAND odb
            ARGS -d mysql --std c++${CMAKE_CXX_STANDARD} --generate-query --generate-schema --profile boost/date-time ${odb_path}/${odb_file}
            DEPENDS ${odb_path}/${odb_file}
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${odb_src_file}
            COMMENT "生成的odb框架的.cxx文件:" ${CMAKE_CURRENT_BINARY_DIR}/${odb_src_file}
        )
    endif()

    # 将生成的源文件添加到.cxx源文件列表中
    list(APPEND odb_src_files ${CMAKE_CURRENT_BINARY_DIR}/${odb_src_file})
endforeach()

# 设置链接库
set(link_libs
    -lbrpc -lgflags -lspdlog -lssl -lcrypto
    -lprotobuf -lleveldb -letcd-cpp-api -lcpprest -lcurl
    -lodb-mysql -lodb -lodb-boost
    -lboost_date_time
    -L/usr/local/lib -lelasticlient -l:libjsoncpp.so.19
    -lamqpcpp -lev -lpthread -ldl
)

# 生成${target}可执行文件
set(src_files "")
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src src_files)
add_executable(${target} ${src_files} ${proto_src_files} ${odb_src_files})
target_link_libraries(${target} ${link_libs})

# 集成测试（依赖真实 MySQL / ES / 已启动的 msgStorage 与 user/file 服务）
set(test_files "")
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/test test_files)
add_executable(${test_target} ${test_files} ${proto_src_files})
target_link_libraries(${test_target} ${link_libs} -lgtest)

# 设置头文件目录
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../odb/)

# 安装目标文件
INSTALL(TARGETS ${target} ${test_target} RUNTIME DESTINATION bin)
