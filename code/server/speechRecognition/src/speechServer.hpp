#pragma once

#include "asr.hpp"
#include "etcd.hpp"
#include "log.hpp"
#include "rpcService.hpp"
#include "speechRecognition.pb.h"
#include <brpc/server.h>



namespace imserver{

class SpeechRecognitionServiceImpl : public imserver::SpeechRecognitionService
{
public:
    SpeechRecognitionServiceImpl(const std::shared_ptr<ASRClient> &client):
        _client(client)
    {}


    void RecognizeSpeech(::google::protobuf::RpcController* controller,
                       const ::imserver::SpeechRecognitionRequest* request,
                       ::imserver::SpeechRecognitionResponse* response,
                       ::google::protobuf::Closure* done)
    {
        LOG_DEBUG("收到语音转文字请求,请求id={}",request->request_id());

        //类似智能指针，在函数运行结束时需要确保调用done->run();
        //调用run()才表示服务器已经处理完该次响应
        brpc::ClosureGuard rpc_guard(done);

        //进行语音识别
        std::string errorMessage;
        std::string recognized_text=_client->recognize(request->speech_content(),errorMessage);

        //根据识别结果填充响应
        response->set_request_id(request->request_id());
        if(recognized_text.empty() && !errorMessage.empty())//如果识别失败
        {
            response->set_success(false);
            response->set_error_message(errorMessage);
        }
        else//如果识别成功
        {
            response->set_success(true);
            response->set_recognized_text(recognized_text);
        }
    }

private:
    std::shared_ptr<ASRClient> _client;
};


/// @brief 语音识别服务器
class SpeechRecognitionServer
{
public:
    /// @brief 构造函数
    /// @param asrClient 百度语音识别客户端
    /// @param sr 服务注册对象
    /// @param server rpc服务器
    SpeechRecognitionServer(std::shared_ptr<ASRClient> asrClient,
                            std::shared_ptr<ServiceRegister> sr,
                            std::shared_ptr<brpc::Server> server):
        _asrClient(asrClient),
        _sr(sr),
        _server(server)
    {}

    /// @brief 使得服务器进程一直运行,直到收到2号信号停止
    void start()
    {
        //服务器进程运行,直到收到2号信号(ctrl+c)
        _server->RunUntilAskedToQuit();
    }

private:
    std::shared_ptr<ASRClient> _asrClient;
    std::shared_ptr<ServiceRegister> _sr;
    std::shared_ptr<brpc::Server> _server;
};


/// @brief SpeechRecognitionServer类的建造者类,
/// 调用顺序:
/// 1. makeASRClientObj()
/// 2. makeRpcServer()
/// 3. makeServiceRegisterObj()
/// 4. NewServer()
class SpeechRecognitionServerBuilder
{
public:
    /// @brief 创建ASR客户端对象
    /// @param appId 百度语音识别appId
    /// @param apiKey 百度语音识别apiKey
    /// @param secretKey 百度语音识别secretKey
    void makeASRClientObj(const std::string &appId,const std::string &apiKey,const std::string &secretKey)
    {
        _asrClient=std::make_shared<ASRClient>(appId,apiKey,secretKey);
    }

    /// @brief 创建rpc服务器,并启动服务器
    /// @param port 服务器端口
    /// @param idle_timeout_sec rpc服务器超时时间,-1为阻塞等待 
    /// @param num_threads rpc服务器线程数
    void makeRpcServer(uint16_t port,int idle_timeout_sec,int num_threads)
    {
        if(!_asrClient) 
        {
            LOG_ERROR("未初始化语音识别客户端模块");
            abort();
        }

        _server=make_shared<brpc::Server>();

        //往服务器里面添加服务
        SpeechRecognitionServiceImpl *service=new SpeechRecognitionServiceImpl(_asrClient);
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

    /// @brief 创建一个语音识别服务器
    /// @return 返回一个语音识别服务器对象
    std::shared_ptr<SpeechRecognitionServer> NewServer()
    {
        if(!_asrClient) 
        {
            LOG_ERROR("未初始化语音识别客户端模块");
            abort();
        }
        if(!_server) 
        {
            LOG_ERROR("未初始化rpc服务器模块");
            abort();
        }
        if(!_sr)
        {
            LOG_ERROR("未初始化服务注册模块");
        }

        return std::make_shared<SpeechRecognitionServer>(_asrClient,_sr,_server);
    }

private:
    std::shared_ptr<ASRClient> _asrClient;
    std::shared_ptr<ServiceRegister> _sr;
    std::shared_ptr<brpc::Server> _server;
};

}