#pragma once

#include "aip-cpp-sdk-4.16.7/speech.h"
#include "log.hpp"

namespace imserver
{


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
    /// @param errorMessage 输出型参数,保存错误信息
    /// @param rate 采样率
    /// @return 返回转换的结果,识别失败返回""
    std::string recognizeFile(const std::string &pcmFileName,std::string &errorMessage,const int rate=16000)
    {
        return recognize(getFileContent(pcmFileName),errorMessage,rate);
    }

    /// @brief 进行语音识别,返回转换的文字
    /// @param fileContent 需要进行语音识别的内容
    /// @param errorMessage 输出型参数,保存错误信息
    /// @param rate 采样率
    /// @return 返回转换的结果,识别失败返回""
    std::string recognize(const std::string &fileContent,std::string &errorMessage,const int rate=16000)
    {
        errorMessage.clear();

        //进行语言转文字识别
        Json::Value result = _client.recognize(fileContent, "pcm", rate, aip::null);

        //判断结果错误码是否为0,不为0表示错误
        if(result["err_no"].asInt()!=0)
        {
            errorMessage=result["err_msg"].asString();
            LOG_ERROR("语言转文字错误,错误码:{},错误内容:{}",result["err_no"].asInt(),errorMessage);
            return std::string();
        }

        //返回识别结果
        return result["result"][0].asString();
    }
public:
    /// @brief 提取.pcm文件的内容
    /// @param pcmFileName .pcm文件名
    /// @return 返回提取的内容
    static std::string getFileContent(const std::string &pcmFileName)
    {
        //提取.pcm文件的内容到fileContent
        std::string fileContent;
        if(aip::get_file_content(pcmFileName.c_str(), &fileContent)==-1)
        {
            LOG_ERROR("提取pcm文件失败");
            return std::string();
        }
        
        return fileContent;
    }

private:
    /// @brief 百度语音识别SDK客户端
    aip::Speech _client;
};



}