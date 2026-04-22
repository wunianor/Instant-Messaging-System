#pragma once
#include <odb/forward.hxx>
#include <string>
#include <cstddef> 
#include <odb/core.hxx>

namespace imserver
{

//odb -d mysql --std c++17 --generate-query --generate-schema --profile boost/date-time .hxx文件路径
#pragma db object table("chat_session_member")
class ChatSessionMember
{
public:
    //要有默认构造
    ChatSessionMember()=default;

    ChatSessionMember(const std::string &sessionId,const std::string &userId):
        _session_id(sessionId),
        _user_id(userId)
    {}

    std::string sessionId() {return _session_id;}
    void sessionId(const std::string &val) {_session_id=val;}

    std::string userId() {return _user_id;}
    void userId(const std::string & val) {_user_id=val;}


private:
    friend class odb::access;

    #pragma db id auto
    unsigned long _id;

    #pragma db type("varchar(64)") index
    std::string _session_id;

    #pragma db type("varchar(64)")
    std::string _user_id;
};
    
}
