#pragma once

#include <string>
#include <cstddef>
#include <odb/nullable.hxx>

namespace imserver {

// //odb -d mysql --std c++11 --generate-query --generate-schema --profile boost/date-time 源文件

#pragma db object table("user_friend_relation")
class UserFriendRelation
{
public:
    UserFriendRelation() = default;

    UserFriendRelation(
        const std::string &user_id,
        const std::string &peer_id
    ):
        _user_id(user_id),
        _peer_id(peer_id)
    {}

    std::string userId() {return _user_id;}
    void userId(const std::string &val) {_user_id=val;}

    std::string peerId() {return _peer_id;}
    void peerId(const std::string &val) {_peer_id=val;}

private:
    friend class odb::access;

    //自增id
    #pragma db id auto
    unsigned long _id;

    //用户id
    #pragma db type("varchar(64)") index
    std::string _user_id;

    //用户好友id
    #pragma db type("varchar(64)")
    std::string _peer_id;
};

}