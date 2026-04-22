#pragma once

#include <string>
#include <cstddef> 
#include <odb/nullable.hxx>
#include <odb/core.hxx>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace imserver{

#pragma db object table("message")
class Message
{
public:
    Message() = default;

    Message(
        const std::string &messageId,
        const std::string &chatSessionId,
        const std::string &userId,
        const unsigned char messageType,
        const boost::posix_time::ptime &createTime
    ):
        _message_id(messageId),
        _chat_session_id(chatSessionId),
        _user_id(userId),
        _message_type(messageType),
        _create_time(createTime)
    {}

    std::string messageId() const {return _message_id;}
    void messageId(const std::string &val) {_message_id=val;}

    std::string chatSessionId() const {return _chat_session_id;}
    void chatSessionId(const std::string &val) {_chat_session_id=val;}

    std::string userId() const {return _user_id;}
    void userId(const std::string &val) {_user_id=val;}

    unsigned char messageType() const {return _message_type;}
    void messageType(const unsigned char val) {_message_type=val;}

    boost::posix_time::ptime createTime() const {return _create_time;}
    void createTime(const boost::posix_time::ptime &val) {_create_time=val;}

    std::string content() const
    {
        if(!_content) return std::string();
        return *_content;
    }
    void content(const std::string &val) {_content=val;}
 

    std::string fileId() const
    {
        if(!_file_id) return std::string();
        return *_file_id;
    }
    void fileId(const std::string &val) {_file_id=val;}


    std::string fileName() const
    {
        if(!_file_name) return std::string();
        return *_file_name;
    }
    void fileName(const std::string &val) {_file_name=val;}


    unsigned int fileSize() const
    {
        if(!_file_size) return 0;
        return *_file_size;
    }
    void fileSize(const int val) {_file_size=val;}

private:
    friend class odb::access;

    #pragma db id auto
    unsigned long _id;

    //消息唯一id
    #pragma db type("varchar(64)") index unique
    std::string _message_id;

    //消息所属聊天会话id
    #pragma db type("varchar(64)") index
    std::string _chat_session_id;

    //消息的发送者用户id
    #pragma db type("varchar(64)")
    std::string _user_id;

    //消息类型
    unsigned char _message_type;

    //消息创建的时间戳
    #pragma db type("TIMESTAMP")
    boost::posix_time::ptime _create_time;

    //消息文本内容,跟下面三个字段是互斥的
    odb::nullable<std::string> _content;

    //消息文件id
    #pragma db type("varchar(64)")
    odb::nullable<std::string> _file_id;
    //消息文件名字
    #pragma db type("varchar(128)")
    odb::nullable<std::string> _file_name;
    //消息文件大小
    odb::nullable<unsigned int> _file_size;
};
}