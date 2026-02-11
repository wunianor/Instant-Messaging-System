#include "etcd.hpp"
#include "rpcService.hpp"
#include "utils.hpp"
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include "user.pb.h"
#include "base.pb.h"
#include "file.pb.h"

DEFINE_bool(run_mode, false, "程序的运行模式，false-调试； true-发布；");
DEFINE_string(log_file, "", "发布模式下，用于指定日志的输出文件");
DEFINE_int32(log_level, 0, "发布模式下，用于指定日志输出等级");

DEFINE_string(etcd_host, "http://127.0.0.1:2379", "服务注册中心地址");
DEFINE_string(base_service, "/service/", "服务监控根目录");
DEFINE_string(user_service, "/service/user", "用户服务目录");

std::shared_ptr<imserver::RPCServiceManager> _user_rpcsm;

std::string login_session_id;
std::string user_id;
std::string verification_code_id;

// //---------------------------------------------------------------------
// //第一次测试
// //Test sending verification code
// TEST(UserServiceTest, SendVerificationCodeTest) {
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::SendVerificationCodeRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_phone("13812345678");
//     imserver::SendVerificationCodeResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.SendVerificationCode(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
//     verification_code_id = rsp.verification_code_id();
//     std::cout << "Verification Code ID: " << verification_code_id << std::endl;
// }

// // Test phone registration
// TEST(UserServiceTest, PhoneRegistrationTest) {
//     ASSERT_FALSE(verification_code_id.empty());
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::UserPhoneRegisterRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_phone("13812345678");
//     req.set_verification_code_id(verification_code_id);
//     std::cout << "Please enter verification code: " << std::endl;
//     std::string code;
//     std::cin >> code;
//     req.set_verification_code(code);
//     imserver::UserPhoneRegisterResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.UserPhoneRegister(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
// }

// // Test nickname password registration
// TEST(UserServiceTest, NicknamePasswordRegistrationTest) {
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::UserNicknameRegisterRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_nickname("TestUser");
//     req.set_password("Test1234");
//     imserver::UserNicknameRegisterResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.UserNicknameRegister(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
// }

// // Test nickname password login
// TEST(UserServiceTest, NicknamePasswordLoginTest) {
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::UserNicknameLoginRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_nickname("TestUser");
//     req.set_password("Test1234");
//     imserver::UserNicknameLoginResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.UserNicknameLogin(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
//     login_session_id = rsp.session_id();
//     std::cout << "Login Session ID: " << login_session_id << std::endl;
// }


// // Test sending verification code
// TEST(UserServiceTest, SendVerificationCodeTest2) {
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::SendVerificationCodeRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_phone("13812345678");
//     imserver::SendVerificationCodeResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.SendVerificationCode(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
//     verification_code_id = rsp.verification_code_id();
//     std::cout << "Verification Code ID: " << verification_code_id << std::endl;
// }

// // Test phone login
// TEST(UserServiceTest, PhoneLoginTest) {
//     ASSERT_FALSE(verification_code_id.empty());
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::UserPhoneLoginRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_phone("13812345678");
//     req.set_verification_code_id(verification_code_id);
//     std::cout << "Please enter verification code: " << std::endl;
//     std::string code;
//     std::cin >> code;
//     req.set_verification_code(code);
//     imserver::UserPhoneLoginResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.UserPhoneLogin(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
//     login_session_id = rsp.session_id();
//     user_id = "13812345678";
//     std::cout << "Login Session ID: " << login_session_id << std::endl;
// }



// //--------------------------------------------------------------------------
// //第二次测试

// // Test get single user info
// TEST(UserServiceTest, GetSingleUserInfoTest) {
//     login_session_id="00163E226257-f48ca4ec1708-0003";
//     user_id="00163E226257-9fc60749dca4-0002";
//     ASSERT_FALSE(login_session_id.empty());
//     ASSERT_FALSE(user_id.empty());
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::GetSingleUserInfoRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_user_id(user_id);
//     req.set_session_id(login_session_id);
//     imserver::GetSingleUserInfoResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.GetSingleUserInfo(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
//     ASSERT_EQ(user_id, rsp.user_info().user_id());
//     std::cout << "User Nickname: " << rsp.user_info().nickname() << std::endl;
//     std::cout << "User avatar:"  << rsp.user_info().avatar()<<std::endl;
// }

// // Test set user nickname
// TEST(UserServiceTest, SetUserNicknameTest) {
//     ASSERT_FALSE(login_session_id.empty());
//     ASSERT_FALSE(user_id.empty());
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::SetUserNicknameRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_user_id(user_id);
//     req.set_session_id(login_session_id);
//     req.set_nickname("TestUser1");
//     imserver::SetUserNicknameResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.SetUserNickname(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
// }

// // Test set user description
// TEST(UserServiceTest, SetUserDescriptionTest) {
//     ASSERT_FALSE(login_session_id.empty());
//     ASSERT_FALSE(user_id.empty());
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::SetUserDescriptionRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_user_id(user_id);
//     req.set_session_id(login_session_id);
//     req.set_description("This is a test user");
//     imserver::SetUserDescriptionResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.SetUserDescription(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
// }

// // Test set user password
// TEST(UserServiceTest, SetUserPasswordTest) {
//     ASSERT_FALSE(login_session_id.empty());
//     ASSERT_FALSE(user_id.empty());
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::SetUserPasswordRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_user_id(user_id);
//     req.set_session_id(login_session_id);
//     req.set_old_password("Test1234");
//     req.set_new_password("NTest5678");
//     imserver::SetUserPasswordResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.SetUserPassword(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
// }

// // Test set user avatar
// TEST(UserServiceTest, SetUserAvatarTest) {
//     ASSERT_FALSE(login_session_id.empty());
//     ASSERT_FALSE(user_id.empty());
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::SetUserAvatarRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_user_id(user_id);
//     req.set_session_id(login_session_id);
//     req.set_avatar("这是一个头像数据");
//     imserver::SetUserAvatarResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.SetUserAvatar(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
// }

// // Test sending verification code
// TEST(UserServiceTest, SendVerificationCodeTest2) {
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::SendVerificationCodeRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_phone("15122223333");
//     imserver::SendVerificationCodeResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.SendVerificationCode(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
//     verification_code_id = rsp.verification_code_id();
//     std::cout << "Verification Code ID: " << verification_code_id << std::endl;
// }

// //设置手机号
// TEST(UserServiceTest,SetUserPhoneTest)
// {
//     ASSERT_FALSE(login_session_id.empty());
//     ASSERT_FALSE(user_id.empty());
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::SetUserPhoneRequest req;
//     req.set_request_id(imserver::uuid());
//     req.set_user_id(user_id);
//     req.set_session_id(login_session_id);
//     req.set_phone("15122223333");
//     req.set_verification_code_id(verification_code_id);
//     std::cout << "Please enter verification code: " << std::endl;
//     std::string code;
//     std::cin >> code;
//     req.set_verification_code(code);

//     imserver::SetUserPhoneResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.SetUserPhone(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
// }


// //------------------------------------------------------------------------------
// //第三次测试
// //Test get multiple user info
// TEST(UserServiceTest, GetMultiUserInfoTest) {
//     auto channel = _user_rpcsm->getChannel(FLAGS_user_service);
//     ASSERT_TRUE(channel);

//     imserver::GetMultiUserInfoRequest req;
//     req.set_request_id(imserver::uuid());
//     req.add_user_id("00163E226257-8348f2e83b98-0001");
//     req.add_user_id("00163E226257-9fc60749dca4-0002");
//     imserver::GetMultiUserInfoResponse rsp;
//     brpc::Controller cntl;
//     imserver::UserService_Stub stub(channel.get());
//     stub.GetMultiUserInfo(&cntl, &req, &rsp, nullptr);
//     ASSERT_FALSE(cntl.Failed());
//     ASSERT_TRUE(rsp.success());
//     ASSERT_TRUE(rsp.user_info_map().size()==req.user_id_size());
//     std::cout << "Number of users retrieved: " << rsp.user_info_map().size() << std::endl;
// }

int main(int argc, char *argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    imserver::initGlogger(FLAGS_run_mode, FLAGS_log_file, static_cast<spdlog::level::level_enum>(FLAGS_log_level));

    // 初始化RPC服务管理器
    _user_rpcsm = std::make_shared<imserver::RPCServiceManager>();
    _user_rpcsm->followService(FLAGS_user_service);

    // 等待服务上线
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 初始化服务发现
    auto putCb = std::bind(&imserver::RPCServiceManager::serviceOnline, _user_rpcsm.get(), std::placeholders::_1, std::placeholders::_2);
    auto delCb = std::bind(&imserver::RPCServiceManager::serviceOffline, _user_rpcsm.get(), std::placeholders::_1, std::placeholders::_2);
    auto sd = std::make_shared<imserver::ServiceDiscover>(FLAGS_etcd_host, FLAGS_base_service, putCb, delCb);



    testing::InitGoogleTest(&argc, argv);
    LOG_INFO("开始测试用户服务！");
    return RUN_ALL_TESTS();
}
