#pragma once

#include <etcd/Client.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/Response.hpp>
#include <etcd/Value.hpp>
#include <etcd/Watcher.hpp>

#include "log.hpp"

/// @brief 服务注册类
class ServiceRegister
{
public:
    /// @brief 构造函数
    /// @param addr etcd服务器ip:port
    /// @param ttl  租约生存时间
    ServiceRegister(const string &addr,int ttl):
        _client(std::make_shared<etcd::Client>(addr)),
        _keepAlive(_client->leasekeepalive(ttl).get()),
        _leaseId(_keepAlive->Lease())
    {}

    /// @brief 注册服务
    /// @param key 服务的键 
    /// @param value 服务的值
    /// @return 注册服务成功,返回true;否则返回false
    bool registerService(const std::string &key,const std::string &value)
    {
        auto rsp=_client->put(key,value,_leaseId).get();
        if(rsp.is_ok()==false)
        {
            //注册服务失败日志打印
            LOG_ERROR("注册{}-{}服务失败",key,value);
            return false;
        }
        LOG_INFO("注册{}-{}服务成功",key,value);
        return true;
    }

private:
    /// @brief etcd客户端
    std::shared_ptr<etcd::Client> _client;
    
    /// @brief 保活对象
    std::shared_ptr<etcd::KeepAlive> _keepAlive;

    /// @brief 租约号
    int64_t _leaseId;
};


/// @brief 服务发现类
class ServiceDiscover
{
public:
    using NotifyCallback_t = std::function<void(const std::string &,const std::string &)>;

    /// @brief 构造函数
    /// @param addr etcd服务器ip:port
    /// @param baseDir 服务根目录
    /// @param putCb 新增/修改服务回调函数
    /// @param delCb 删除服务回调函数
    ServiceDiscover(const std::string &addr,
                    const std::string &baseDir,
                    const NotifyCallback_t &putCb,
                    const NotifyCallback_t &delCb):
        _client(std::make_shared<etcd::Client>(addr)),
        _watcher(std::make_shared<etcd::Watcher>(*(_client.get()),
                                                   baseDir,
                                                   std::bind(&ServiceDiscover::watcherCallback,this,std::placeholders::_1),
                                                   true)),
        _putCb(putCb),
        _delCb(delCb)

    {
        //发现已有服务
        discoverService(baseDir);
    }

    /// @brief 析构函数
    ~ServiceDiscover()
    {
        //取消事件监控
        _watcher->Cancel();
    }

private:
    /// @brief 发现已有服务
    /// @param baseDir 服务的根目录
    void discoverService(const string &baseDir)
    {
        //获取baseDir下的所有服务
        auto rsp=_client->ls(baseDir).get();

        //如果响应失败
        if(rsp.is_ok()==false)
        {
            LOG_ERROR("获取baseDir下的所有服务失败");
            return ;
        }

        //获取baseDir下的服务数量
        int serviceCount=rsp.keys().size();

        //遍历baseDir下的所有服务,并调用_putCb()回调
        for(int i=0;i<serviceCount;++i)
        {
            if(_putCb) _putCb(rsp.key(i),rsp.value(i).as_string());
        }
    }

    /// @brief watcher回调函数
    /// @param rsp watcher的响应结果
    void watcherCallback(const etcd::Response &rsp)
    {
        //如果响应失败
        if(rsp.is_ok()==false)
        {
            LOG_ERROR("收到错误的事件通知");
            return ;
        }

        //遍历每一个事件
        for(const auto &event:rsp.events())
        {
            //如果事件是 新增/修改服务键值对(服务上线)
            if(event.event_type() == etcd::Event::EventType::PUT)
            {
                if(_putCb) _putCb(event.kv().key(),event.kv().as_string());
            }
            //如果事件是 删除服务键值对(服务下线)
            else if(event.event_type() == etcd::Event::EventType::DELETE_)
            {
                if(_delCb) _delCb(event.prev_kv().key(),event.prev_kv().as_string());
            }
        }
    }

private:
    /// @brief etcd客户端
    std::shared_ptr<etcd::Client> _client;

    /// @brief 异步键值对变更监听对象
    std::shared_ptr<etcd::Watcher> _watcher;

    /// @brief 新增/修改服务(键值对)回调函数
    NotifyCallback_t _putCb;

    /// @brief 删除服务(键值对)回调函数
    NotifyCallback_t _delCb;
};