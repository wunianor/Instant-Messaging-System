#pragma once

#include <string>
#include <brpc/server.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "etcd.hpp"
#include "log.hpp"
#include "rpcService.hpp"
#include "base.pb.h"
#include "file.pb.h"
#include "utils.hpp"


namespace imserver{

class FileServiceImpl : public imserver::FileService
{
public:
    FileServiceImpl(const std::string fileBaseDir):
        _fileBaseDir(fileBaseDir)
    {
        umask(0);
        mkdir(_fileBaseDir.c_str(),0775);
        if(_fileBaseDir.back()!='/') _fileBaseDir += '/';
    }

    void GetSingleFile(::google::protobuf::RpcController* controller,
                       const ::imserver::GetSingleFileRequest* request,
                       ::imserver::GetSingleFileResponse* response,
                       ::google::protobuf::Closure* done)
    {
        //类似智能指针，在函数运行结束时需要确保调用done->run();
        //调用run()才表示服务器已经处理完该次响应
        brpc::ClosureGuard rpc_guard(done);

        const std::string fileId=request->file_id();
        const std::string filePath=_fileBaseDir+fileId;
        std::string fileContent;

        //根据读取文件结果来填充响应
        if(readFile(filePath,fileContent)==false)
        {
            //填充响应
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message("读取文件"+filePath+"失败!");
            LOG_ERROR("读取文件{}失败!,请求id:{},用户id:{},文件uuid:{}",filePath,request->request_id(),request->user_id(),request->file_id());
        }
        else
        {
            //填充响应
            response->set_request_id(request->request_id());
            response->set_success(true);
            response->mutable_file_data()->set_file_id(fileId);
            response->mutable_file_data()->set_file_content(fileContent);
        }

    }

    void GetMultiFile(::google::protobuf::RpcController* controller,
                       const ::imserver::GetMultiFileRequest* request,
                       ::imserver::GetMultiFileResponse* response,
                       ::google::protobuf::Closure* done)
    {
        //类似智能指针，在函数运行结束时需要确保调用done->run();
        //调用run()才表示服务器已经处理完该次响应
        brpc::ClosureGuard rpc_guard(done);

        for(int i=0;i<request->file_id_list_size();++i)
        {
            const std::string fileId=request->file_id_list(i);
            const std::string filePath=_fileBaseDir+fileId;
            std::string fileContent;

            if(readFile(filePath,fileContent)==false)
            {
                response->set_request_id(request->request_id());
                response->set_success(false);
                response->mutable_file_data_map()->clear();
                response->set_error_message("读取文件"+filePath+"失败");
                LOG_ERROR("读取文件{}失败!,请求id:{},用户id:{},文件uuid:{}",filePath,request->request_id(),request->user_id(),fileId);
                return;
            }

            FileDownloadData fdd;
            fdd.set_file_id(fileId);
            fdd.set_file_content(fileContent);
            response->mutable_file_data_map()->insert({fileId,fdd});
        }

        response->set_request_id(request->request_id());
        response->set_success(true);
    }

    void PutSingleFile(::google::protobuf::RpcController* controller,
                       const ::imserver::PutSingleFileRequest* request,
                       ::imserver::PutSingleFileResponse* response,
                       ::google::protobuf::Closure* done)
    {
        //类似智能指针，在函数运行结束时需要确保调用done->run();
        //调用run()才表示服务器已经处理完该次响应
        brpc::ClosureGuard rpc_guard(done);

        const std::string fileId=uuid();
        const std::string filePath=_fileBaseDir+fileId;

        if(writeFile(filePath,request->file_data().file_content())==false)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message("写入文件"+filePath+"失败");
            LOG_ERROR("写入文件{}失败!,请求id:{},用户id:{},文件uuid:{}",filePath,request->request_id(),request->user_id(),fileId);
        }
        else 
        {
            response->set_request_id(request->request_id());
            response->set_success(true);
            response->mutable_file_info()->set_file_id(fileId);
            response->mutable_file_info()->set_file_size(request->file_data().file_size());
            response->mutable_file_info()->set_file_name(request->file_data().file_name());
        }
    }

    void PutMultiFile(::google::protobuf::RpcController* controller,
                       const ::imserver::PutMultiFileRequest* request,
                       ::imserver::PutMultiFileResponse* response,
                       ::google::protobuf::Closure* done)
    {
        //类似智能指针，在函数运行结束时需要确保调用done->run();
        //调用run()才表示服务器已经处理完该次响应
        brpc::ClosureGuard rpc_guard(done);

        for(int i=0;i<request->file_data_list_size();++i)
        {
            const auto &fileData=request->file_data_list(i);
            const std::string fileId=uuid();
            const std::string filePath=_fileBaseDir+fileId;

            if(writeFile(filePath,fileData.file_content())==false)
            {
                response->set_request_id(request->request_id());
                response->set_success(false);
                response->mutable_file_info_list()->Clear();
                response->set_error_message("写入文件"+filePath+"失败");

                LOG_ERROR("写入文件{}失败!,请求id:{},用户id:{},文件uuid:{}",filePath,request->request_id(),request->user_id(),fileId);
                return ;
            }

            auto *newFileInfo=response->add_file_info_list();
            newFileInfo->set_file_id(fileId);
            newFileInfo->set_file_size(fileData.file_size());
            newFileInfo->set_file_name(fileData.file_name());
        }

        response->set_request_id(request->request_id());
        response->set_success(true);

    }

private:

    /// @brief 存储文件的根目录
    std::string _fileBaseDir; 
};


/// @brief 文件服务器
class FileServer
{
public:
    FileServer(std::shared_ptr<ServiceRegister> sr,
               std::shared_ptr<brpc::Server> server):
        _sr(sr),
        _server(server)
    {}

    void start()
    {
        //服务器进程运行,直到收到2号信号(ctrl+c)
        _server->RunUntilAskedToQuit();
    }

private:
    std::shared_ptr<ServiceRegister> _sr;
    std::shared_ptr<brpc::Server> _server;
};


class FileServerBuilder
{
public:
    /// @brief 创建rpc服务器,并启动服务器
    /// @param port 服务器端口
    /// @param idle_timeout_sec rpc服务器超时时间,-1为阻塞等待 
    /// @param num_threads rpc服务器线程数
    void makeRpcServer(uint16_t port,int idle_timeout_sec,int num_threads,const std::string &fileBaseDir="./data/")
    {
        _server=make_shared<brpc::Server>();

        //往服务器里面添加服务
        FileServiceImpl *service=new FileServiceImpl(fileBaseDir);
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

    /// @brief 创建一个文件服务器
    /// @return 返回一个文件服务器对象
    std::shared_ptr<FileServer> NewServer()
    {
        if(!_server) 
        {
            LOG_ERROR("未初始化rpc服务器模块");
            abort();
        }
        if(!_sr)
        {
            LOG_ERROR("未初始化服务注册模块");
        }

        return std::make_shared<FileServer>(_sr,_server);
    }

private:
    std::shared_ptr<ServiceRegister> _sr;
    std::shared_ptr<brpc::Server> _server;
};

}