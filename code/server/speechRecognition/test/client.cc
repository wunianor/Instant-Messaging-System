#include <brpc/channel.h>
#include <iostream>
#include "speechRecognition.pb.h"
#include "etcd.hpp"
#include "rpcService.hpp"
#include "asr.hpp"


using std::cout;
using std::endl;

// //用于异步处理服务器响应的函数
// void Callback(brpc::Controller *controller,imserver::SpeechRecognitionRequest *rsp)
// {
//     //如果rpc调用失败
//     if(controller->Failed())
//     {
//         cout<<"rpc调用失败:"<<controller->ErrorText()<<endl;
//         delete controller;
//         delete rsp;
//         exit(-1);
//     }

//     //对响应进行处理
//     cout<<"收到响应:"<<rsp->message()<<endl;
//     delete controller;
//     delete rsp;
// }

int main()
{
    imserver::initGlogger(false);

    imserver::RPCServiceManager rpcsm;

    std::string host="127.0.0.1:2379";
    imserver::ServiceDiscover sd(host,
                       "/service",
                       std::bind(&imserver::RPCServiceManager::serviceOnline,&rpcsm,std::placeholders::_1,std::placeholders::_2),
                       std::bind(&imserver::RPCServiceManager::serviceOffline,&rpcsm,std::placeholders::_1,std::placeholders::_2)
                      );



    //上下文管理器对象,获取rpc调用是成功还是失败
    brpc::Controller *controller=new brpc::Controller();
    //构造请求
    imserver::SpeechRecognitionRequest *req=new imserver::SpeechRecognitionRequest();
    req->set_request_id("1");

    std::string file="../test/test1_16k.pcm";
    std::string fileContent=imserver::ASRClient::getFileContent(file);
    req->set_speech_content(fileContent);


    //用于存放响应
    imserver::SpeechRecognitionResponse *rsp=new imserver::SpeechRecognitionResponse();

    rpcsm.followService("/service/SpeechRecognition");
    while(1)
    {
        controller->Reset();
        auto channel=rpcsm.getChannel("/service/SpeechRecognition");
        if(channel == nullptr) 
        {
            sleep(10);
            continue;
        }

        //客户端存根类对象,用于进行rpc调用
        imserver::SpeechRecognitionService_Stub speechRService_stub(channel.get());
        //进行rpc调用
        speechRService_stub.RecognizeSpeech(controller,req,rsp,nullptr);

        //如果rpc调用失败
        if(controller->Failed())
        {
            cout<<"rpc调用失败:"<<controller->ErrorText()<<endl;
            sleep(10);
            continue;
        }

        //对响应进行处理
        if(rsp->success()==false)//如果识别失败
        {
            cout<<"语音识别失败,error_message="<<rsp->error_message()<<endl;
            sleep(10);
        }
        else
        {
            cout<<"识别结果:"<<rsp->recognized_text()<<endl;
            break;
        }
    }

    delete controller;
    delete req;
    delete rsp;

    return 0;
}

