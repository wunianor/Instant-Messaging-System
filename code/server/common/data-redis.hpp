#include <chrono>
#include <memory>
#include <string>
#include <sw/redis++/redis++.h>
#include <sw/redis++/utils.h>
#include "log.hpp"

namespace imserver 
{

class RedisClientFactory
{
public:
    static std::shared_ptr<sw::redis::Redis> create(const std::string &host,const uint16_t port,const int db=0,bool keepAlive=true)
    {
        sw::redis::ConnectionOptions connectionOptions;
        connectionOptions.host = host;  // Redis服务器地址
        connectionOptions.port = port;  // Redis服务器端口
        connectionOptions.db = db;  // Redis数据库
        connectionOptions.keep_alive = keepAlive; // 是否启用TCP KeepAlive机制

        try {
            return std::make_shared<sw::redis::Redis>(connectionOptions);
        } catch (const std::exception &e) {
            LOG_ERROR("创建Redis客户端失败,错误信息:{}",e.what());
            return nullptr;
        }
    }
};

class Session
{
public:
    Session(const std::shared_ptr<sw::redis::Redis> &client):
        _client(client)
    {}

    void add(const std::string &sessionId,const std::string &userId)
    {
       _client->set(sessionId,userId);
    }

    void del(const std::string &sessionId)
    {
        _client->del(sessionId);
    }

    sw::redis::OptionalString userId(const std::string &sessionId)
    {
        return _client->get(sessionId);
    }

private:
    std::shared_ptr<sw::redis::Redis> _client;
};


class LoginStatus 
{
public:
    LoginStatus(const std::shared_ptr<sw::redis::Redis> &client):
        _client(client)
    {}

    void add(const std::string &userId)
    {
        _client->set(userId,"");
    }

    void del(const std::string &userId)
    {
        _client->del(userId);
    }

    bool exist(const std::string &userId)
    {
        return _client->exists(userId);
    }

private:
    std::shared_ptr<sw::redis::Redis> _client;
};

class VerificationCode
{
public:
    VerificationCode(const std::shared_ptr<sw::redis::Redis> &client):
        _client(client)
    {}

    void add(const std::string &codeId,const std::string &code,const std::chrono::milliseconds ttl=std::chrono::milliseconds(60000))
    {
        _client->set(codeId,code,ttl);
    }

    void del(const std::string &codeId)
    {
        _client->del(codeId);
    }

    sw::redis::OptionalString vcode(const std::string &codeId)
    {
        return _client->get(codeId);
    }
    
private:
    std::shared_ptr<sw::redis::Redis> _client;
};



}