
#include <elasticlient/client.h>
#include <memory>
#include <string>

#include "es.hpp"
#include "../odb/user.hxx"

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


}