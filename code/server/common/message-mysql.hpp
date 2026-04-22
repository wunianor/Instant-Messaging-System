#pragma once

#include <algorithm>
#include <boost/date_time/posix_time/time_formatters.hpp>
#include <exception>
#include <memory>
#include <sstream>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <odb/core.hxx>
#include <odb/forward.hxx>
#include <odb/result.hxx>
#include <odb/database.hxx>       // odb::database 完整定义
#include <odb/transaction.hxx>    // odb::transaction 完整定义
#include <odb/mysql/database.hxx> // MySQL数据库的具体实现
#include <odb/mysql/transaction.hxx> // MySQL事务兼容具体实现
#include "../odb/message.hxx"
#include "message-odb.hxx"
#include "log.hpp"


namespace imserver
{

class MessageTable
{
public:
    MessageTable(const std::shared_ptr<odb::core::database> &db):
        _db(db)
    {}

    /// @brief 新增一条消息到 message 表
    /// @param message 需要持久化的消息对象
    /// @return 成功返回 true,失败返回 false
    bool insert(Message message)
    {
        try
        {
            odb::transaction trans(_db->begin());
            _db->persist(message);
            trans.commit();
        }
        catch(const std::exception &e)
        {
            LOG_ERROR("新增消息失败,消息id:{},报错信息:{}!",message.messageId(),e.what());
            return false;
        }
        return true;
    }

    /// @brief 删除某个聊天会话下的所有消息
    /// @param chat_session_id 聊天会话ID
    /// @return 成功返回 true,失败返回 false
    bool remove(const std::string &chat_session_id)
    {
        try
        {
            odb::transaction trans(_db->begin());
            _db->erase_query<Message>(odb::query<Message>::chat_session_id == chat_session_id);
            trans.commit();
        }
        catch(const std::exception &e)
        {
            LOG_ERROR("删除聊天会话所有消息失败,聊天会话id:{},报错信息:{}!",chat_session_id,e.what());
            return false;
        }
        return true;
    }
    /// @brief 查询某个聊天会话最近 n 条消息(按时间倒序)
    /// @param chat_session_id 聊天会话ID
    /// @param n 需要返回的消息数量
    /// @return 最近 n 条消息,查询失败返回空数组
    std::vector<Message> recent(const std::string &chat_session_id,size_t n)
    {
        std::vector<Message> ret;
        if(n == 0) return ret;

        try
        {
            odb::transaction trans(_db->begin());
            // 直接下推到数据库进行排序与分页,避免全量数据回表后再裁剪
            std::ostringstream oss;
            oss << "chat_session_id='" << chat_session_id << "' "
                << "ORDER BY create_time DESC "
                << "LIMIT " << n;

            odb::result<Message> queryRet(_db->query<Message>(oss.str()));
            for(auto iter=queryRet.begin();iter!=queryRet.end();++iter)
            {
                ret.push_back(*iter);
            }

            trans.commit();
        }
        catch(const std::exception &e)
        {
            LOG_ERROR("查询最近消息失败,聊天会话id:{},n:{},报错信息:{}!",chat_session_id,n,e.what());
            ret.clear();
        }

        return ret;
    }
    /// @brief 查询某个聊天会话在指定时间区间内的消息
    /// @param chat_session_id 聊天会话ID
    /// @param start_time 区间开始时间(含)
    /// @param end_time 区间结束时间(含)
    /// @return 时间区间内的消息,按时间升序返回
    std::vector<Message> range(
        const std::string &chat_session_id,
        const boost::posix_time::ptime &start_time,
        const boost::posix_time::ptime &end_time
    )
    {
        std::vector<Message> ret;
        if(end_time < start_time) return ret;

        try
        {
            odb::transaction trans(_db->begin());
            auto queryRet=_db->query<Message>(
                odb::query<Message>::chat_session_id == chat_session_id &&
                odb::query<Message>::create_time >= start_time &&
                odb::query<Message>::create_time <= end_time
            );

            for(const auto &msg:queryRet)
            {
                ret.push_back(msg);
            }
            trans.commit();

            // 区间消息按时间升序返回,便于前端从旧到新渲染
            std::sort(ret.begin(),ret.end(),[](const Message &lhs,const Message &rhs){
                return lhs.createTime() < rhs.createTime();
            });
        }
        catch(const std::exception &e)
        {
            LOG_ERROR("查询时间区间消息失败,聊天会话id:{},时间区间:{}-{},报错信息:{}!",
                chat_session_id,
                boost::posix_time::to_simple_string(start_time),
                boost::posix_time::to_simple_string(end_time),
                e.what());
            ret.clear();
        }

        return ret;
    }


private:
    std::shared_ptr<odb::core::database> _db;
};


}