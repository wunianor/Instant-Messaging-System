
#include <elasticlient/client.h>
#include <boost/date_time/posix_time/time_parsers.hpp>
#include <memory>
#include <string>
#include <vector>

#include "es.hpp"
#include "../odb/user.hxx"
#include "../odb/message.hxx"

namespace imserver 
{

class ESClientFactory
{
public:
    static std::shared_ptr<elasticlient::Client> create(const std::vector<std::string> &hostUrlList)
    {
        try {
            return std::make_shared<elasticlient::Client>(hostUrlList);
        } catch (const std::exception &e) {
            LOG_ERROR("创建ES客户端失败,错误信息:{}",e.what());
            return nullptr;
        }
    }
};


class ESUser
{
public:
    ESUser(const std::shared_ptr<elasticlient::Client> &client):
        _client(client)
    {}

    bool createIndex()
    {
        if(ESIndex(_client,"user")
            .append("user_id","keyword","standard")
            .append("nickname")
            .append("phone","keyword","standard")
            .append("description","text","standard",false)
            .append("avatarId","keyword","standard",false)
            .create()==false)
        {
            LOG_ERROR("创建es索引user失败");
            return false;
        }
        else {
            LOG_INFO("创建es索引user成功");
            return true;
        }
    }

    bool addData(
        const std::string &userId,
        const std::string &nickname,
        const std::string &description,
        const std::string &phone,
        const std::string &avatarId
    )
    {
        if(ESInsert(_client,"user")
            .append("userId", userId)
            .append("nickname", nickname)
            .append("description", description)
            .append("phone", phone)
            .append("avatarId", avatarId)
            .insert()==false)
        {
            LOG_ERROR("向es索引user插入/修改数据失败,用户id:{}",userId);
            return false;
        }
        else {
            LOG_INFO("向es索引user插入/修改数据成功,用户id:{}",userId);
            return true;
        }
    }


    std::vector<imserver::User> search(const std::string &key,const std::vector<std::string> &userIds)
    {
        ESSearch esSearch(_client,"user");
        esSearch.append("should","match","userId.keyword",key)
            .append("should", "match", "nickname",key)
            .append("should","match","phone.keyword",key);
        for(const auto &userId:userIds)
        {
            esSearch.append("must_not","match","userId.keyword",userId);
        }
        Json::Value result=esSearch.search();
        std::vector<imserver::User> users;

        if(result.empty())
        {
            LOG_INFO("没有搜索到用户,搜索关键字:{}",key);
            return users;
        }

        for(int i=0;i<result.size();++i)
        {
            const auto &userJson=result[i]["_source"];
            imserver::User user;
            user.userId(userJson["userId"].asString());
            user.nickname(userJson["nickname"].asString());
            user.description(userJson["description"].asString());
            user.phone(userJson["phone"].asString());
            user.avatarId(userJson["avatarId"].asString());
            users.push_back(user);
        }
        LOG_INFO("搜索到{}个用户,搜索关键字:{}",users.size(),key);
        return users;
    }


private:
    std::shared_ptr<elasticlient::Client> _client;
};


class ESMessage
{
public:
    ESMessage(const std::shared_ptr<elasticlient::Client> &client):
        _client(client)
    {}

    // 1. 创建索引
    bool createIndex()
    {
        if(ESIndex(_client,"message")
            .append("message_id","keyword","standard",false)
            .append("user_id","keyword","standard")
            .append("chat_session_id","keyword","standard")
            .append("create_time","long","standard",false)
            .append("content")
            .create()==false)
        {
            LOG_ERROR("创建es索引message失败");
            return false;
        }
        LOG_INFO("创建es索引message成功");
        return true;
    }

    // 2. 添加消息数据
    bool add(
        const std::string &messageId,
        const std::string &userId,
        const std::string &chatSessionId,
        const long createTime,
        const std::string &content
    )
    {
        if(ESInsert(_client,"message",messageId)
            .append("message_id",messageId)
            .append("user_id",userId)
            .append("chat_session_id",chatSessionId)
            .append("create_time",createTime)
            .append("content",content)
            .insert()==false)
        {
            LOG_ERROR("向es索引message插入/修改数据失败,消息id:{}",messageId);
            return false;
        }
        LOG_INFO("向es索引message插入/修改数据成功,消息id:{}",messageId);
        return true;
    }

    // 3. 搜索消息(支持按会话过滤)
    std::vector<imserver::Message> search(
        const std::string &key,
        const std::string &chatSessionId="",
        const std::string &userId=""
    )
    {
        ESSearch esSearch(_client,"message");
        esSearch.append("should","match","content",key);
        if(!chatSessionId.empty())
        {
            esSearch.append("must","match","chat_session_id",chatSessionId);
            // 在指定会话内,允许按发送者(user_id)继续过滤
            if(!userId.empty())
            {
                esSearch.append("must","match","user_id",userId);
            }
        }

        Json::Value result=esSearch.search();
        std::vector<imserver::Message> messages;
        if(result.empty())
        {
            LOG_INFO("没有搜索到消息,搜索关键字:{},聊天会话id:{},用户id:{}",key,chatSessionId,userId);
            return messages;
        }

        for(int i=0;i<result.size();++i)
        {
            const auto &messageJson=result[i]["_source"];
            const std::string messageId=messageJson["message_id"].asString();
            const std::string senderId=messageJson["user_id"].asString();
            const std::string sessionId=messageJson["chat_session_id"].asString();
            const std::string content=messageJson["content"].asString();
            const unsigned char messageType=messageJson.isMember("message_type")
                ? (unsigned char)messageJson["message_type"].asUInt()
                : 0;
            boost::posix_time::ptime createTime;
            if(messageJson["create_time"].isInt64())
            {
                createTime=boost::posix_time::from_time_t(messageJson["create_time"].asInt64());
            }
            else
            {
                try {
                    createTime=boost::posix_time::time_from_string(messageJson["create_time"].asString());
                } catch (...) {
                    createTime=boost::posix_time::ptime();
                }
            }

            imserver::Message msg(messageId,sessionId,senderId,messageType,createTime);
            msg.content(content);
            if(messageJson.isMember("file_id")) msg.fileId(messageJson["file_id"].asString());
            if(messageJson.isMember("file_name")) msg.fileName(messageJson["file_name"].asString());
            if(messageJson.isMember("file_size")) msg.fileSize(messageJson["file_size"].asUInt());
            messages.push_back(msg);
        }
        LOG_INFO("搜索到{}条消息,搜索关键字:{},聊天会话id:{},用户id:{}",messages.size(),key,chatSessionId,userId);
        return messages;
    }

    // 4. 删除消息
    bool remove(const std::string &messageId)
    {
        if(ESRemove(_client,"message",messageId).remove()==false)
        {
            LOG_ERROR("删除es索引message中的消息失败,消息id:{}",messageId);
            return false;
        }
        LOG_INFO("删除es索引message中的消息成功,消息id:{}",messageId);
        return true;
    }

private:
    std::shared_ptr<elasticlient::Client> _client;
};

}