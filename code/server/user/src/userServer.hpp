#pragma once

#include <brpc/controller.h>
#include <brpc/server.h>
#include <elasticlient/client.h>
#include <memory>
#include <odb/forward.hxx>
#include <optional>
#include <string>
#include <sw/redis++/redis++.h>
#include <sw/redis++/redis.h>
#include <sw/redis++/utils.h>

// #include "../../common/etcd.hpp"
// #include "../../common/log.hpp"
// #include "../../common/rpcService.hpp"
// #include "../../common/utils.hpp"

// #include "../../common/data-redis.hpp"
// #include "../../common/data-es.hpp"
// #include "../../common/mysql.hpp"
// #include "../../common/user-mysql.hpp"


#include "etcd.hpp"
#include "log.hpp"
#include "rpcService.hpp"
#include "utils.hpp"

#include "data-redis.hpp"
#include "data-es.hpp"
#include "mysql.hpp"
#include "user-mysql.hpp"

#include "base.pb.h"
#include "user.pb.h"
#include "file.pb.h"



namespace imserver{

class UserServiceImpl : public imserver::UserService
{
public:
    UserServiceImpl(
        const std::shared_ptr<sw::redis::Redis> &redisClient,
        const std::shared_ptr<odb::core::database> &mysqlClient,
        const std::shared_ptr<elasticlient::Client> &esClient,
        const std::shared_ptr<RPCServiceManager> &rpcsm,
        const std::string &fileServiceDir
    ):
        _sessionTable(std::make_shared<imserver::Session>(redisClient)),
        _loginStatusTable(std::make_shared<imserver::LoginStatus>(redisClient)),
        _verificationCodeTable(std::make_shared<imserver::VerificationCode>(redisClient)),
        _userTable(std::make_shared<imserver::UserTable>(mysqlClient)),
        _esUserTable(std::make_shared<imserver::ESUser>(esClient)),
        _rpcsm(rpcsm),
        _fileServiceDir(fileServiceDir)
    {
        _esUserTable->createIndex();
    }

    bool checkNickname(const std::string &nickname)
    {
        if (nickname.empty() || nickname.length() > 20) {
            return false;
        }
        for (char c : nickname) {
            if (!std::isalnum(c) && c != '_' && c != '-' && c != ' ') {
                return false;
            }
        }
        return true;
    }

    bool checkPassword(const std::string &password)
    {
        if (password.length() < 6 || password.length() > 20) {
            return false;
        }
        bool hasUpper = false, hasLower = false, hasDigit = false;
        for (char c : password) {
            if (std::isupper(c)) hasUpper = true;
            else if (std::islower(c)) hasLower = true;
            else if (std::isdigit(c)) hasDigit = true;
        }
        return hasUpper && hasLower && hasDigit;
    }

    bool checkPhone(const std::string &phone)
    {
        if (phone.length() != 11) {
            return false;
        }
        for (char c : phone) {
            if (!std::isdigit(c)) {
                return false;
            }
        }
        if (phone[0] != '1') {
            return false;
        }
        char secondChar = phone[1];
        if (secondChar < '3' || secondChar > '9') {
            return false;
        }
        return true;
    }


    virtual void UserNicknameRegister(::google::protobuf::RpcController* controller,
                       const ::imserver::UserNicknameRegisterRequest* request,
                       ::imserver::UserNicknameRegisterResponse* response,
                       ::google::protobuf::Closure* done)
    {
        //类似智能指针，在函数运行结束时需要确保调用done->run();
        //调用run()才表示服务器已经处理完该次响应
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        const std::string nickname=request->nickname();
        const std::string password=request->password();

        //判断昵称是否合法
        if(checkNickname(nickname)==false)
        {
            setErrorResponse("昵称不合法!");
            LOG_ERROR("新用户注册时昵称不合法!,request_id:{},nickname:{}",request->request_id(),nickname);
            return ;
        }

        //判断密码是否合法
        if(checkPassword(password)==false)
        {
            setErrorResponse("密码不合法!");
            LOG_ERROR("新用户注册时密码不合法!,request_id:{}",request->request_id());
            return ;
        }

        //判断昵称是否已存在
        if(_userTable->selectByNickname(nickname))
        {
            setErrorResponse("昵称已存在!");
            LOG_ERROR("新用户注册时昵称已存在!,request_id:{},nickname:{}",request->request_id(),nickname);
            return ;
        }

        std::string userId=uuid();//创建一个用户id
        User newUser(userId,nickname,password);
        
        //向数据库插入新用户
        if(_userTable->insert(newUser)==false)
        {
            setErrorResponse("数据库新增用户错误!");
            LOG_ERROR("数据库新增用户错误!,request_id:{},userId:{},nickname:{}",request->request_id(),userId,nickname);
            return ;
        }

        //向ES插入新用户
        if(_esUserTable->addData(userId, nickname,"", "", "") ==false)
        {
            setErrorResponse("ES新增用户错误!");
            LOG_ERROR("ES新增用户错误!,request_id:{},userId:{},nickname:{}",request->request_id(),userId,nickname);
            return ;
        }

        response->set_request_id(request->request_id());
        response->set_success(true);
    }

    virtual void UserPhoneRegister(::google::protobuf::RpcController* controller,
                        const ::imserver::UserPhoneRegisterRequest* request,
                        ::imserver::UserPhoneRegisterResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        const std::string phone=request->phone();
        const std::string vcodeId=request->verification_code_id();
        const std::string vcode=request->verification_code();

        //检查手机号是否合法
        if(checkPhone(phone)==false)
        {
            setErrorResponse("手机号不合法!");
            LOG_ERROR("新用户注册时手机号不合法!,request_id:{},phone:{}",request->request_id(),phone);
            return ;
        }

        //从redis中获取正确的验证码 与 用户输入的进行对比
        if(_verificationCodeTable->vcode(vcodeId) != vcode)
        {    
            setErrorResponse("验证码错误");
            LOG_ERROR("新用户注册时验证码错误!,request_id:{}",request->request_id());
            return ;
        }

        //通过数据库查询手机号是否注册
        if(_userTable->selectByPhone(phone))
        {
            setErrorResponse("该手机号已注册");
            LOG_ERROR("新用户注册时手机号已经注册!,request_id:{},phone:{}",request->request_id(),phone);
            return ;
        }

        const std::string userId=uuid();
        User user(userId,phone);

        //往数据库新增用户
        if(_userTable->insert(user) == false)
        {
            setErrorResponse("数据库新增用户错误!");
            LOG_ERROR("数据库新增用户错误!,request_id:{},userId:{},phone:{}",request->request_id(),userId,phone);
            return ;
        }


        //往ES新增用户
        if(_esUserTable->addData(userId,userId,"", phone, "") == false)
        {
            setErrorResponse("ES新增用户错误!");
            LOG_ERROR("ES新增用户错误!,request_id:{},userId:{},phone:{}",request->request_id(),userId,phone);
            return ;
        }

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
    }

    virtual void SendVerificationCode(::google::protobuf::RpcController* controller,
                        const ::imserver::SendVerificationCodeRequest* request,
                        ::imserver::SendVerificationCodeResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        const std::string phone=request->phone();

        //检查手机号是否合法
        if(checkPhone(phone)==false)
        {
            setErrorResponse("手机号不合法!");
            LOG_ERROR("发送验证码时手机号不合法!,request_id:{},phone:{}",request->request_id(),phone);
            return ;
        }

        const std::string vcodeId=uuid();
        const std::string vcode=vCode();
        
        //使用短信平台发送验证码
        //没有资质做不了了，只能打印了
        LOG_INFO("{}已发送验证码!,request_id:{},vcodeId:{},vcode:{}{}","\033[34m",request->request_id(),vcodeId,vcode,"\033[0m");

        //往redis新增验证码
        _verificationCodeTable->add(vcodeId,vcode);

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->set_verification_code_id(vcodeId);
    }
    virtual void UserNicknameLogin(::google::protobuf::RpcController* controller,
                        const ::imserver::UserNicknameLoginRequest* request,
                        ::imserver::UserNicknameLoginResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        const std::string nickname=request->nickname();
        const std::string password=request->password();

        //检查昵称是否存在 && 密码是否正确
        auto user=_userTable->selectByNickname(nickname);
        if(!user || user->password() != password)
        {
            setErrorResponse("该昵称不存在或密码错误");
            LOG_ERROR("用户昵称登录时该昵称不存在或密码错误!,request_id:{}",request->request_id());
            return ;
        }

        //检查用户是否已经登录
        if(_loginStatusTable->exist(user->userId()))
        {
            setErrorResponse("该用户已登录");
            LOG_ERROR("用户昵称登录时该用户已登录!,request_id:{}",request->request_id());
            return ;
        }

        //生成用户会话id,并添加到会话表
        const std::string sessionId=uuid();
        _sessionTable->add(sessionId,user->userId());
        //改变用户登录状态为登录态   
        _loginStatusTable->add(user->userId());
        
        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->set_session_id(sessionId);
    }

    virtual void UserPhoneLogin(::google::protobuf::RpcController* controller,
                        const ::imserver::UserPhoneLoginRequest* request,
                        ::imserver::UserPhoneLoginResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        const std::string phone=request->phone();
        const std::string vcodeId=request->verification_code_id();
        const std::string vcode=request->verification_code();

        //检查手机号是否合法
        if(checkPhone(phone)==false)
        {
            setErrorResponse("手机号不合法!");
            LOG_ERROR("手机验证码登陆时手机号不合法!,request_id:{},phone:{}",request->request_id(),phone);
            return ;
        }

        //查找该用户是否存在
        auto user=_userTable->selectByPhone(phone);
        if(!user)
        {
            setErrorResponse("该手机号未注册!");
            LOG_ERROR("手机验证码登陆时该手机号未注册!,request_id:{},phone:{}",request->request_id(),phone);
            return ;
        }

        //验证验证码是否正确
        if(_verificationCodeTable->vcode(vcodeId)!=vcode)
        {
            setErrorResponse("验证码输入错误");
            LOG_ERROR("手机验证码登陆时验证码输入错误!,request_id:{},vcodeId:{},vcode:{}",request->request_id(),vcodeId,vcode);
            return ;
        }
        _verificationCodeTable->del(vcodeId);
        //检查用户是否已经登录
        if(_loginStatusTable->exist(user->userId()))
        {
            setErrorResponse("该用户已登录");
            LOG_ERROR("用户手机号登录时该用户已登录!,request_id:{}",request->request_id());
            return ;
        }
        //生成用户会话id,并添加到会话表
        const std::string sessionId=uuid();
        _sessionTable->add(sessionId,user->userId());
        //改变用户登录状态为登录态   
        _loginStatusTable->add(user->userId());
        
        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->set_session_id(sessionId);
    }

    virtual void GetSingleUserInfo(::google::protobuf::RpcController* controller,
                        const ::imserver::GetSingleUserInfoRequest* request,
                        ::imserver::GetSingleUserInfoResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        //获取会话id和用户id
        const std::string session_id=request->session_id();
        std::string userId;
        if(!session_id.empty())
        {
            userId=_sessionTable->userId(session_id).value_or("");
        }
        else
        {
            userId=request->user_id();
        }

        //检查该用户是否存在
        auto user=_userTable->selectByUserId(userId);
        if(!user)
        {
            setErrorResponse("该用户不存在");
            LOG_ERROR("获取用户个人信息时该用户不存在!,request_id:{},userId:{}",request->request_id(),userId);
            return ;
        }

        //获取头像数据
        const std::string avatarId=user->avatarId();
        if(!avatarId.empty())
        {
            auto channel=_rpcsm->getChannel(_fileServiceDir);
            if(!channel)
            {
                setErrorResponse("获取文件服务通道失败");
                LOG_ERROR("获取用户个人信息时获取文件服务通道失败!,request_id:{}",request->request_id());
                return ;
            }

            FileService_Stub stub(channel.get());
            brpc::Controller cntl;
            GetSingleFileRequest req;
            GetSingleFileResponse rsp;
            req.set_request_id(request->request_id());
            req.set_file_id(avatarId);

            stub.GetSingleFile(&cntl,&req,&rsp,nullptr);

            if(cntl.Failed() || rsp.success()==false)
            {
                setErrorResponse("获取头像数据失败");
                LOG_ERROR("获取用户个人信息时获取头像数据失败!,request_id:{}",request->request_id());
                return ;
            }
            
            response->mutable_user_info()->set_avatar(rsp.file_data().file_content());
        }

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->mutable_user_info()->set_user_id(userId);
        response->mutable_user_info()->set_nickname(user->nickname());
        response->mutable_user_info()->set_description(user->description());
        response->mutable_user_info()->set_phone(user->phone());
    }
    virtual void GetMultiUserInfo(::google::protobuf::RpcController* controller,
                        const ::imserver::GetMultiUserInfoRequest* request,
                        ::imserver::GetMultiUserInfoResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        //获取请求中所有的用户id
        std::vector<std::string> userIds;
        for(int i=0;i<request->user_id_size();++i)
        {
            userIds.push_back(request->user_id(i));
        }        

        //查找userIds中对应的所有用户
        const auto users=_userTable->selectMutilUserByUserId(userIds);
        if(users.size() != userIds.size())
        {
            setErrorResponse("获取多个用户信息时,有用户不存在");
            LOG_ERROR("获取多个用户信息时,有用户不存在!,request_id:{}",request->request_id());
            return ;
        }

        //获取文件服务器信道
        auto channel=_rpcsm->getChannel(_fileServiceDir);
        if(!channel)
        {
            setErrorResponse("获取文件服务通道失败");
            LOG_ERROR("获取用户个人信息时获取文件服务通道失败!,request_id:{}",request->request_id());
            return ;
        }

        //获取多个用户头像数据
        FileService_Stub stub(channel.get());
        brpc::Controller cntl;        
        GetMultiFileRequest req;
        GetMultiFileResponse rsp;
        req.set_request_id(request->request_id());
        for(const auto &user:users)
        {
            req.add_file_id_list(user.avatarId());
            LOG_DEBUG("获取多个用户信息时的头像id,userId:{},avatarId:{},avatarId是否为空:{}",user.userId(),user.avatarId(),user.avatarId().empty());
        }
        stub.GetMultiFile(&cntl,&req,&rsp,nullptr);
        if(cntl.Failed() || rsp.success()==false)
        {
            setErrorResponse("获取多个用户头像数据失败");
            LOG_ERROR("获取多个用户头像数据失败!,request_id:{}",request->request_id());
            return ;
        }

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
        auto user_map=response->mutable_user_info_map();
        const auto &avatar_map=rsp.file_data_map();
        for(const auto &user:users)
        {
            UserInfo userInfo;
            userInfo.set_user_id(user.userId());
            userInfo.set_nickname(user.nickname());
            userInfo.set_description(user.description());
            userInfo.set_phone(user.phone());
            userInfo.set_avatar(avatar_map.at(user.avatarId()).file_content());
            user_map->insert({user.userId(),userInfo});
        }

    }
    virtual void SetUserNickname(::google::protobuf::RpcController* controller,
                        const ::imserver::SetUserNicknameRequest* request,
                        ::imserver::SetUserNicknameResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        //获取会话id和用户id
        const std::string session_id=request->session_id();
        std::string userId;
        if(!session_id.empty())
        {
            userId=_sessionTable->userId(session_id).value_or("");
        }
        else
        {
            userId=request->user_id();
        }

        const std::string newNickname=request->nickname();

        //检查新昵称格式是否正确
        if(checkNickname(newNickname)==false)
        {
            setErrorResponse("昵称格式错误");
            LOG_ERROR("设置用户昵称时,昵称格式错误!,request_id:{}",request->request_id());
            return ;
        }

        //检查用户是否存在
        auto user=_userTable->selectByUserId(userId);
        if(!user)
        {
            setErrorResponse("用户不存在");
            LOG_ERROR("设置用户昵称时,用户不存在!,request_id:{}",request->request_id());
            return ;
        }

        user->nickname(newNickname);

        //往数据库中更新数据
        if(_userTable->update(user) == false)
        {
            setErrorResponse("设置用户昵称时,数据库更新失败");
            LOG_ERROR("设置用户昵称时,数据库更新失败!,request_id:{}",request->request_id());
            return ;
        }

        //往es中更新数据
        if(_esUserTable->addData(
            user->userId(),
            user->nickname(),
            user->description(),
            user->phone(),
            user->avatarId()
        ) == false)
        {
            setErrorResponse("设置用户昵称时,ES数据更新失败");
            LOG_ERROR("设置用户昵称时,ES数据更新失败!,request_id:{}",request->request_id());

            return ;
        }

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
    }
    virtual void SetUserPassword(::google::protobuf::RpcController* controller,
                        const ::imserver::SetUserPasswordRequest* request,
                        ::imserver::SetUserPasswordResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        //获取会话id和用户id
        const std::string session_id=request->session_id();
        std::string userId;
        if(!session_id.empty())
        {
            userId=_sessionTable->userId(session_id).value_or("");
        }
        else
        {
            userId=request->user_id();
        }
        const std::string oldPassword=request->old_password();
        const std::string newPassword=request->new_password();

        //检查新密码格式
        if(checkPassword(newPassword)==false)
        {
            setErrorResponse("新密码格式错误");
            LOG_ERROR("修改用户密码时,新密码格式错误!,request_id:{}",request->request_id());
            return ;
        }

        //检查用户是否存在
        auto user=_userTable->selectByUserId(userId);
        if(!user)
        {
            setErrorResponse("用户不存在");
            LOG_ERROR("修改用户密码时,用户不存在!,request_id:{}",request->request_id());
            return ;
        }

        //检查旧密码是否正确
        if(oldPassword != user->password())
        {
            setErrorResponse("旧密码错误");
            LOG_ERROR("修改用户密码时,旧密码错误!,request_id:{}",request->request_id());
            return ;
        }

        user->password(newPassword);
        //往数据库中更新数据
        if(_userTable->update(user) == false)
        {
            setErrorResponse("修改用户密码时,数据库更新失败");
            LOG_ERROR("修改用户密码时,数据库更新失败!,request_id:{}",request->request_id());
            return ;
        }

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
    }
    virtual void SetUserPhone(::google::protobuf::RpcController* controller,
                        const ::imserver::SetUserPhoneRequest* request,
                        ::imserver::SetUserPhoneResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        };

        //获取会话id和用户id
        const std::string session_id=request->session_id();
        std::string userId;
        if(!session_id.empty())
        {
            userId=_sessionTable->userId(session_id).value_or("");
        }
        else
        {
            userId=request->user_id();
        }
        const std::string newPhone=request->phone();
        const std::string vcodeId=request->verification_code_id();
        const std::string vcode=request->verification_code();

        //检查新手机号格式是否正确
        if(checkPhone(newPhone)==false)
        {
            setErrorResponse("手机号格式错误");
            LOG_ERROR("设置用户手机号时,手机号格式错误!,request_id:{}",request->request_id());
            return ;
        }

        //检查验证码是否正确
        if(_verificationCodeTable->vcode(vcodeId) != vcode)
        {
            setErrorResponse("验证码错误");
            LOG_ERROR("设置用户手机号时,验证码错误!,request_id:{}",request->request_id());
            return ;
        }
        _verificationCodeTable->del(vcodeId);

        //检查用户是否存在
        auto user=_userTable->selectByUserId(userId);
        if(!user)
        {
            setErrorResponse("用户不存在");
            LOG_ERROR("设置用户手机号时,用户不存在!,request_id:{}",request->request_id());
            return ;
        }

        user->phone(newPhone);

        //往数据库中更新数据
        if(_userTable->update(user) == false)
        {
            setErrorResponse("设置用户手机号时,数据库更新失败");
            LOG_ERROR("设置用户手机号时,数据库更新失败!,request_id:{}",request->request_id());
            return ;
        }

        //往es中更新数据
        if(_esUserTable->addData(
            user->userId(),
            user->nickname(),
            user->description(),
            user->phone(),
            user->avatarId()
        ) == false)
        {
            setErrorResponse("设置用户手机号时,ES数据更新失败");
            LOG_ERROR("设置用户手机号时,ES数据更新失败!,request_id:{}",request->request_id());

            return ;
        }

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
    }



    virtual void SetUserDescription(::google::protobuf::RpcController* controller,
                        const ::imserver::SetUserDescriptionRequest* request,
                        ::imserver::SetUserDescriptionResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        }; 

        //获取会话id和用户id
        const std::string session_id=request->session_id();
        std::string userId;
        if(!session_id.empty())
        {
            userId=_sessionTable->userId(session_id).value_or("");
        }
        else
        {
            userId=request->user_id();
        }
        const std::string newDescription=request->description();

        //检查用户是否存在
        auto user=_userTable->selectByUserId(userId);
        if(!user)
        {
            setErrorResponse("用户不存在");
            LOG_ERROR("设置用户描述时,用户不存在!,request_id:{}",request->request_id());
            return ;
        }

        user->description(newDescription);
        
        //往数据库中更新数据
        if(_userTable->update(user) == false)
        {
            setErrorResponse("设置用户描述时,数据库更新失败");
            LOG_ERROR("设置用户描述时,数据库更新失败!,request_id:{}",request->request_id());
            return ;
        }

        //往es中更新数据
        if(_esUserTable->addData(
            user->userId(),
            user->nickname(),
            user->description(),
            user->phone(),
            user->avatarId()
        ) == false)
        {
            setErrorResponse("设置用户描述时,ES数据更新失败");
            LOG_ERROR("设置用户描述时,ES数据更新失败!,request_id:{}",request->request_id());
            return ;
        }

        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
    }


    virtual void SetUserAvatar(::google::protobuf::RpcController* controller,
                        const ::imserver::SetUserAvatarRequest* request,
                        ::imserver::SetUserAvatarResponse* response,
                        ::google::protobuf::Closure* done)
    {
        brpc::ClosureGuard rpc_guard(done);

        auto setErrorResponse=[&response,request](const std::string &errorMessage)
        {
            response->set_request_id(request->request_id());
            response->set_success(false);
            response->set_error_message(errorMessage);
        }; 

        //获取会话id和用户id
        const std::string session_id=request->session_id();
        std::string userId;
        if(!session_id.empty())
        {
            userId=_sessionTable->userId(session_id).value_or("");
        }
        else
        {
            userId=request->user_id();
        }
        const std::string newAvatar=request->avatar();

        //检查用户是否存在
        auto user=_userTable->selectByUserId(userId);
        if(!user)
        {
            setErrorResponse("用户不存在");
            LOG_ERROR("设置用户头像时,用户不存在!,request_id:{}",request->request_id());
            return ;
        }

        const auto oldAvatarId=user->avatarId();
        LOG_DEBUG("设置用户头像时,从数据库中获取到的旧的头像文件id:{},旧的文件id是否为空:{},request_id:{}",oldAvatarId,oldAvatarId.empty(),request->request_id());
        if(oldAvatarId.empty()) //如果旧的头像文件id为空,说明该用户第一次设置头像
        {
            user->avatarId(uuid());
            LOG_DEBUG("生成用户头像文件id:{}",user->avatarId());
        }

        //获取文件服务器信道
        auto channel=_rpcsm->getChannel(_fileServiceDir);
        if(!channel)
        {
            setErrorResponse("获取文件服务通道失败");
            LOG_ERROR("设置用户头像时获取文件服务通道失败!,request_id:{}",request->request_id());
            return ;
        }

        //更新头像数据
        FileService_Stub stub(channel.get());
        brpc::Controller cntl;
        PutSingleFileRequest req;
        PutSingleFileResponse rsp;
        req.set_request_id(request->request_id());
        req.mutable_file_data()->set_file_size(newAvatar.size());
        req.mutable_file_data()->set_file_content(newAvatar);
        req.mutable_file_data()->set_file_id(user->avatarId());
        stub.PutSingleFile(&cntl,&req,&rsp,nullptr);
        if(cntl.Failed() || rsp.success()==false)
        {
            setErrorResponse("设置用户头像时,文件服务更新失败");
            LOG_ERROR("设置用户头像时,文件服务更新失败!,request_id:{}",request->request_id());
            return ;
        }

        //如果不等于,说明是第一次新增头像,需要更新数据库,es中的数据
        if(oldAvatarId != user->avatarId())
        {
            //往数据库中更新数据
            if(_userTable->update(user) == false)
            {
                setErrorResponse("设置用户头像时,数据库更新失败");
                LOG_ERROR("设置用户头像时,数据库更新失败!,request_id:{}",request->request_id());
                return ;
            }
            //往es中更新数据
            if(_esUserTable->addData(
                user->userId(),
                user->nickname(),
                user->description(),
                user->phone(),
                user->avatarId()
            ) == false)
            {
                setErrorResponse("设置用户头像时,ES数据更新失败");
                LOG_ERROR("设置用户头像时,ES数据更新失败!,request_id:{}",request->request_id());
                return ;
            }
        }


        //填充响应
        response->set_request_id(request->request_id());
        response->set_success(true);
    }

private:
    /// @brief session表
    std::shared_ptr<imserver::Session> _sessionTable;
    /// @brief 登录状态表
    std::shared_ptr<imserver::LoginStatus> _loginStatusTable;
    /// @brief 验证码表
    std::shared_ptr<imserver::VerificationCode> _verificationCodeTable;
    
    /// @brief user表
    std::shared_ptr<imserver::UserTable> _userTable;

    /// @brief ES user索引操作封装类
    std::shared_ptr<imserver::ESUser> _esUserTable;

    /// @brief RPC服务管理器
    std::shared_ptr<RPCServiceManager> _rpcsm;

    /// @brief 文件服务目录,格式例子:/service/file/
    std::string _fileServiceDir;
};


/// @brief 用户服务器
class UserServer
{
public:
    UserServer(
        const std::shared_ptr<sw::redis::Redis> &redisClient,
        const std::shared_ptr<odb::core::database> &mysqlClient,
        const std::shared_ptr<elasticlient::Client> &esClient,
        const std::shared_ptr<ServiceRegister> &sr,
        const std::shared_ptr<ServiceDiscover> &sd,
        const std::shared_ptr<brpc::Server> &server
    ):
        _redisClient(redisClient),
        _mysqlClient(mysqlClient),
        _esClient(esClient),
        _sr(sr),
        _sd(sd),
        _server(server)
    {}

    void start()
    {
        //服务器进程运行,直到收到2号信号(ctrl+c)
        _server->RunUntilAskedToQuit();
    }

private:
    std::shared_ptr<sw::redis::Redis> _redisClient;
    std::shared_ptr<odb::core::database> _mysqlClient;
    std::shared_ptr<elasticlient::Client> _esClient;
    
    std::shared_ptr<ServiceRegister> _sr;
    std::shared_ptr<ServiceDiscover> _sd;

    std::shared_ptr<brpc::Server> _server;
};


class UserServerBuilder
{
public:
    /// @brief 创建redis客户端
    /// @param host redis服务器ip
    /// @param port redis服务器端口
    /// @param db 连接的数据库
    /// @param keepAlive 是否开启长连接保活
    void makeRedisClient(
        const std::string &host,
        const uint16_t port,
        const int db=0,
        bool keepAlive=true)
    {
        _redisClient=RedisClientFactory::create(host,port,db,keepAlive);
    }

    /// @brief 创建mysql客户端
    /// @param user mysql用户名
    /// @param password mysql密码
    /// @param host mysql服务器ip
    /// @param db 连接的数据库
    /// @param characterSet mysql字符集
    /// @param port mysql服务器端口
    /// @param connectionPoolCnt mysql连接池连接数量
    void makeMysqlClient( 
        const std::string &user,
        const std::string &password,
        const std::string &host,
        const std::string &db,
        const std::string &characterSet,
        uint16_t port,
        int connectionPoolCnt)
    {
        _mysqlClient=ODBFactory::create(user,password,host,db,characterSet,port,connectionPoolCnt);
    }

    /// @brief 创建ES客户端
    /// @param hostUrlList http://ip:port/的形式的ES服务器地址列表
    void makeESClient(const std::vector<std::string> &hostUrlList)
    {
        _esClient=ESClientFactory::create(hostUrlList);
    }   

    /// @brief 创建rpc服务管理器(并设置关心的服务),服务发现对象;
    /// @param serviceRegistryAddr 服务注册中心的IP:port
    /// @param baseDir 服务根目录(形式：/service/)
    /// @param fileServiceName 文件服务名称(不带实例名,格式例如file,最后面不能加'/')
    void makeRpcsmAndSdObj(
        const std::string &serviceRegistryAddr,
        const std::string &baseDir,
        const std::string &fileServiceName)
    {
        //设置文件服务目录
        if(baseDir.back()=='/') _fileServiceDir=baseDir+fileServiceName;
        else _fileServiceDir=baseDir+'/'+fileServiceName;
        if(_fileServiceDir.back()=='/') _fileServiceDir.pop_back();

        //创建rpc服务管理器对象
        _rpcsm=std::make_shared<RPCServiceManager>();
        _rpcsm->followService(_fileServiceDir);

        //创建服务发现对象
        auto putCb=std::bind(&RPCServiceManager::serviceOnline,_rpcsm,std::placeholders::_1,std::placeholders::_2);
        auto delCb=std::bind(&RPCServiceManager::serviceOffline,_rpcsm,std::placeholders::_1,std::placeholders::_2);
        _sd=std::make_shared<ServiceDiscover>(
            serviceRegistryAddr,
            baseDir,
            putCb,
            delCb
        );
    }

    /// @brief 创建rpc服务器,并启动服务器
    /// @param port 服务器端口
    /// @param idle_timeout_sec rpc服务器超时时间,-1为阻塞等待 
    /// @param num_threads rpc服务器线程数
    void makeRpcServer(uint16_t port,int idle_timeout_sec,int num_threads)
    {
        if(!_rpcsm || _fileServiceDir.empty())
        {
            LOG_ERROR("rpc服务管理模块未初始化!");
            abort();
        }

        _server=make_shared<brpc::Server>();

        //往服务器里面添加服务
        UserServiceImpl *service=new UserServiceImpl(_redisClient,_mysqlClient,_esClient,_rpcsm,_fileServiceDir);
        if(_server->AddService(service,brpc::ServiceOwnership::SERVER_OWNS_SERVICE)==-1)
        {
            LOG_ERROR("添加rpc服务失败");
            abort();
        }

        //服务器参数选项对象
        brpc::ServerOptions options;
        options.idle_timeout_sec=idle_timeout_sec;//超时时间
        options.num_threads=num_threads;//服务器线程数量

        //启动服务器
        if(_server->Start(port,&options)==-1)
        {
            LOG_ERROR("启动rpc服务器失败");
            abort();
        }
    }

    /// @brief 创建服务注册对象,并进行服务注册操作
    /// @param serviceRegistryAddr 服务注册中心的IP:port
    /// @param ttl 当取消keepAlive后,服务在线时长(单位:s)
    /// @param instanceName /服务名/实例名
    /// @param instanceAddr 实例ip:port
    void makeServiceRegisterObj(const std::string& serviceRegistryAddr,int ttl,const std::string &instanceName,const std::string &instanceAddr)
    {
        if(!_server) 
        {
            LOG_ERROR("未初始化rpc服务器模块");
            abort();
        }

        //创建服务注册对象
        _sr=std::make_shared<ServiceRegister>(serviceRegistryAddr,ttl);

        //进行服务注册操作
        _sr->registerService(instanceName,instanceAddr);
    }

    /// @brief 创建一个用户服务器
    /// @return 返回一个用户服务器对象
    std::shared_ptr<UserServer> NewServer()
    {
        if(!_redisClient) 
        {
            LOG_ERROR("未初始化redis客户端模块");
            abort();
        }
        if(!_mysqlClient) 
        {
            LOG_ERROR("未初始化mysql客户端模块");
            abort();
        }
        if(!_esClient) 
        {
            LOG_ERROR("未初始化es客户端模块");
            abort();
        }
        if(!_rpcsm || _fileServiceDir.empty())
        {
            LOG_ERROR("rpc服务管理模块未初始化!");
            abort();
        }
        if(!_server) 
        {
            LOG_ERROR("未初始化rpc服务器模块");
            abort();
        }
        if(!_sr)
        {
            LOG_ERROR("未初始化服务注册模块");
        }

        return std::make_shared<UserServer>(_redisClient,
                                            _mysqlClient,
                                            _esClient,
                                            _sr,
                                            _sd,
                                            _server);
    }

private:
    std::shared_ptr<sw::redis::Redis> _redisClient;
    std::shared_ptr<odb::core::database> _mysqlClient;
    std::shared_ptr<elasticlient::Client> _esClient;
    
    std::shared_ptr<ServiceRegister> _sr;
    std::shared_ptr<RPCServiceManager> _rpcsm;
    std::shared_ptr<ServiceDiscover> _sd;

    std::string _fileServiceDir;

    std::shared_ptr<brpc::Server> _server;
};

}