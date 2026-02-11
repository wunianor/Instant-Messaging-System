#include "../../common/mysql-user.hpp"
#include "../../common/odbFactory.hpp"
#include <memory>



using namespace imserver;


void insert(UserTable &userTable)
{
    if(userTable.insert(std::make_shared<User>("1","aaa","123456")))
    {
        std::cout<<"插入用户成功,用户id=1!"<<std::endl;
    }
    else std::cout<<"插入用户失败,用户id=1!"<<std::endl;

    if(userTable.insert(std::make_shared<User>("2","18877779999")))
    {
        std::cout<<"插入用户成功,用户id=2!"<<std::endl;
    }
    else std::cout<<"插入用户失败,用户id=2!"<<std::endl;
}

void selectByuseIdAndUpdate(UserTable &userTable)
{
    auto user=userTable.selectByUserId("2");
    std::cout<<"通过userId查询用户成功,用户id="<<user->userId()<<",昵称="<<user->nickname()<<",手机号="<<user->phone()<<std::endl;
    user->nickname("我是2");
    if(userTable.update(user))
    {
        std::cout<<"更新用户成功!"<<std::endl;
    }
    else std::cout<<"更新用户失败!"<<std::endl;
}

void selectByNickname(UserTable &userTable)
{
    auto user=userTable.selectByNickname("aaa");
    std::cout<<"通过nickname查询用户成功,用户id="<<user->userId()<<",昵称="<<user->nickname()<<std::endl;
}

void selectByPhone(UserTable &userTable)
{
    auto user=userTable.selectByPhone("18877779999");
    std::cout<<"通过phone查询用户成功,用户id="<<user->userId()<<",昵称="<<user->nickname()<<",手机号="<<user->phone()<<std::endl;
}


void selectByUserIds(UserTable &userTable)
{
    std::vector<std::string> userIds={"1","2"};
    auto users=userTable.selectMutilUserByUserId(userIds);
    std::cout<<"通过userId批量查询用户成功,查询到用户数量="<<users.size()<<std::endl;
    for(const auto &user:users)
    {
        std::cout<<"用户id="<<user.userId()<<",昵称="<<user.nickname()<<",手机号="<<user.phone()<<std::endl;
    }
}


int main()
{
    initGlogger(false);

    auto db=ODBFactory::create("root", "123456", "127.0.0.1", "im", "utf8", 0, 1);

    UserTable userTable(db);

    insert(userTable);
    selectByuseIdAndUpdate(userTable);
    selectByNickname(userTable);
    selectByPhone(userTable);
    selectByUserIds(userTable);

    return 0;
}