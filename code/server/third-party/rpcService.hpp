#pragma once

#include <brpc/channel.h>
#include <iostream>
#include <vector>

#include "log.hpp"


namespace imserver
{

/// @brief RPC服务信道集合类,保存某个RPC服务的所有主机节点的通信信道
class RPCServiceChannel
{
public:
    /// @brief 构造函数
    /// @param serviceName 服务名称
    RPCServiceChannel(const std::string &serviceName):
        _serviceName(serviceName), 
        _RRIndex(0)
    {}

    /// @brief 新增提供该服务的主机节点
    /// @param host 主机的ip:port
    void appendChannel(const std::string &host)
    {
        //创建通信信道对象
        auto channel=std::make_shared<brpc::Channel>();
        
        //通信信道参数配置对象
        brpc::ChannelOptions options;
        options.connect_timeout_ms = -1; //连接等待超时时间
        options.timeout_ms=-1;//rpc请求超时时间
        options.max_retry=3;//请求重试次数
        options.protocol="baidu_std";//序列化协议

        //初始化通信信道
        if(channel->Init(host.c_str(),&options)==-1)
        {
            LOG_ERROR("初始化{}服务-{}主机信道失败", _serviceName,host);
            return ;
        }

        {
            //将该主机节点的通信信道存储起来
            std::unique_lock<std::mutex> lock(_mutex);
            _channels.push_back(channel);
            _hostMapPtr[host]=channel;
        }
    }

    /// @brief 移除提供该服务的主机节点
    /// @param host 主机的ip:port
    void removeChannel(const std::string &host)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it=_hostMapPtr.find(host);//查找该主机节点的信道对象指针
        if(it!=_hostMapPtr.end())
        {
            //移除该主机节点的信道对象
            auto channel=it->second;
            _hostMapPtr.erase(it);

            for(auto iter=_channels.begin();iter!=_channels.end();++iter)
            {
                if((*iter)==channel)
                {
                    _channels.erase(iter);
                    break;
                }
            }
        }
        else
        {
            LOG_WARN("删除{}服务-{}主机信道失败,未找到该主机信道", _serviceName,host);
        }
    }

    /// @brief 获取提供该服务的主机节点信道
    /// @return 返回提供该服务的主机节点信道
    std::shared_ptr<brpc::Channel> getChannel()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if(_channels.size()==0)
        {
            LOG_ERROR("获取{}服务信道失败,当前无可用信道", _serviceName);
            return nullptr;
        }

        auto channel=_channels[_RRIndex];

        //采用RR轮转进行负载均衡
        _RRIndex=(_RRIndex+1)%_channels.size();

        return channel;
    }

private:
    /// @brief 保护_channels和_hostMapPtr的互斥锁
    std::mutex _mutex;

    /// @brief 服务姓名
    std::string _serviceName;

    /// @brief RR轮转指针
    size_t _RRIndex;

    /// @brief 保存提供该服务的主机节点的信道对象指针
    std::vector<std::shared_ptr<brpc::Channel>> _channels;

    /// @brief 主机ip:port 映射 该主机的信道对象指针
    std::unordered_map<std::string, std::shared_ptr<brpc::Channel>> _hostMapPtr;
};


/// @brief RPC服务管理类,保存所有的RPC服务
class RPCServiceManager
{
public:
    /// @brief 新增关心的服务
    /// @param serviceName 服务名称
    void followService(const std::string &serviceName)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _followServices.insert(serviceName);
    }

    /// @brief 根据 服务名称 获取 提供该服务的主机信道
    /// @param serviceName 服务名称
    /// @return 返回提供该服务的主机信道
    std::shared_ptr<brpc::Channel> getChannel(const std::string &serviceName)
    {
        std::unique_lock<std::mutex> lock(_mutex);

        //根据 服务名称 寻找 提供该服务的主机信道
        auto it = _serviceNameMapService.find(serviceName);
        if (it != _serviceNameMapService.end())
        {
            return it->second->getChannel();
        }
        else //没有返回nullptr
        {
            LOG_ERROR("没有可以提供{}服务的节点", serviceName);
            return nullptr;
        }
    }

    /// @brief 服务主机上线的回调函数
    /// @param serviceName 服务/主机 名
    /// @param host 提供该服务的主机的ip:port
    void serviceOnline(const std::string &serviceHostName, const std::string &host)
    {
        std::string serviceName=getServiceName(serviceHostName);
        std::shared_ptr<RPCServiceChannel> service;

        {
            std::unique_lock<std::mutex> lock(_mutex);

            //如果上线的服务不是受关心的服务,
            //直接返回即可
            auto followIt=_followServices.find(serviceName);
            if(followIt == _followServices.end())
            {
                LOG_DEBUG("不关心的{}服务上线了,主机为{}",serviceName,host);
                return ;
            }

            //获取上线服务的信道集合对象service
            auto it = _serviceNameMapService.find(serviceName);
            if (it == _serviceNameMapService.end()) 
            {
                service = std::make_shared<RPCServiceChannel>(serviceName);
                _serviceNameMapService[serviceName] = service;
            }
            else
            {
                service=it->second;
            }
        }

        //往服务的信道集合内新增信道
        service->appendChannel(host);

        LOG_DEBUG("{}服务上线了,主机为{}",serviceName,host);
    }

    /// @brief 服务主机下线的回调函数
    /// @param serviceName 服务/主机 名
    /// @param host 主机ip:port
    void serviceOffline(const std::string &serviceHostName, const std::string &host)
    {
        std::string serviceName=getServiceName(serviceHostName);
        std::unique_lock<std::mutex> lock(_mutex);

        //如果该服务不是受关心的服务,直接返回即可
        auto followIt=_followServices.find(serviceName);
        if(followIt == _followServices.end())
        {
            LOG_DEBUG("不关心的{}服务下线了,主机为{}",serviceName,host);
            return ;
        }

        //移除提供该服务的host主机的相关管理信息
        auto it = _serviceNameMapService.find(serviceName);
        if (it != _serviceNameMapService.end())
        {
            it->second->removeChannel(host);
        }
        LOG_DEBUG("{}服务下线了,主机为{}",serviceName,host);
    }

private:
    
    /// @brief 通过 服务/主机 名 获取 服务名
    /// @param 服务/主机 名
    /// @return 返回服务名,若服务不存在返回""
    std::string getServiceName(const std::string &serviceHostName)
    {
        size_t pos=serviceHostName.rfind('/');
        if(pos == std::string::npos)
        {
            return "";
        }

        return serviceHostName.substr(0,pos);
    }

private:
    /// @brief 互斥锁
    std::mutex _mutex;

    /// @brief 关心的服务的名称集合
    std::unordered_set<std::string> _followServices;

    /// @brief 服务姓名映射服务信道
    std::unordered_map<std::string, std::shared_ptr<RPCServiceChannel>> _serviceNameMapService;
};


}