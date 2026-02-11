#include <gflags/gflags.h>

#include "userServer.hpp"

using namespace imserver;


//日志输出相关参数
DEFINE_bool(isRelease,false,"日志模式是否为发布模式,true为是");
DEFINE_string(logFile,"log.txt","发布模式下日志的输出位置");
DEFINE_int32(logLevel,2,"发布模式下日志的最低输出等级");

//redis客户端相关参数
DEFINE_string(redis_host,"127.0.0.1","redis服务器ip地址");
DEFINE_int32(redis_port,6379,"redis服务器端口");
DEFINE_int32(redis_db,0,"redis连接的数据库");
DEFINE_bool(redis_keepAlive,true,"redis连接是否开启长连接保活");

//MySQL客户端相关参数
DEFINE_string(mysql_user,"root","mysql连接的用户名");
DEFINE_string(mysql_password,"123456","mysql连接的密码");
DEFINE_string(mysql_host,"127.0.0.1","mysql连接的主机地址");
DEFINE_string(mysql_db,"im","mysql连接的数据库名称");
DEFINE_string(mysql_characterSet,"utf8","mysql连接的字符集");
DEFINE_int32(mysql_port,3306,"mysql连接的端口");
DEFINE_int32(mysql_connectionPoolCnt,1,"mysql连接池的连接数量");

//ES客户端相关参数
DEFINE_string(es_hostUrl,"http://127.0.0.1:9200/","ES服务器地址,格式为http://ip:port/");

//rpc服务器相关参数
DEFINE_int32(instancePort,10002,"rpc服务器实例的port");
DEFINE_int32(timeout_sec,-1,"rpc服务器超时时间");
DEFINE_int32(num_threads,2,"线程数量");

//服务注册与发现相关参数
DEFINE_string(serviceRegistryAddr,"127.0.0.1:2379","服务注册发现地址");
DEFINE_string(baseDir,"/service/","服务根目录");
DEFINE_string(fileServiceName,"file","文件服务名称(不带实例名,格式例如file/,file)");
DEFINE_int32(ttl,3,"没有keepAlive服务的生存时间");
DEFINE_string(instanceName,"user/instance1","格式:服务名/实例名称");
DEFINE_string(instanceIp,"127.0.0.1","实例的ip地址");


int main(int argc,char **argv)
{
    google::ParseCommandLineFlags(&argc, &argv, true);

    //初始化全局日志输出器
    initGlogger(FLAGS_isRelease,FLAGS_logFile,(spdlog::level::level_enum)FLAGS_logLevel);

    //创建并启动用户服务器
    UserServerBuilder usb;
    usb.makeRedisClient(FLAGS_redis_host,FLAGS_redis_port,FLAGS_redis_db,FLAGS_redis_keepAlive);
    usb.makeMysqlClient(FLAGS_mysql_user, FLAGS_mysql_password, FLAGS_mysql_host, FLAGS_mysql_db, FLAGS_mysql_characterSet, FLAGS_mysql_port, FLAGS_mysql_connectionPoolCnt);
    usb.makeESClient({FLAGS_es_hostUrl});
    usb.makeRpcsmAndSdObj(FLAGS_serviceRegistryAddr, FLAGS_baseDir, FLAGS_fileServiceName);
    usb.makeRpcServer(FLAGS_instancePort, FLAGS_timeout_sec, FLAGS_num_threads);
    usb.makeServiceRegisterObj(FLAGS_serviceRegistryAddr, FLAGS_ttl, FLAGS_baseDir+FLAGS_instanceName, FLAGS_instanceIp+":"+std::to_string(FLAGS_instancePort));
    std::shared_ptr<UserServer> server=usb.NewServer();
    server->start();

    return 0;
}