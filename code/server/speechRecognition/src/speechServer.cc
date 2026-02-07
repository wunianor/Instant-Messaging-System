#include <gflags/gflags.h>

#include "speechServer.hpp"

using namespace imserver;


//日志输出相关参数
DEFINE_bool(isRelease,false,"日志模式是否为发布模式,true为是");
DEFINE_string(logFile,"log.txt","发布模式下日志的输出位置");
DEFINE_int32(logLevel,2,"发布模式下日志的最低输出等级");

//百度语音识别SDK相关参数
DEFINE_string(appId,"122000923","百度语音识别appId");
DEFINE_string(apiKey,"mfNbpeifugvcIQtUGmmJ1RWj","百度语音识别apiKey");
DEFINE_string(secretKey,"LW53h477eh6CWRLXDBhMqLGyQqx5UqcP","百度语音识别secretKey");

//rpc服务器相关参数
DEFINE_int32(instancePort,10000,"rpc服务器实例的port");
DEFINE_int32(timeout_sec,-1,"rpc服务器超时时间");
DEFINE_int32(num_threads,2,"线程数量");

//服务注册相关参数
DEFINE_string(serviceRegistryAddr,"127.0.0.1:2379","服务注册发现地址");
DEFINE_int32(ttl,3,"没有keepAlive服务的生存时间");
DEFINE_string(baseDir,"/service/","服务根目录");
DEFINE_string(instanceName,"SpeechRecognition/instance1","服务名/实例名称");
DEFINE_string(instanceIp,"127.0.0.1","实例的ip地址");


int main(int argc,char **argv)
{
    google::ParseCommandLineFlags(&argc, &argv, true);

    //初始化全局日志输出器
    initGlogger(FLAGS_isRelease,FLAGS_logFile,(spdlog::level::level_enum)FLAGS_logLevel);

    //创建并启动语音识别服务器
    SpeechRecognitionServerBuilder ssb;
    ssb.makeASRClientObj(FLAGS_appId,FLAGS_apiKey,FLAGS_secretKey);
    ssb.makeRpcServer(FLAGS_instancePort,FLAGS_timeout_sec,FLAGS_num_threads);
    ssb.makeServiceRegisterObj(FLAGS_serviceRegistryAddr,FLAGS_ttl,FLAGS_baseDir+FLAGS_instanceName,FLAGS_instanceIp+":"+std::to_string(FLAGS_instancePort));
    std::shared_ptr<SpeechRecognitionServer> server=ssb.NewServer();
    server->start();

    return 0;
}