#include <bits/types/FILE.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <cstddef>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include "etcd.hpp"
#include "rpcService.hpp"
#include "log.hpp"
#include "file.pb.h"
#include "base.pb.h"
#include "utils.hpp"


std::shared_ptr<brpc::Channel> channel;
std::string fileId;
std::string fileName="CMakeLists.txt";

TEST(put,singleFile)
{
    
    std::string fileContent;
    ASSERT_TRUE(imserver::readFile(fileName, fileContent));

    imserver::FileService_Stub stub(channel.get());

    //上下文管理器对象,获取rpc调用是成功还是失败
    std::shared_ptr<brpc::Controller> controller(new brpc::Controller());
    std::shared_ptr<imserver::PutSingleFileRequest> req(new imserver::PutSingleFileRequest());
    req->set_request_id("1");
    req->mutable_file_data()->set_file_name(fileName);
    req->mutable_file_data()->set_file_size(fileContent.size());
    req->mutable_file_data()->set_file_content(fileContent);
    std::shared_ptr<imserver::PutSingleFileResponse> rsp(new imserver::PutSingleFileResponse());


    stub.PutSingleFile(controller.get(),req.get(),rsp.get(),nullptr);

    ASSERT_TRUE(rsp->success());
    ASSERT_EQ(fileName,rsp->file_info().file_name());
    ASSERT_EQ(fileContent.size(),rsp->file_info().file_size());

    fileId=rsp->file_info().file_id();
    LOG_INFO("{}上传到服务器的uuid为{}",fileName,fileId);
}


TEST(get,singleFile)
{
    //上下文管理器对象,获取rpc调用是成功还是失败
    std::shared_ptr<brpc::Controller> controller(new brpc::Controller());
    std::shared_ptr<imserver::GetSingleFileRequest> req(new imserver::GetSingleFileRequest());
    req->set_request_id("2");
    req->set_file_id(fileId);
    std::shared_ptr<imserver::GetSingleFileResponse> rsp(new imserver::GetSingleFileResponse());

    imserver::FileService_Stub stub(channel.get());
    stub.GetSingleFile(controller.get(),req.get(),rsp.get(),nullptr);

    ASSERT_TRUE(rsp->success());
    ASSERT_EQ(fileId,rsp->file_data().file_id());
    imserver::writeFile(fileName+".download", rsp->file_data().file_content());
}


std::vector<std::string> fileIdList;
std::vector<std::string> fileNames{"file.pb.cc","file.pb.h"};

TEST(put,multiFile)
{
    
    std::vector<std::string> fileContents(2);
    for(int i=0;i<fileNames.size();++i)
    {
        ASSERT_TRUE(imserver::readFile(fileNames[i], fileContents[i]));
    }

    imserver::FileService_Stub stub(channel.get());

    //上下文管理器对象,获取rpc调用是成功还是失败
    std::shared_ptr<brpc::Controller> controller(new brpc::Controller());
    std::shared_ptr<imserver::PutMultiFileRequest> req(new imserver::PutMultiFileRequest());
    req->set_request_id("3");
    for(int i=0;i<fileNames.size();++i)
    {
        auto *fileData=req->add_file_data_list();
        fileData->set_file_name(fileNames[i]);
        fileData->set_file_size(fileContents[i].size());
        fileData->set_file_content(fileContents[i]);
    }
    std::shared_ptr<imserver::PutMultiFileResponse> rsp(new imserver::PutMultiFileResponse());

    stub.PutMultiFile(controller.get(),req.get(),rsp.get(),nullptr);

    ASSERT_TRUE(rsp->success());       
    for(int i=0;i<rsp->file_info_list_size();++i)
    {
        const auto & fileInfo=rsp->file_info_list(i);
        ASSERT_EQ(fileInfo.file_size(),fileContents[i].size());
        ASSERT_EQ(fileInfo.file_name(),fileNames[i]);

        LOG_INFO("{}上传到服务器的uuid为{}",fileNames[i],fileInfo.file_id());
        fileIdList.push_back(fileInfo.file_id());
    }
}


TEST(get,multiFile)
{
    imserver::FileService_Stub stub(channel.get());

    std::shared_ptr<brpc::Controller> controller(new brpc::Controller());
    std::shared_ptr<imserver::GetMultiFileRequest> req(new imserver::GetMultiFileRequest());
    req->set_request_id("4");
    for(const auto & fileId:fileIdList)
    {
        req->add_file_id_list(fileId);
    }
    std::shared_ptr<imserver::GetMultiFileResponse> rsp(new imserver::GetMultiFileResponse());
    stub.GetMultiFile(controller.get(),req.get(),rsp.get(),nullptr);
    ASSERT_TRUE(rsp->success());
    for(int i=0;i<fileIdList.size();++i)
    {
        const auto &fdd=rsp->file_data_map().at(fileIdList[i]);
        ASSERT_EQ(fdd.file_id(),fileIdList[i]);
        imserver::writeFile(fileNames[i]+".download", fdd.file_content());
    }
}



int main(int argc,char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    google::ParseCommandLineFlags(&argc, &argv, true);

    imserver::initGlogger(false);

    imserver::RPCServiceManager rpcSm;
    rpcSm.followService("/service/file");

    auto putCb=std::bind(&imserver::RPCServiceManager::serviceOnline,&rpcSm,std::placeholders::_1,std::placeholders::_2);
    auto delCb=std::bind(&imserver::RPCServiceManager::serviceOffline,&rpcSm,std::placeholders::_1,std::placeholders::_2);

    imserver::ServiceDiscover sd("127.0.0.1:2379",
                                 "/service/",
                                 putCb,
                                 delCb
                                );


    sleep(5);//等待服务发现模块发现服务并回调putCb函数,在putCb函数内会为提供file服务的主机创建信道对象并保存在rpcSm中

    channel=rpcSm.getChannel("/service/file");

    if(channel == nullptr)
    {
        LOG_ERROR("没有可以提供file服务的节点");
        return -1;
    }

    return RUN_ALL_TESTS();
}