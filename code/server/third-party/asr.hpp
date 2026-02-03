#pragma once

#include "aip-cpp-sdk-4.16.7/speech.h"
#include "log.hpp"

class ASRClient
{
public:
    /// @brief 构造函数
    /// @param appId  百度语音识别appID
    /// @param apiKey 百度语音识别apiKey
    /// @param secretKey 百度语音识别secretKey
    ASRClient(const std::string &appId,
              const std::string &apiKey,
              const std::string &secretKey):
        _client(appId,apiKey,secretKey)
    {}

    /// @brief 传入一个.pcm文件,进行语音识别,返回转换的文字
    /// @param pcmFileName .pcm文件名
    /// @param rate 采样率
    /// @return 返回转换的结果
    std::string recognize(const std::string &pcmFileName,const int rate=16000)
    {
        //提取.pcm文件的内容到fileContent
        std::string fileContent;
        aip::get_file_content(pcmFileName.c_str(), &fileContent);

        //进行语言转文字识别
        Json::Value result = _client.recognize(fileContent, "pcm", rate, aip::null);

        //判断结果错误码是否为0,不为0表示错误
        if(result["err_no"].asInt()!=0)
        {
            LOG_ERROR("语言转文字错误,错误码:{},错误内容:{}",result["err_no"].asInt(),result["err_msg"].asString());
            return std::string();
        }

        //返回识别结果
        return result["result"][0].asString();
    }

private:
    /// @brief 百度语音识别SDK客户端
    aip::Speech _client;
};