#pragma once


#include "etcd.hpp"
#include "log.hpp"
#include "rpcService.hpp"
#include "rabbitMQ.hpp"
#include "utils.hpp"


#include "mysql.hpp"
#include "chat_session_member-mysql.hpp"
#include "chat-session-member-odb.hxx"

#include "base.pb.h"
#include "msgTransmit.pb.h"
#include "user.pb.h"

#include <brpc/controller.h>
#include <brpc/server.h>
#include <cstdlib>
#include <memory>
#include <mysql/mysql.h>
#include <odb/forward.hxx>



namespace imserver{

class MsgTransmitServiceImpl : public imserver::MsgTransmitService
{
public:
    MsgTransmitServiceImpl(
        const std::string &userServiceDir,
        const std::shared_ptr<RPCServiceManager> &rpcsm,
        const std::string &exchangeName,
        const std::string &routingKey,
        const std::shared_ptr<RMQClient> &rmqClient,
        const std::shared_ptr<odb::core::database> &mysqlClient
    ):
        _userServiceDir(userServiceDir),
        _rpcsm(rpcsm),
        _exchangeName(exchangeName),
        _routingKey(routingKey),
        _rmqClient(rmqClient),
        _chatSessionMemberTable(std::make_shared<imserver::ChatSessionMemberTable>(mysqlClient))
    {}

    template<class Request,class Response> 
    void setErrorResponse(const Request& request,Response &response,const std::string &errMessage)
    {
        response->set_request_id(request->request_id());
        response->set_success(false);
        response->set_error_message(errMessage);
    }

    virtual void GetTransmitTarget(::google::protobuf::RpcController* controller,
                       const ::imserver::NewMessageRequest* request,
                       ::imserver::GetTransmitTargetResponse* response,
                       ::google::protobuf::Closure* done)
    {
        //类似智能指针，在函数运行结束时需要确保调用done->run();
        //调用run()才表示服务器已经处理完该次响应
        brpc::ClosureGuard rpc_guard(done);

        //获取请求中的内容
        const std::string userId=request->user_id();
        const std::string sessionId=request->session_id();
        const std::string chatSessionId=request->chat_session_id();
        const auto &messageContent=request->message_content();

        //寻找可以提供用户子服务的服务器
        auto channel=_rpcsm->getChannel(_userServiceDir);
        if(!channel)
        {
            setErrorResponse(request,response,"没有可以提供用户子服务的服务器");
            LOG_ERROR("没有可以提供用户子服务的服务器,request_id={}",request->request_id());
            return ;
        }

        //获取消息发送者的信息
        UserService_Stub stub(channel.get());
        brpc::Controller cntl;
        GetSingleUserInfoRequest req;
        GetSingleUserInfoResponse rsp;
        req.set_request_id(request->request_id());
        req.set_user_id(userId);
        stub.GetSingleUserInfo(&cntl,&req,&rsp,nullptr);
        if(cntl.Failed() || rsp.success()==false)
        {
            setErrorResponse(request,response,"获取消息发送者信息时失败");
            LOG_ERROR("获取消息发送者信息时失败,request_id={}",request->request_id());
            return ;
        }

        //获取该信息需要转发到的用户
        std::vector<std::string> targetIds=_chatSessionMemberTable->members(chatSessionId);

        //填充关于消息的信息
        MessageInfo messageInfo;
        messageInfo.set_message_id(uuid());
        messageInfo.set_chat_session_id(chatSessionId);
        messageInfo.set_timestamp(time(nullptr));
        messageInfo.mutable_sender()->CopyFrom(rsp.user_info());
        messageInfo.mutable_message_content()->CopyFrom(messageContent);

        //向_routingKey对应的队列发送消息
        if(_rmqClient->publish(_exchangeName,_routingKey,messageInfo.SerializeAsString())==false)
        {
            setErrorResponse(request,response,"持久化转发消息时失败");
            LOG_ERROR("持久化转发消息时失败,request_id={}",request->request_id());
            return ;
        }

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->mutable_message_info()->CopyFrom(messageInfo);
        for(const auto &targetId:targetIds)
        {
            response->add_target_id(targetId);
        }
    }

private:
    //用户服务目录: 格式:/service/user
    std::string _userServiceDir;
    //用于寻找可以提供服务信道的RPC服务管理器
    std::shared_ptr<RPCServiceManager> _rpcsm;

    //交换机名字
    std::string _exchangeName;
    //routingKey
    std::string _routingKey;
    //RabbitMQ客户端
    std::shared_ptr<RMQClient> _rmqClient;
    
    //聊天会话成员表
    std::shared_ptr<ChatSessionMemberTable> _chatSessionMemberTable;
};


/// @brief 消息转发服务器
class MsgTransmitServer
{
public:

    MsgTransmitServer(
        const std::shared_ptr<odb::core::database> &mysqlClient,
        const std::shared_ptr<RMQClient> &rmqClient,
        const std::shared_ptr<ServiceDiscover> &sd,
        const std::shared_ptr<brpc::Server> &server,
        const std::shared_ptr<ServiceRegister> &sr
    ):
        _mysqlClient(mysqlClient),
        _rmqClient(rmqClient),
        _sd(sd),
        _server(server),
        _sr(sr)
    {}

    /// @brief 使得服务器进程一直运行,直到收到2号信号停止
    void start()
    {
        //服务器进程运行,直到收到2号信号(ctrl+c)
        _server->RunUntilAskedToQuit();
    }

private:
    //mysql客户端
    std::shared_ptr<odb::core::database> _mysqlClient;
    //rabbitMQ客户端
    std::shared_ptr<RMQClient> _rmqClient;

    //服务发现客户端
    std::shared_ptr<ServiceDiscover> _sd;

    //rpc服务器
    std::shared_ptr<brpc::Server> _server;

    //服务注册客户端
    std::shared_ptr<ServiceRegister> _sr;
};


/// @brief MsgTransmit类的建造者类
class MsgTransmitServerBuilder
{
public:
    /// @brief 创建mysql客户端
    /// @param user mysql用户名
    /// @param password mysql密码
    /// @param host mysql服务器ip
    /// @param db 连接的数据库
    /// @param characterSet mysql字符集
    /// @param port mysql服务器端口
    /// @param connectionPoolCnt mysql连接池连接数量
    void makeMysqlClient( 
        const std::string &user,
        const std::string &password,
        const std::string &host,
        const std::string &db,
        const std::string &characterSet,
        uint16_t port,
        int connectionPoolCnt)
    {
        _mysqlClient=ODBFactory::create(user,password,host,db,characterSet,port,connectionPoolCnt);
    }

    /// @brief 创建RMQ客户端并将交换机和队列绑定在一起,队列绑定的key为routingKey
    void makeRMQClient(
        const std::string &user,
        const std::string &password,
        const std::string &ip,
        const uint16_t port,
        const std::string &exchangeName,
        const std::string &queueName,
        const std::string &routingKey
    )
    {
        _exchangeName=exchangeName;
        _routingKey=routingKey;
        
        //创建RMQ客户端
        _rmqClient=std::make_shared<RMQClient>(user,password,ip,port);

        //将队列和交换机绑定在一起
        _rmqClient->bindQueueAndExchange(queueName,routingKey,exchangeName);
    }


    /// @brief 创建rpc服务管理器(并设置关心的服务),服务发现对象;
    /// @param serviceRegistryAddr 服务注册中心的IP:port
    /// @param baseDir 服务根目录(形式：/service/)
    /// @param userServiceName 用户服务名称(不带实例名,格式例如"user",最后面不能加'/')
    void makeRpcsmAndSdObj(
        const std::string &serviceRegistryAddr,
        const std::string &baseDir,
        const std::string &userServiceName)
    {
        //设置文件服务目录
        if(baseDir.back()=='/') _userServiceDir=baseDir+userServiceName;
        else _userServiceDir=baseDir+'/'+userServiceName;
        if(_userServiceDir.back()=='/') _userServiceDir.pop_back();

        //创建rpc服务管理器对象
        _rpcsm=std::make_shared<RPCServiceManager>();
        _rpcsm->followService(_userServiceDir);

        //创建服务发现对象
        auto putCb=std::bind(&RPCServiceManager::serviceOnline,_rpcsm,std::placeholders::_1,std::placeholders::_2);
        auto delCb=std::bind(&RPCServiceManager::serviceOffline,_rpcsm,std::placeholders::_1,std::placeholders::_2);
        _sd=std::make_shared<ServiceDiscover>(
            serviceRegistryAddr,
            baseDir,
            putCb,
            delCb
        );
    }

    /// @brief 创建rpc服务器,并启动服务器
    /// @param port 服务器端口
    /// @param idle_timeout_sec rpc服务器超时时间,-1为阻塞等待 
    /// @param num_threads rpc服务器线程数
    void makeRpcServer(uint16_t port,int idle_timeout_sec,int num_threads)
    {
        if(!_mysqlClient) 
        {
            LOG_ERROR("未初始化MySQL客户端模块");
            abort();
        }
        if(!_rmqClient)
        {
            LOG_ERROR("未初始化rabbitMQ客户端模块");
            abort();
        }
        if(_userServiceDir.empty() || !_rpcsm || !_sd)
        {
            LOG_ERROR("未初始化服务发现模块");
            abort();
        }

        _server=make_shared<brpc::Server>();

        //往服务器里面添加服务
        MsgTransmitServiceImpl *service=new MsgTransmitServiceImpl(
            _userServiceDir,
            _rpcsm,
            _exchangeName,
            _routingKey,
            _rmqClient,
            _mysqlClient
        );

        if(_server->AddService(service,brpc::ServiceOwnership::SERVER_OWNS_SERVICE)==-1)
        {
            LOG_ERROR("添加rpc服务失败");
            abort();
        }

        //服务器参数选项对象
        brpc::ServerOptions options;
        options.idle_timeout_sec=idle_timeout_sec;//超时时间
        options.num_threads=num_threads;//服务器线程数量

        //启动服务器
        if(_server->Start(port,&options)==-1)
        {
            LOG_ERROR("启动rpc服务器失败");
            abort();
        }
    }

    /// @brief 创建服务注册对象,并进行服务注册操作
    /// @param serviceRegistryAddr 服务注册中心的IP:port
    /// @param ttl 当取消keepAlive后,服务在线时长(单位:s)
    /// @param instanceName /服务名/实例名
    /// @param instanceAddr 实例ip:port
    void makeServiceRegisterObj(const std::string& serviceRegistryAddr,int ttl,const std::string &instanceName,const std::string &instanceAddr)
    {
        if(!_server) 
        {
            LOG_ERROR("未初始化rpc服务器模块");
            abort();
        }

        //创建服务注册对象
        _sr=std::make_shared<ServiceRegister>(serviceRegistryAddr,ttl);

        //进行服务注册操作
        _sr->registerService(instanceName,instanceAddr);
    }

    /// @brief 创建一个消息转发服务器
    /// @return 返回一个消息转发服务器对象
    std::shared_ptr<MsgTransmitServer> NewServer()
    {
        if(!_mysqlClient) 
        {
            LOG_ERROR("未初始化MySQL客户端模块");
            abort();
        }
        if(!_rmqClient)
        {
            LOG_ERROR("未初始化rabbitMQ客户端模块");
            abort();
        }
        if(_userServiceDir.empty() || !_rpcsm || !_sd)
        {
            LOG_ERROR("未初始化服务发现模块");
            abort();
        }
        if(!_server)
        {
            LOG_ERROR("未初始化rpc服务器模块");
            abort();
        }
        if(!_sr)
        {
            LOG_ERROR("没有对已上线的服务进行服务注册");
            abort();
        }

        return std::make_shared<MsgTransmitServer>(_mysqlClient,_rmqClient,_sd,_server,_sr);
    }

private:

    //mysql客户端
    std::shared_ptr<odb::core::database> _mysqlClient;

    //交换机名字
    std::string _exchangeName;
    //routingKey
    std::string _routingKey;
    //RabbitMQ客户端
    std::shared_ptr<RMQClient> _rmqClient;

    //用户服务目录: 格式:/service/user
    std::string _userServiceDir;
    //用于寻找可以提供服务信道的RPC服务管理器
    std::shared_ptr<RPCServiceManager> _rpcsm;
    //服务发现客户端
    std::shared_ptr<ServiceDiscover> _sd;

    //rpc服务器
    std::shared_ptr<brpc::Server> _server;

    //服务注册客户端
    std::shared_ptr<ServiceRegister> _sr;
};

}