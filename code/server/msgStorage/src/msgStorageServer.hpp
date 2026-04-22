#pragma once

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <brpc/controller.h>
#include <brpc/server.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <odb/forward.hxx>

#include "base.pb.h"
#include "file.pb.h"
#include "msgStorage.pb.h"
#include "user.pb.h"

#include "data-es.hpp"
#include "etcd.hpp"
#include "log.hpp"
#include "message-mysql.hpp"
#include "mysql.hpp"
#include "rabbitMQ.hpp"
#include "rpcService.hpp"
#include "utils.hpp"

namespace imserver
{

class MsgStorageServiceImpl : public imserver::MessageStorageService
{
public:
    /// @brief 构造消息存储服务实现，并注册 RabbitMQ 消费回调
    /// @param userServiceDir 用户服务发现目录
    /// @param fileServiceDir 文件服务发现目录
    /// @param consumeQueueName 消费的消息队列名
    /// @param rpcsm RPC 服务管理器
    /// @param rmqClient RabbitMQ 客户端
    /// @param mysqlClient MySQL ODB 客户端
    /// @param esClient Elasticsearch 客户端
    MsgStorageServiceImpl(
        const std::string &userServiceDir,
        const std::string &fileServiceDir,
        const std::string &consumeQueueName,
        const std::shared_ptr<RPCServiceManager> &rpcsm,
        const std::shared_ptr<RMQClient> &rmqClient,
        const std::shared_ptr<odb::core::database> &mysqlClient,
        const std::shared_ptr<elasticlient::Client> &esClient):
        _userServiceDir(userServiceDir),
        _fileServiceDir(fileServiceDir),
        _rpcsm(rpcsm),
        _rmqClient(rmqClient),
        _messageTable(std::make_shared<MessageTable>(mysqlClient)),
        _esMessage(std::make_shared<ESMessage>(esClient))
    {
        _rmqClient->consume(consumeQueueName, [this](const char *data, uint64_t len)
        {
            this->onMessage(data, len);
        });
    }

    /// @brief 获取指定会话在时间区间内的历史消息
    /// @param controller brpc 控制器
    /// @param request 历史消息请求
    /// @param response 历史消息响应
    /// @param done RPC 收尾回调
    void GetHistoryMessage(::google::protobuf::RpcController *controller,
                           const ::imserver::GetHistoryMessageRequest *request,
                           ::imserver::GetHistoryMessageResponse *response,
                           ::google::protobuf::Closure *done) override
    {
        brpc::ClosureGuard rpcGuard(done);

        if (!checkRequester(request->request_id(), request->user_id(), request->session_id()))
        {
            setErrorResponse(request, response, "请求用户身份校验失败");
            return;
        }

        const boost::posix_time::ptime startTime =
            unixSecondsToServerLocalPtime(static_cast<std::time_t>(request->start_time()));
        const boost::posix_time::ptime overTime =
            unixSecondsToServerLocalPtime(static_cast<std::time_t>(request->over_time()));

        auto messages = _messageTable->range(request->chat_session_id(), startTime, overTime);
        std::unordered_map<std::string, UserInfo> userInfoMap;
        if (!fillUserInfoMap(request->request_id(), messages, userInfoMap))
        {
            setErrorResponse(request, response, "批量获取消息发送者信息失败");
            return;
        }

        fillSuccessResponse(request, response, messages, userInfoMap);
    }

    /// @brief 获取指定会话最近 N 条消息
    /// @param controller brpc 控制器
    /// @param request 最近消息请求
    /// @param response 最近消息响应
    /// @param done RPC 收尾回调
    void GetRecentMessage(::google::protobuf::RpcController *controller,
                          const ::imserver::GetRecentMessageRequest *request,
                          ::imserver::GetRecentMessageResponse *response,
                          ::google::protobuf::Closure *done) override
    {
        brpc::ClosureGuard rpcGuard(done);

        if (!checkRequester(request->request_id(), request->user_id(), request->session_id()))
        {
            setErrorResponse(request, response, "请求用户身份校验失败");
            return;
        }

        auto messages = _messageTable->recent(
            request->chat_session_id(),
            static_cast<size_t>(std::max<int64_t>(request->message_count(), 0)));
        // recent 按时间倒序查询,这里改成升序便于前端按时间线展示
        std::reverse(messages.begin(), messages.end());

        std::unordered_map<std::string, UserInfo> userInfoMap;
        if (!fillUserInfoMap(request->request_id(), messages, userInfoMap))
        {
            setErrorResponse(request, response, "批量获取消息发送者信息失败");
            return;
        }

        fillSuccessResponse(request, response, messages, userInfoMap);
    }

    /// @brief 按关键字搜索会话消息
    /// @param controller brpc 控制器
    /// @param request 消息搜索请求
    /// @param response 消息搜索响应
    /// @param done RPC 收尾回调
    void MessageSearch(::google::protobuf::RpcController *controller,
                       const ::imserver::MessageSearchRequest *request,
                       ::imserver::MessageSearchResponse *response,
                       ::google::protobuf::Closure *done) override
    {
        brpc::ClosureGuard rpcGuard(done);

        if (!checkRequester(request->request_id(), request->user_id(), request->session_id()))
        {
            setErrorResponse(request, response, "请求用户身份校验失败");
            return;
        }

        auto messages = _esMessage->search(request->search_key(), request->chat_session_id());
        std::unordered_map<std::string, UserInfo> userInfoMap;
        if (!fillUserInfoMap(request->request_id(), messages, userInfoMap))
        {
            setErrorResponse(request, response, "批量获取消息发送者信息失败");
            return;
        }

        fillSuccessResponse(request, response, messages, userInfoMap);
    }

private:
    template <class Request, class Response>
    /// @brief 统一设置 RPC 失败响应
    /// @tparam Request 请求类型
    /// @tparam Response 响应类型
    /// @param request RPC 请求
    /// @param response RPC 响应
    /// @param errMessage 错误信息
    void setErrorResponse(const Request *request, Response *response, const std::string &errMessage)
    {
        response->set_request_id(request->request_id());
        response->set_success(false);
        response->set_error_message(errMessage);
    }

    /// @brief 校验请求用户身份是否有效
    /// @param requestId 请求 ID
    /// @param userId 请求用户 ID
    /// @param sessionId 请求会话 ID
    /// @return 校验通过返回 true，否则返回 false
    bool checkRequester(
        const std::string &requestId,
        const std::string &userId,
        const std::string &sessionId)
    {
        if (userId.empty()) return true;

        auto channel = _rpcsm->getChannel(_userServiceDir);
        if (!channel)
        {
            LOG_ERROR("校验请求用户失败,用户服务不可用,request_id:{}", requestId);
            return false;
        }

        UserService_Stub stub(channel.get());
        brpc::Controller cntl;
        GetSingleUserInfoRequest req;
        GetSingleUserInfoResponse rsp;
        req.set_request_id(requestId);
        req.set_user_id(userId);
        req.set_session_id(sessionId);
        stub.GetSingleUserInfo(&cntl, &req, &rsp, nullptr);
        if (cntl.Failed() || !rsp.success())
        {
            LOG_ERROR("校验请求用户失败,request_id:{},user_id:{}", requestId, userId);
            return false;
        }
        return true;
    }

    /// @brief 根据消息列表批量获取发送者信息
    /// @param requestId 请求 ID
    /// @param messages 消息列表
    /// @param userInfoMap 输出的用户信息映射
    /// @return 获取成功返回 true，否则返回 false
    bool fillUserInfoMap(
        const std::string &requestId,
        const std::vector<imserver::Message> &messages,
        std::unordered_map<std::string, UserInfo> &userInfoMap)
    {
        userInfoMap.clear();
        if (messages.empty()) return true;

        std::unordered_set<std::string> userSet;
        for (const auto &message : messages)
        {
            userSet.insert(message.userId());
        }

        auto channel = _rpcsm->getChannel(_userServiceDir);
        if (!channel)
        {
            LOG_ERROR("批量获取用户信息失败,用户服务不可用,request_id:{}", requestId);
            return false;
        }

        UserService_Stub stub(channel.get());
        brpc::Controller cntl;
        GetMultiUserInfoRequest req;
        GetMultiUserInfoResponse rsp;
        req.set_request_id(requestId);
        for (const auto &userId : userSet)
        {
            req.add_user_id(userId);
        }
        stub.GetMultiUserInfo(&cntl, &req, &rsp, nullptr);
        if (cntl.Failed() || !rsp.success())
        {
            LOG_ERROR("批量获取用户信息失败,request_id:{}", requestId);
            return false;
        }

        for (const auto &item : rsp.user_info_map())
        {
            userInfoMap.insert(item);
        }
        return true;
    }

    template <class Request, class Response>
    /// @brief 将消息与发送者信息组装为成功响应
    /// @tparam Request 请求类型
    /// @tparam Response 响应类型
    /// @param request RPC 请求
    /// @param response RPC 响应
    /// @param messages 查询得到的消息
    /// @param userInfoMap 用户信息映射
    void fillSuccessResponse(
        const Request *request,
        Response *response,
        const std::vector<imserver::Message> &messages,
        const std::unordered_map<std::string, UserInfo> &userInfoMap)
    {
        response->set_request_id(request->request_id());
        response->set_success(true);
        for (const auto &message : messages)
        {
            auto *messageInfo = response->add_message_info();
            messageInfo->set_message_id(message.messageId());
            messageInfo->set_chat_session_id(message.chatSessionId());
            messageInfo->set_timestamp(
                static_cast<std::int64_t>(serverLocalPtimeToUnixSeconds(message.createTime())));

            const auto iter = userInfoMap.find(message.userId());
            if (iter != userInfoMap.end()) messageInfo->mutable_sender()->CopyFrom(iter->second);
            else messageInfo->mutable_sender()->set_user_id(message.userId());

            auto *messageContent = messageInfo->mutable_message_content();
            const auto messageType = static_cast<imserver::MessageType>(message.messageType());
            messageContent->set_message_type(messageType);
            switch (messageType)
            {
            case imserver::MessageType::STRING:
                messageContent->mutable_string_message()->set_content(message.content());
                break;
            case imserver::MessageType::IMAGE:
                messageContent->mutable_image_message()->set_file_id(message.fileId());
                break;
            case imserver::MessageType::FILE:
                messageContent->mutable_file_message()->set_file_id(message.fileId());
                messageContent->mutable_file_message()->set_file_name(message.fileName());
                messageContent->mutable_file_message()->set_file_size(message.fileSize());
                break;
            case imserver::MessageType::SPEECH:
                messageContent->mutable_speech_message()->set_file_id(message.fileId());
                break;
            default:
                break;
            }
        }
    }

    /// @brief 调用文件服务上传单个二进制文件
    /// @param requestId 请求 ID
    /// @param userId 用户 ID
    /// @param fileName 文件名
    /// @param binaryContent 文件二进制内容
    /// @param fileInfo 输出的文件元信息
    /// @return 上传成功返回 true，否则返回 false
    bool putSingleFile(
        const std::string &requestId,
        const std::string &userId,
        const std::string &fileName,
        const std::string &binaryContent,
        FileMessageInfo &fileInfo)
    {
        auto channel = _rpcsm->getChannel(_fileServiceDir);
        if (!channel)
        {
            LOG_ERROR("上传文件失败,文件服务不可用,request_id:{}", requestId);
            return false;
        }

        FileService_Stub stub(channel.get());
        brpc::Controller cntl;
        PutSingleFileRequest req;
        PutSingleFileResponse rsp;
        req.set_request_id(requestId);
        req.set_user_id(userId);
        req.mutable_file_data()->set_file_name(fileName);
        req.mutable_file_data()->set_file_size(static_cast<int64_t>(binaryContent.size()));
        req.mutable_file_data()->set_file_content(binaryContent);
        stub.PutSingleFile(&cntl, &req, &rsp, nullptr);
        if (cntl.Failed() || !rsp.success())
        {
            LOG_ERROR("上传文件失败,request_id:{},user_id:{}", requestId, userId);
            return false;
        }

        fileInfo = rsp.file_info();
        return true;
    }

    /// @brief RabbitMQ 消息消费回调，负责反序列化并持久化消息
    /// @param data 原始消息数据
    /// @param len 原始消息长度
    void onMessage(const char *data, uint64_t len)
    {
        MessageInfo incoming;
        if (!incoming.ParseFromArray(data, static_cast<int>(len)))
        {
            LOG_ERROR("反序列化消息队列数据失败");
            return;
        }

        const std::string requestId = uuid();
        const std::string messageId = incoming.message_id();
        const std::string chatSessionId = incoming.chat_session_id();
        const std::string userId = incoming.sender().user_id();
        const unsigned char messageType = static_cast<unsigned char>(incoming.message_content().message_type());
        const auto createTime =
            unixSecondsToServerLocalPtime(static_cast<std::time_t>(incoming.timestamp()));

        imserver::Message message(messageId, chatSessionId, userId, messageType, createTime);
        const auto &messageContent = incoming.message_content();
        switch (messageContent.message_type())
        {
        case imserver::MessageType::STRING:
            message.content(messageContent.string_message().content());
            _esMessage->add(
                messageId,
                userId,
                chatSessionId,
                incoming.timestamp(),
                message.content());
            break;
        case imserver::MessageType::IMAGE:
        {
            FileMessageInfo fileInfo;
            const std::string fileName = "image-" + messageId;
            if (!putSingleFile(requestId, userId, fileName, messageContent.image_message().image_content(), fileInfo))
            {
                return;
            }
            message.fileId(fileInfo.file_id());
            message.fileName(fileInfo.file_name());
            message.fileSize(static_cast<unsigned int>(fileInfo.file_size()));
            break;
        }
        case imserver::MessageType::FILE:
        {
            FileMessageInfo fileInfo;
            if (!putSingleFile(
                    requestId,
                    userId,
                    messageContent.file_message().file_name(),
                    messageContent.file_message().file_contents(),
                    fileInfo))
            {
                return;
            }
            message.fileId(fileInfo.file_id());
            message.fileName(fileInfo.file_name());
            message.fileSize(static_cast<unsigned int>(fileInfo.file_size()));
            break;
        }
        case imserver::MessageType::SPEECH:
        {
            FileMessageInfo fileInfo;
            const std::string fileName = "speech-" + messageId;
            if (!putSingleFile(requestId, userId, fileName, messageContent.speech_message().file_contents(), fileInfo))
            {
                return;
            }
            message.fileId(fileInfo.file_id());
            message.fileName(fileInfo.file_name());
            message.fileSize(static_cast<unsigned int>(fileInfo.file_size()));
            break;
        }
        default:
            LOG_WARN("未支持的消息类型,message_id:{},type:{}", messageId, messageType);
            return;
        }

        if (!_messageTable->insert(message))
        {
            LOG_ERROR("消息入库失败,message_id:{}", messageId);
            return;
        }

        LOG_INFO("消息持久化成功,message_id:{},chat_session_id:{}", messageId, chatSessionId);
    }

private:
    std::string _userServiceDir;
    std::string _fileServiceDir;

    std::shared_ptr<RPCServiceManager> _rpcsm;
    std::shared_ptr<RMQClient> _rmqClient;

    std::shared_ptr<MessageTable> _messageTable;
    std::shared_ptr<ESMessage> _esMessage;
};

class MsgStorageServer
{
public:
    /// @brief 构造消息存储服务器聚合对象
    /// @param mysqlClient MySQL ODB 客户端
    /// @param esClient ES 客户端
    /// @param rmqClient RabbitMQ 客户端
    /// @param sd 服务发现对象
    /// @param server brpc 服务器对象
    /// @param sr 服务注册对象
    MsgStorageServer(
        const std::shared_ptr<odb::core::database> &mysqlClient,
        const std::shared_ptr<elasticlient::Client> &esClient,
        const std::shared_ptr<RMQClient> &rmqClient,
        const std::shared_ptr<ServiceDiscover> &sd,
        const std::shared_ptr<brpc::Server> &server,
        const std::shared_ptr<ServiceRegister> &sr):
        _mysqlClient(mysqlClient),
        _esClient(esClient),
        _rmqClient(rmqClient),
        _sd(sd),
        _server(server),
        _sr(sr)
    {}

    /// @brief 启动服务器主循环，直到进程退出
    void start()
    {
        _server->RunUntilAskedToQuit();
    }

private:
    std::shared_ptr<odb::core::database> _mysqlClient;
    std::shared_ptr<elasticlient::Client> _esClient;
    std::shared_ptr<RMQClient> _rmqClient;
    std::shared_ptr<ServiceDiscover> _sd;
    std::shared_ptr<brpc::Server> _server;
    std::shared_ptr<ServiceRegister> _sr;
};

class MsgStorageServerBuilder
{
public:
    /// @brief 创建并初始化 MySQL 客户端
    /// @param user MySQL 用户名
    /// @param password MySQL 密码
    /// @param host MySQL 主机地址
    /// @param db 数据库名
    /// @param characterSet 字符集
    /// @param port 端口
    /// @param connectionPoolCnt 连接池大小
    void makeMysqlClient(
        const std::string &user,
        const std::string &password,
        const std::string &host,
        const std::string &db,
        const std::string &characterSet,
        uint16_t port,
        int connectionPoolCnt)
    {
        _mysqlClient = ODBFactory::create(user, password, host, db, characterSet, port, connectionPoolCnt);
    }

    /// @brief 创建 ES 客户端并初始化消息索引
    /// @param hostUrlList ES 主机地址列表
    void makeEsClient(const std::vector<std::string> &hostUrlList)
    {
        _esClient = ESClientFactory::create(hostUrlList);
        if (!_esClient)
        {
            LOG_ERROR("初始化ES客户端失败");
            abort();
        }
        _esMessage = std::make_shared<ESMessage>(_esClient);
        if (!_esMessage->createIndex())
        {
            LOG_ERROR("初始化消息ES索引失败");
            abort();
        }
    }

    /// @brief 创建 RabbitMQ 客户端并完成队列交换机绑定
    /// @param user RabbitMQ 用户名
    /// @param password RabbitMQ 密码
    /// @param ip RabbitMQ 主机地址
    /// @param port RabbitMQ 端口
    /// @param exchangeName 交换机名
    /// @param queueName 队列名
    /// @param routingKey 路由键
    void makeRMQClient(
        const std::string &user,
        const std::string &password,
        const std::string &ip,
        const uint16_t port,
        const std::string &exchangeName,
        const std::string &queueName,
        const std::string &routingKey)
    {
        _consumeQueueName = queueName;
        _rmqClient = std::make_shared<RMQClient>(user, password, ip, port);
        _rmqClient->bindQueueAndExchange(queueName, routingKey, exchangeName);
    }

    /// @brief 初始化服务发现与 RPC 服务管理器
    /// @param serviceRegistryAddr etcd 地址
    /// @param baseDir 服务根目录
    /// @param userServiceName 用户服务名
    /// @param fileServiceName 文件服务名
    void makeRpcsmAndSdObj(
        const std::string &serviceRegistryAddr,
        const std::string &baseDir,
        const std::string &userServiceName,
        const std::string &fileServiceName)
    {
        _userServiceDir = normalizeServiceDir(baseDir, userServiceName);
        _fileServiceDir = normalizeServiceDir(baseDir, fileServiceName);

        _rpcsm = std::make_shared<RPCServiceManager>();
        _rpcsm->followService(_userServiceDir);
        _rpcsm->followService(_fileServiceDir);

        auto putCb = std::bind(&RPCServiceManager::serviceOnline, _rpcsm, std::placeholders::_1, std::placeholders::_2);
        auto delCb = std::bind(&RPCServiceManager::serviceOffline, _rpcsm, std::placeholders::_1, std::placeholders::_2);
        _sd = std::make_shared<ServiceDiscover>(serviceRegistryAddr, baseDir, putCb, delCb);
    }

    /// @brief 创建并启动 brpc 服务器
    /// @param port 实例端口
    /// @param idleTimeoutSec 空闲超时时间
    /// @param numThreads 工作线程数
    void makeRpcServer(uint16_t port, int idleTimeoutSec, int numThreads)
    {
        if (!_mysqlClient)
        {
            LOG_ERROR("未初始化MySQL客户端模块");
            abort();
        }
        if (!_esClient || !_esMessage)
        {
            LOG_ERROR("未初始化ES客户端模块");
            abort();
        }
        if (!_rmqClient || _consumeQueueName.empty())
        {
            LOG_ERROR("未初始化RabbitMQ客户端模块");
            abort();
        }
        if (_userServiceDir.empty() || _fileServiceDir.empty() || !_rpcsm || !_sd)
        {
            LOG_ERROR("未初始化服务发现模块");
            abort();
        }

        _server = std::make_shared<brpc::Server>();
        auto *service = new MsgStorageServiceImpl(
            _userServiceDir,
            _fileServiceDir,
            _consumeQueueName,
            _rpcsm,
            _rmqClient,
            _mysqlClient,
            _esClient);
        if (_server->AddService(service, brpc::ServiceOwnership::SERVER_OWNS_SERVICE) == -1)
        {
            LOG_ERROR("添加rpc服务失败");
            abort();
        }

        brpc::ServerOptions options;
        options.idle_timeout_sec = idleTimeoutSec;
        options.num_threads = numThreads;
        if (_server->Start(port, &options) == -1)
        {
            LOG_ERROR("启动rpc服务器失败");
            abort();
        }
    }

    /// @brief 创建服务注册对象并向注册中心注册实例
    /// @param serviceRegistryAddr etcd 地址
    /// @param ttl 服务 TTL
    /// @param instanceName 实例名（含服务路径）
    /// @param instanceAddr 实例地址（ip:port）
    void makeServiceRegisterObj(
        const std::string &serviceRegistryAddr,
        int ttl,
        const std::string &instanceName,
        const std::string &instanceAddr)
    {
        if (!_server)
        {
            LOG_ERROR("未初始化rpc服务器模块");
            abort();
        }
        _sr = std::make_shared<ServiceRegister>(serviceRegistryAddr, ttl);
        _sr->registerService(instanceName, instanceAddr);
    }

    /// @brief 构建最终的消息存储服务器对象
    /// @return 组装完成的服务器对象
    std::shared_ptr<MsgStorageServer> NewServer()
    {
        if (!_mysqlClient || !_esClient || !_rmqClient || !_sd || !_server || !_sr)
        {
            LOG_ERROR("消息存储服务器组件初始化不完整");
            abort();
        }
        return std::make_shared<MsgStorageServer>(_mysqlClient, _esClient, _rmqClient, _sd, _server, _sr);
    }

private:
    /// @brief 规范化服务目录路径，保证形如 /service/xxx
    /// @param baseDir 服务根目录
    /// @param serviceName 服务名
    /// @return 规范化后的服务目录
    static std::string normalizeServiceDir(const std::string &baseDir, const std::string &serviceName)
    {
        std::string serviceDir;
        if (!baseDir.empty() && baseDir.back() == '/') serviceDir = baseDir + serviceName;
        else serviceDir = baseDir + '/' + serviceName;
        if (!serviceDir.empty() && serviceDir.back() == '/') serviceDir.pop_back();
        return serviceDir;
    }

private:
    std::shared_ptr<odb::core::database> _mysqlClient;
    std::shared_ptr<elasticlient::Client> _esClient;
    std::shared_ptr<ESMessage> _esMessage;
    std::shared_ptr<RMQClient> _rmqClient;
    std::string _consumeQueueName;

    std::string _userServiceDir;
    std::string _fileServiceDir;
    std::shared_ptr<RPCServiceManager> _rpcsm;
    std::shared_ptr<ServiceDiscover> _sd;

    std::shared_ptr<brpc::Server> _server;
    std::shared_ptr<ServiceRegister> _sr;
};

} // namespace imserver
