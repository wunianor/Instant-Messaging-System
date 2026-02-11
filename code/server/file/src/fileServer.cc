#include <gflags/gflags.h>

#include "fileServer.hpp"

using namespace imserver;


//日志输出相关参数
DEFINE_bool(isRelease,false,"日志模式是否为发布模式,true为是");
DEFINE_string(logFile,"log.txt","发布模式下日志的输出位置");
DEFINE_int32(logLevel,2,"发布模式下日志的最低输出等级");

//rpc服务器相关参数
DEFINE_int32(instancePort,10001,"rpc服务器实例的port");
DEFINE_int32(timeout_sec,-1,"rpc服务器超时时间");
DEFINE_int32(num_threads,2,"线程数量");

//rpc服务相关参数
DEFINE_string(fileBaseDir,"./data/","文件系统根目录,存储文件");

//服务注册相关参数
DEFINE_string(serviceRegistryAddr,"127.0.0.1:2379","服务注册发现地址");
DEFINE_int32(ttl,3,"没有keepAlive服务的生存时间");
DEFINE_string(baseDir,"/service/","服务根目录");
DEFINE_string(instanceName,"file/instance1","服务名/实例名称");
DEFINE_string(instanceIp,"127.0.0.1","实例的ip地址");


int main(int argc,char **argv)
{
    google::ParseCommandLineFlags(&argc, &argv, true);

    //初始化全局日志输出器
    initGlogger(FLAGS_isRelease,FLAGS_logFile,(spdlog::level::level_enum)FLAGS_logLevel);

    //创建并启动文件服务器
    FileServerBuilder fsb;
    fsb.makeRpcServer(FLAGS_instancePort,FLAGS_timeout_sec,FLAGS_num_threads,FLAGS_fileBaseDir);//其实这一步是真正的启动服务器
    fsb.makeServiceRegisterObj(FLAGS_serviceRegistryAddr,FLAGS_ttl,FLAGS_baseDir+FLAGS_instanceName,FLAGS_instanceIp+":"+std::to_string(FLAGS_instancePort));
    std::shared_ptr<FileServer> server=fsb.NewServer();
    server->start();

    return 0;
}