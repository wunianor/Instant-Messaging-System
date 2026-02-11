#pragma once


#include <exception>
#include <memory>
#include <sstream>
#include <odb/core.hxx>
#include <odb/forward.hxx>
#include <odb/result.hxx>
#include <odb/database.hxx>       // odb::database 完整定义
#include <odb/transaction.hxx>    // odb::transaction 完整定义
#include <odb/mysql/database.hxx> // MySQL数据库的具体实现
#include <odb/mysql/transaction.hxx> // MySQL事务兼容具体实现
#include "../odb/user.hxx"
#include "../odb/testUser/user-odb.hxx"
#include "log.hpp"

namespace imserver
{

class UserTable
{
public:
    UserTable(const std::shared_ptr<odb::core::database> &db):
        _db(db)
    {}

    bool insert(User &user)
    {
         try {
            odb::transaction trans(_db->begin());
            _db->persist(user);
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("新增用户失败,用户昵称:{},错误信息:{}",user.nickname(),e.what());
            return false;
        }

        return true;
    }

    bool insert(const std::shared_ptr<User> &user)
    {
        try {
            odb::transaction trans(_db->begin());
            _db->persist(*user);
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("新增用户失败,用户昵称:{},错误信息:{}",user->nickname(),e.what());
            return false;
        }

        return true;
    }


    bool update(User &user)
    {
        return update(std::make_shared<User>(user));
    }

    bool update(const std::shared_ptr<User> &user)
    {
        try 
        {
            odb::transaction trans(_db->begin());
            _db->update(*user);
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("更新用户失败,用户昵称:{},错误信息:{}",user->nickname(),e.what());
            return false;
        }   
        return true;
    }

    std::shared_ptr<User> selectByUserId(const std::string &userId)
    {
        std::shared_ptr<User> ret;
        try {
            odb::transaction trans(_db->begin());
            ret.reset(_db->query_one<User>(odb::query<User>::userId == userId));
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("使用用户id查询用户失败,userId:{},错误信息:{}",userId,e.what());
        }
        return ret;
    }

    std::shared_ptr<User> selectByNickname(const std::string &nickname)
    {
        std::shared_ptr<User> ret;
        try {
            odb::transaction trans(_db->begin());
            ret.reset(_db->query_one<User>(odb::query<User>::nickname == nickname));
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("使用nickname查询用户失败,nickname:{},错误信息:{}",nickname,e.what());
        }
        return ret;
    }

    std::shared_ptr<User> selectByPhone(const std::string &phone)
    {
        std::shared_ptr<User> ret;
        try {
            odb::transaction trans(_db->begin());
            ret.reset(_db->query_one<User>(odb::query<User>::phone == phone));
            trans.commit();
        } catch (const std::exception &e) {
            LOG_ERROR("使用phone查询用户失败,phone:{},错误信息:{}",phone,e.what());
        }
        return ret;
    }

    std::vector<User> selectMutilUserByUserId(const std::vector<std::string> &userIds)
    {
        if(userIds.empty())
        {
            return std::vector<User>();
        }

        std::vector<User> ret;
        try {
            odb::transaction trans(_db->begin());

            //组织查询表达式
            std::ostringstream oss;
            oss<<"userId in (";
            for(const auto &userId:userIds)
            {   
                oss<<"'"<< userId<<"',";
            }
            std::string condition=oss.str();
            condition.pop_back();
            condition += ')';

            odb::result<User> r(_db->query<User>(condition));
            
            for(auto it=r.begin(); it != r.end() ; ++it)
            {
                ret.push_back(*it);
            }
            trans.commit();

        } catch (const std::exception &e) {
            LOG_ERROR("userId批量查询用户失败,错误消息:{}",e.what());
            ret.clear();
        }

        return ret;
    }


private:
    std::shared_ptr<odb::core::database> _db;
};


}