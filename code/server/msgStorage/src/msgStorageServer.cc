#include <gflags/gflags.h>

#include "msgStorageServer.hpp"

using namespace imserver;

// 日志输出相关参数
DEFINE_bool(isRelease, false, "日志模式是否为发布模式,true为是");
DEFINE_string(logFile, "log.txt", "发布模式下日志的输出位置");
DEFINE_int32(logLevel, 2, "发布模式下日志的最低输出等级");

// MySQL客户端相关参数
DEFINE_string(mysql_user, "root", "mysql连接的用户名");
DEFINE_string(mysql_password, "123456", "mysql连接的密码");
DEFINE_string(mysql_host, "127.0.0.1", "mysql连接的主机地址");
DEFINE_string(mysql_db, "im", "mysql连接的数据库名称");
DEFINE_string(mysql_characterSet, "utf8", "mysql连接的字符集");
DEFINE_int32(mysql_port, 3306, "mysql连接的端口");
DEFINE_int32(mysql_connectionPoolCnt, 1, "mysql连接池的连接数量");

// ES客户端相关参数
DEFINE_string(es_host, "http://127.0.0.1:9200/", "Elasticsearch主机URL");

// rabbitMQ客户端和交换机,队列相关参数
DEFINE_string(rabbitMQ_user, "root", "rabbitMQ用户名");
DEFINE_string(rabbitMQ_password, "123456", "rabbitMQ用户密码");
DEFINE_string(rabbitMQ_host, "127.0.0.1", "rabbitMQ服务端主机ip地址");
DEFINE_int32(rabbitMQ_port, 5672, "rabbitMQ服务端端口");
DEFINE_string(rabbitMQ_exchangeName, "message_exchange", "rabbitMQ消息转发交换机名字");
DEFINE_string(rabbitMQ_queueName, "message_queue", "rabbitMQ消息持久化队列名字");
DEFINE_string(rabbitMQ_routingKey, "message_queue", "rabbitMQ消息持久化队列对应的routingKey");

// rpc服务器相关参数
DEFINE_int32(instancePort, 10004, "rpc服务器实例的port");
DEFINE_int32(timeout_sec, -1, "rpc服务器超时时间");
DEFINE_int32(num_threads, 2, "线程数量");

// 服务注册与发现相关参数
DEFINE_string(serviceRegistryAddr, "127.0.0.1:2379", "服务注册发现地址");
DEFINE_string(baseDir, "/service/", "服务根目录");
DEFINE_string(userServiceName, "user", "用户服务名称(不带实例名,格式例如\"user\")");
DEFINE_string(fileServiceName, "file", "文件服务名称(不带实例名,格式例如\"file\")");
DEFINE_int32(ttl, 3, "取消keepAlive后服务的生存时间");
DEFINE_string(instanceName, "msgStorage/instance1", "格式:服务名/实例名称");
DEFINE_string(instanceIp, "127.0.0.1", "实例的ip地址");

/// @brief 消息存储服务进程入口，负责初始化依赖并启动服务
/// @param argc 命令行参数个数
/// @param argv 命令行参数数组
/// @return 进程退出码
int main(int argc, char **argv)
{
    google::ParseCommandLineFlags(&argc, &argv, true);

    initGlogger(FLAGS_isRelease, FLAGS_logFile, (spdlog::level::level_enum)FLAGS_logLevel);

    MsgStorageServerBuilder mssb;
    mssb.makeMysqlClient(
        FLAGS_mysql_user,
        FLAGS_mysql_password,
        FLAGS_mysql_host,
        FLAGS_mysql_db,
        FLAGS_mysql_characterSet,
        FLAGS_mysql_port,
        FLAGS_mysql_connectionPoolCnt);
    mssb.makeEsClient({FLAGS_es_host});
    mssb.makeRMQClient(
        FLAGS_rabbitMQ_user,
        FLAGS_rabbitMQ_password,
        FLAGS_rabbitMQ_host,
        FLAGS_rabbitMQ_port,
        FLAGS_rabbitMQ_exchangeName,
        FLAGS_rabbitMQ_queueName,
        FLAGS_rabbitMQ_routingKey);
    mssb.makeRpcsmAndSdObj(
        FLAGS_serviceRegistryAddr,
        FLAGS_baseDir,
        FLAGS_userServiceName,
        FLAGS_fileServiceName);
    mssb.makeRpcServer(FLAGS_instancePort, FLAGS_timeout_sec, FLAGS_num_threads);
    mssb.makeServiceRegisterObj(
        FLAGS_serviceRegistryAddr,
        FLAGS_ttl,
        FLAGS_baseDir + FLAGS_instanceName,
        FLAGS_instanceIp + ":" + std::to_string(FLAGS_instancePort));

    std::shared_ptr<MsgStorageServer> server = mssb.NewServer();
    server->start();

    return 0;
}
