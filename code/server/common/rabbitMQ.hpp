#pragma once

#include <ev.h>
#include <amqpcpp.h>
#include <amqpcpp/libev.h>
#include <openssl/ssl.h>
#include <openssl/opensslv.h>

#include <thread>

#include "log.hpp"

//初始化过程：
// //实例化底层网络通信框架的IO事件监控句柄
// auto *loop=EV_DEFAULT;

// //实例化LibEvHandler句柄---将AMQP框架与事件监控关联起来
// AMQP::LibEvHandler handler(loop);

// //实例化连接对象
// AMQP::Address address("amqp://root:123456@127.0.0.1:5672/");
// AMQP::TcpConnection connection(&handler,address);

// //实例化信道对象
// AMQP::TcpChannel channel(&connection);

namespace imserver
{

#define DEFAULT_EXCHANGE_TYPE AMQP::ExchangeType::direct

/// @brief rabbitMQ客户端类
class RMQClient
{
public:
    using MessageCallback_t = std::function<void(const char *,uint64_t)>;
public:
    /// @brief RMQ客户端构造函数
    /// @param user 用户名
    /// @param password 密码
    /// @param ip RMQ服务端ipv4地址
    /// @param port RMQ服务端端口
    RMQClient(const std::string &user,
              const std::string &password,
              const std::string &ip,
              const uint16_t port):
        _loop(EV_DEFAULT),
        _handler(_loop),
        _address("amqp://"+user+":"+password+"@"+ip+":"+std::to_string(port)+"/"),
        _connection(&_handler,_address),
        _channel(&_connection),
        _loopThread([this]()
        {
            ev_run(_loop);
        })
    {}

    /// @brief 声明队列和交换机,并将其绑定在一起
    /// @param queueName 队列名字
    /// @param routingKey 队列对应的routingKey
    /// @param exchangeName 交换机名称
    /// @param exchangeType 交换机类型
    void bindQueueAndExchange(const std::string &queueName,
                              const std::string &routingKey,
                              const std::string &exchangeName,
                              const AMQP::ExchangeType exchangeType=DEFAULT_EXCHANGE_TYPE)
    {
        //声明交换机
        _channel.declareExchange(exchangeName,exchangeType)
            .onError([exchangeName](const char *message)
            {
                LOG_ERROR("声明{}失败:{}",exchangeName,message);
                exit(-1);
            })
            .onSuccess([exchangeName]()
            {
                LOG_INFO("声明{}成功",exchangeName);
            });

        
        //声明队列
        _channel.declareQueue(queueName)
            .onError([queueName](const char *message)
            {
                LOG_ERROR("声明{}失败:{}",queueName,message);
                exit(-1);
            })
            .onSuccess([queueName]()
            {
                LOG_INFO("声明{}成功",queueName);
            });

        //将队列与交换机绑定
        _channel.bindQueue(exchangeName,queueName,routingKey)
            .onError([exchangeName,queueName](const char *message)
            {
                LOG_ERROR("{}绑定{}失败:{}",exchangeName,queueName,message);
                exit(-1);
            })
            .onSuccess([exchangeName,queueName]()
            {
                LOG_INFO("{}绑定{}成功",exchangeName,queueName);
            });
    }

    /// @brief 向指定交换机上指定的routingKey对应的队列发送消息
    /// @param exchangeName 交换机名字
    /// @param routingKey 队列的routingKey
    /// @param message 消息内容
    bool publish(const std::string &exchangeName,
                 const std::string &routingKey,
                 const std::string &message)
    {
        if(_channel.publish(exchangeName,routingKey,message)==false)
        {
            LOG_ERROR("{} publish 消息失败,routingKey:{},message:{}",exchangeName,routingKey,message);
            return false;
        }

        return true;
    }

    /// @brief 对订阅的队列内的消息通过回调函数进行处理
    /// @param queueName 队列名
    /// @param cb 处理消息的回调函数
    void consume(const std::string &queueName,const MessageCallback_t &cb)
    {
        _channel.consume(queueName)
            .onReceived([this,cb](const AMQP::Message &message, uint64_t deliveryTag, bool redelivered)
            {
                //处理消息
                cb(message.body(),message.bodySize());

                //确认已经收到该消息
                _channel.ack(deliveryTag);
            })
            .onError([queueName](const char *message)
            {
                LOG_ERROR("{} consume 失败:{}",queueName,message);
                exit(-1);
            });
    }

    /// @brief 析构函数,异步终止该线程
    ~RMQClient()
    {
        //定义 libev 异步事件监视器
        struct ev_async asyncWatcher;
        //初始化异步监视器，绑定触发时的回调函数
        ev_async_init(&asyncWatcher,watcherCallback);
        //将异步监视器注册到 libev 事件循环中
        ev_async_start(_loop,&asyncWatcher);
        //发送异步事件，触发监视器（跨线程通知事件循环）
        ev_async_send(_loop,&asyncWatcher);

        //阻塞等待事件循环所在的子线程执行完毕
        _loopThread.join();
    }

private:
    /// @brief 异步监视器的触发回调函数
    static void watcherCallback(struct ev_loop *loop,ev_async *watcher,int32_t revents)
    {
        //终止指定的 libev 事件循环
        ev_break(loop,EVBREAK_ALL);
    }

private:
    /// @brief 底层网络通信框架的IO事件监控句柄
    struct ev_loop *_loop;

    /// @brief LibEvHandler句柄
    AMQP::LibEvHandler _handler;

    AMQP::Address _address;

    /// @brief 连接对象
    AMQP::TcpConnection _connection;

    /// @brief 通信信道
    AMQP::TcpChannel _channel;

    /// @brief 该RMQ客户端对应的事件循环线程
    std::thread _loopThread;
};


}