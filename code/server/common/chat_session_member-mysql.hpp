#pragma once

#include <exception>
#include <memory>
#include <odb/query.hxx>
#include <sstream>
#include <odb/core.hxx>
#include <odb/forward.hxx>
#include <odb/result.hxx>
#include <odb/database.hxx>       // odb::database 完整定义
#include <odb/transaction.hxx>    // odb::transaction 完整定义
#include <odb/mysql/database.hxx> // MySQL数据库的具体实现
#include <odb/mysql/transaction.hxx> // MySQL事务兼容具体实现
#include "../odb/chat-session-member.hxx"
#include "chat-session-member-odb.hxx"
#include "log.hpp"

namespace imserver
{

class ChatSessionMemberTable
{
public:
    ChatSessionMemberTable(const std::shared_ptr<odb::core::database> &db):
        _db(db)
    {}

    //添加单个聊天会话成员
    bool addSessionMember(ChatSessionMember &csm)
    {
        try {
            odb::transaction trans(_db->begin());
            _db->persist(csm);
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("新增会话成员失败,会话id:{},昵称id:{},错误信息:{}",csm.sessionId(),csm.userId(),e.what());
            return false;
        }
        return true;
    }

    //一次添加多个会话成员
    bool addMultiSessionMember(std::vector<ChatSessionMember> &csms)
    {
        if(csms.empty()) return true;

        try {
            odb::transaction trans(_db->begin());
            for(auto &csm:csms)
            {
                _db->persist(csm);
            }
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("新增会话成员失败,会话id:{},错误信息:{}",csms[0].sessionId(),e.what());
            return false;
        }
        return true;
    }

    //从聊天会话中删除一个人
    bool delSessionMember(ChatSessionMember &csm)
    {
        try {
            odb::transaction trans(_db->begin());
            //根据条件删除
            _db->erase_query<ChatSessionMember>(
                odb::query<ChatSessionMember>::session_id == csm.sessionId() &&
                odb::query<ChatSessionMember>::user_id  == csm.userId()
            );
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("删除会话成员失败,会话id:{},用户id:{},错误信息:{}",csm.sessionId(),csm.userId(),e.what());
            return false;
        }
        return true;
    }

    //删除一个聊天会话
    bool delSession(const std::string &sessionId)
    {
        try {
            odb::transaction trans(_db->begin());
            //根据条件删除
            _db->erase_query<ChatSessionMember>(
                odb::query<ChatSessionMember>::session_id == sessionId
            );
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("删除会话失败,聊天会话id:{},错误信息:{}",sessionId,e.what());
            return false;
        }
        return true;
    }

    //返回该聊天会话内所有的用户的id
    std::vector<std::string> members(const std::string &sessionId)
    {
        std::vector<std::string> ret;

        try {
            odb::transaction trans(_db->begin());
            auto queryRet=_db->query<ChatSessionMember>(odb::query<ChatSessionMember>::session_id == sessionId);
            for(auto &e:queryRet)
            {
                ret.push_back(e.userId());
            }
            trans.commit();

        } catch (const std::exception &e) {
            LOG_ERROR("查询会话所有成员失败,聊天会话id:{},错误信息:{}",sessionId,e.what());
            ret.clear();
        }
        return ret;
    }

private:
    std::shared_ptr<odb::core::database> _db;
};

}