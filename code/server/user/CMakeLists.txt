
cmake_minimum_required(VERSION 3.10)

#项目名称
project(userServer)

#设置C++标准
set(CMAKE_CXX_STANDARD 17)


#--------------------------------------------------------------------------------------------
#常规配置区

#设置编译目标
set(target "userServer") #服务端
set(test_client "test_client") #测试客户端

#设置链接库
# elasticlient 依赖 /usr/local 下的 libjsoncpp.so.19；勿使用 -ljsoncpp（常解析为系统 libjsoncpp.so.1），否则 ld 报版本冲突
set(link_libs -lbrpc -lgflags
    -lspdlog -lssl -lcrypto
    -lprotobuf -lleveldb -letcd-cpp-api
    -lcpprest -lcurl -lodb-mysql -lodb -lodb-boost
    -L/usr/local/lib -lcpr -lelasticlient -l:libjsoncpp.so.19
    -lhiredis -lredis++ -lpthread -ldl)

set(proto_path ${CMAKE_CURRENT_SOURCE_DIR}/../proto) #proto文件路径
set(proto_files base.proto user.proto file.proto) #proto文件列表

set(odb_path ${CMAKE_CURRENT_SOURCE_DIR}/../odb) #odb文件路径
set(odb_files user.hxx)

#设置头文件目录
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../odb/)



#--------------------------------------------------------------------------------------------


set(proto_src_files "") #存放protoc生成的.cc源文件列表
set(proto_src_file "") #单个protoc生成的.cc源文件

#遍历proto文件列表，生成对应的C++源文件
foreach(proto_file ${proto_files})

    #生成对应的.cc源文件名
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

    #将生成的源文件添加到.cc源文件列表中
    list(APPEND proto_src_files ${CMAKE_CURRENT_BINARY_DIR}/${proto_src_file})
endforeach()

set(odb_src_files "") #存放odb生成的.cxx源文件列表
set(odb_src_file "") #单个odb生成的.cxx源文件

#遍历odb文件列表，生成对应的C++源文件
foreach(odb_file ${odb_files})

    #生成对应的.cc源文件名
    string(REPLACE ".hxx" "-odb.cxx" odb_src_file ${odb_file})

    # 检查目标.cc源文件是否存在，若不存在则添加自定义命令生成
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

    #将生成的源文件添加到.cc源文件列表中
    list(APPEND odb_src_files ${CMAKE_CURRENT_BINARY_DIR}/${odb_src_file})
endforeach()

#生成${target}可执行文件
set(src_files "") 
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src src_files) #获取src目录下的所有源文件
add_executable(${target} ${src_files} ${proto_src_files} ${odb_src_files})  #添加可执行文件
target_link_libraries(${target} ${link_libs}) #链接库

#生成${test_client}可执行文件
set(test_files "")
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/test test_files) #获取test目录下的所有源文件
add_executable(${test_client} ${test_files} ${proto_src_files}) #添加测试客户端可执行文件
target_link_libraries(${test_client} ${link_libs} -lgtest) #链接库


#安装目标文件
INSTALL(TARGETS ${target} ${test_client} RUNTIME DESTINATION bin)
#INSTALL(TARGETS ${target}  RUNTIME DESTINATION bin)

