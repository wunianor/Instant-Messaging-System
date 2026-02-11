#pragma once
#include <string>
#include <cstddef> 
#include <odb/nullable.hxx>
#include <odb/core.hxx>

namespace imserver
{

//odb -d mysql --std c++11 --generate-query --generate-schema --profile boost/date-time .hxx文件路径
#pragma db object table("user")
class User
{
public:
    //一定要有默认构造
    User()=default;

    User(const std::string &userId,const std::string &nickname,const std::string &password):
        _userId(userId),
        _nickname(nickname),
        _password(password)
    {}

    User(const std::string &userId,const std::string &phone):
        _userId(userId),
        _nickname(userId),
        _phone(phone)
    {}

    std::string userId() const {return _userId;}
    void userId(const std::string &val) {_userId=val;}

    std::string nickname() const
    {
        if(!_nickname) return std::string();
        return *_nickname;
    }
    void nickname(const std::string &val) {_nickname=val;}

    std::string description() const
    {
        if(!_description) return std::string();
        return *_description;
    }
    void description(const std::string &val) {_description=val;}

    std::string password() const
    {
        if(!_password) return std::string();   
        return *_password; 
    }
    void password(const std::string &val) {_password=val;}

    std::string phone() const
    {
        if(!_phone) return std::string();
        return *_phone;
    }
    void phone(const std::string &val) {_phone=val;}

    std::string avatarId() const
    {
        if(!_avatarId) return std::string();
        return *_avatarId;
    }
    void avatarId(const std::string &val) {_avatarId=val;}
 

private:
    friend class odb::access;

    /// @brief 自动生成的主键id
    #pragma db id auto
    unsigned long _id;

    /// @brief 用户id
    #pragma db type("varchar(64)") index unique
    std::string _userId;

    /// @brief 用户昵称
    #pragma db type("varchar(64)") index unique
    odb::nullable<std::string> _nickname;

    /// @brief 用户签名
    #pragma db type("varchar(64)") 
    odb::nullable<std::string> _description;

    /// @brief 用户密码
    #pragma db type("varchar(64)")
    odb::nullable<std::string> _password;

    /// @brief 用户手机号
    #pragma db type("varchar(64)") index unique
    odb::nullable<std::string> _phone;

    /// @brief 用户头像id
    #pragma db type("varchar(64)")
    odb::nullable<std::string> _avatarId;
};




}