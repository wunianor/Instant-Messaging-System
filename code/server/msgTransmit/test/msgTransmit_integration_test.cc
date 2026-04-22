#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <brpc/channel.h>

#include "msgTransmit.pb.h"

DEFINE_string(msg_transmit_host, "127.0.0.1", "msgTransmit brpc 监听地址");
DEFINE_int32(msg_transmit_port, 10003, "msgTransmit brpc 端口");

DEFINE_string(mysql_cmd,
              "mysql -uroot -p123456 im",
              "用于准备与查询测试数据的 mysql 客户端命令前缀（含库名）");
DEFINE_string(chat_session_member_sql,
              "/home/hoshinoqaq/Project/Instant-Messaging-System/code/server/msgTransmit/build/chat-session-member.sql",
              "初始化 chat_session_member 表结构的 SQL 文件路径");

namespace
{

const char *kValidUserId = "00163E226257-9fc60749dca4-0002";
const char *kTargetUserId = "im_gtest_transmit_target_b";
const char *kSessionSuccess = "im_gtest_transmit_success";
const char *kSessionNoMember = "im_gtest_transmit_no_member";
const char *kSessionBoundary = "im_gtest_transmit_boundary";

int runShell(const std::string &cmd)
{
    return std::system(cmd.c_str());
}

std::string mysqlQueryFirstLine(const std::string &sql)
{
    std::ostringstream cmd;
    cmd << FLAGS_mysql_cmd << " -N -B -e \"" << sql << "\" 2>/dev/null";
    FILE *fp = popen(cmd.str().c_str(), "r");
    if (!fp) return {};

    // 支持较大文本消息（例如 32KB）的一行查询返回，避免被小缓冲区截断。
    char buf[262144] = {};
    if (!fgets(buf, sizeof(buf), fp))
    {
        pclose(fp);
        return {};
    }
    pclose(fp);

    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    return line;
}

bool mysqlExec(const std::string &sql)
{
    std::ostringstream cmd;
    cmd << FLAGS_mysql_cmd << " -e \"" << sql << "\" 2>/dev/null";
    return runShell(cmd.str()) == 0;
}

void ensureChatSessionMemberTable()
{
    std::ostringstream cmd;
    cmd << FLAGS_mysql_cmd << " < \"" << FLAGS_chat_session_member_sql << "\"";
    ASSERT_EQ(0, runShell(cmd.str()))
        << "init chat_session_member schema failed: " << FLAGS_chat_session_member_sql;
}

void prepareSessionMembers(const std::string &sessionId, const std::vector<std::string> &members)
{
    std::ostringstream sql;
    sql << "DELETE FROM chat_session_member WHERE session_id='" << sessionId << "';";
    if (!members.empty())
    {
        sql << "INSERT INTO chat_session_member (session_id,user_id) VALUES ";
        for (size_t i = 0; i < members.size(); ++i)
        {
            if (i > 0) sql << ",";
            sql << "('" << sessionId << "','" << members[i] << "')";
        }
        sql << ";";
    }
    ASSERT_TRUE(mysqlExec(sql.str())) << "prepare chat_session_member failed";
}

void resetMessageAndSessionMemberFixtures()
{
    std::ostringstream sql;
    // 先清理本测试涉及会话的历史消息、成员与用户数据，确保每次运行从同一基线开始。
    sql << "DELETE FROM message WHERE chat_session_id IN ('"
        << kSessionSuccess << "','" << kSessionBoundary << "','" << kSessionNoMember << "');"
        << "DELETE FROM chat_session_member WHERE session_id IN ('"
        << kSessionSuccess << "','" << kSessionBoundary << "','" << kSessionNoMember << "');"
        << "DELETE FROM user WHERE user_id IN ('"
        << kValidUserId << "','" << kTargetUserId << "');"
        << "INSERT INTO user (user_id,nickname,description,password,phone,avatar_id) VALUES "
        << "('" << kValidUserId << "','itest_sender','itest sender user','123456',NULL,NULL),"
        << "('" << kTargetUserId << "','itest_target','itest target user','123456',NULL,NULL);"
        << "INSERT INTO chat_session_member (session_id,user_id) VALUES "
        << "('" << kSessionSuccess << "','" << kValidUserId << "'),"
        << "('" << kSessionSuccess << "','" << kTargetUserId << "'),"
        << "('" << kSessionBoundary << "','" << kValidUserId << "'),"
        << "('" << kSessionBoundary << "','" << kTargetUserId << "');";

    ASSERT_TRUE(mysqlExec(sql.str())) << "reset message/chat_session_member fixtures failed";
}

void clearSessionMessages(const std::string &sessionId)
{
    std::ostringstream sql;
    sql << "DELETE FROM message WHERE chat_session_id='" << sessionId << "';";
    ASSERT_TRUE(mysqlExec(sql.str())) << "cleanup message failed";
}

std::int64_t countSessionMessages(const std::string &sessionId)
{
    std::ostringstream sql;
    sql << "SELECT COUNT(*) FROM message WHERE chat_session_id='" << sessionId << "';";
    const std::string line = mysqlQueryFirstLine(sql.str());
    if (line.empty()) return -1;
    return std::stoll(line);
}

std::vector<std::string> splitByTab(const std::string &line)
{
    std::vector<std::string> cols;
    std::stringstream ss(line);
    std::string col;
    while (std::getline(ss, col, '\t')) cols.push_back(col);
    return cols;
}

struct PersistedMessageRow
{
    bool found = false;
    int messageType = -1;
    std::string content;
    std::string fileId;
    std::string fileName;
    long long fileSize = 0;
};

PersistedMessageRow queryPersistedMessage(const std::string &messageId)
{
    std::ostringstream sql;
    sql << "SELECT message_type,IFNULL(content,''),IFNULL(file_id,''),IFNULL(file_name,''),IFNULL(file_size,0) "
        << "FROM message WHERE message_id='" << messageId << "' LIMIT 1;";
    const std::string line = mysqlQueryFirstLine(sql.str());
    if (line.empty()) return {};

    PersistedMessageRow row;
    row.found = true;
    const auto cols = splitByTab(line);
    if (cols.size() >= 5)
    {
        row.messageType = std::stoi(cols[0]);
        row.content = cols[1];
        row.fileId = cols[2];
        row.fileName = cols[3];
        row.fileSize = std::stoll(cols[4]);
    }
    return row;
}

std::size_t queryPersistedContentLength(const std::string &messageId)
{
    std::ostringstream sql;
    sql << "SELECT IFNULL(CHAR_LENGTH(content),0) "
        << "FROM message WHERE message_id='" << messageId << "' LIMIT 1;";
    const std::string line = mysqlQueryFirstLine(sql.str());
    if (line.empty()) return 0;
    return static_cast<std::size_t>(std::stoull(line));
}

bool waitForMessagePersisted(
    const std::string &messageId,
    PersistedMessageRow &row,
    int timeoutMs = 15000,
    int intervalMs = 300)
{
    const int rounds = timeoutMs / intervalMs;
    for (int i = 0; i < rounds; ++i)
    {
        row = queryPersistedMessage(messageId);
        if (row.found) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
    return false;
}

void assertTargetIds(
    const imserver::GetTransmitTargetResponse &rsp,
    const std::unordered_set<std::string> &expected)
{
    ASSERT_EQ(expected.size(), static_cast<size_t>(rsp.target_id_size()));
    std::unordered_set<std::string> got;
    for (const auto &id : rsp.target_id()) got.insert(id);
    EXPECT_EQ(expected, got);
}

std::shared_ptr<brpc::Channel> makeChannel()
{
    auto channel = std::make_shared<brpc::Channel>();
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = 8000;
    options.max_retry = 1;
    const std::string addr =
        FLAGS_msg_transmit_host + ":" + std::to_string(FLAGS_msg_transmit_port);
    if (channel->Init(addr.c_str(), &options) != 0)
        return nullptr;
    return channel;
}

class MsgTransmitIntegrationTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        ensureChatSessionMemberTable();
        resetMessageAndSessionMemberFixtures();
    }

    void SetUp() override
    {
        channel_ = makeChannel();
        ASSERT_NE(channel_, nullptr);
    }

    std::shared_ptr<brpc::Channel> channel_;
};

TEST_F(MsgTransmitIntegrationTest, StringMessage_ForwardAndPersistCorrectly)
{
    imserver::MsgTransmitService_Stub stub(channel_.get());
    brpc::Controller cntl;
    imserver::NewMessageRequest req;
    imserver::GetTransmitTargetResponse rsp;

    req.set_request_id("itest_transmit_string");
    req.set_user_id(kValidUserId);
    req.set_session_id("");
    req.set_chat_session_id(kSessionSuccess);
    req.mutable_message_content()->set_message_type(imserver::MessageType::STRING);
    req.mutable_message_content()->mutable_string_message()->set_content("itest transmit string payload");

    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    assertTargetIds(rsp, {kValidUserId, kTargetUserId});
    ASSERT_EQ(imserver::MessageType::STRING, rsp.message_info().message_content().message_type());
    EXPECT_EQ("itest transmit string payload",
              rsp.message_info().message_content().string_message().content());

    PersistedMessageRow row;
    ASSERT_TRUE(waitForMessagePersisted(rsp.message_info().message_id(), row));
    EXPECT_EQ(static_cast<int>(imserver::MessageType::STRING), row.messageType);
    EXPECT_EQ("itest transmit string payload", row.content);
}

TEST_F(MsgTransmitIntegrationTest, ImageMessage_ForwardAndPersistCorrectly)
{
    imserver::MsgTransmitService_Stub stub(channel_.get());
    brpc::Controller cntl;
    imserver::NewMessageRequest req;
    imserver::GetTransmitTargetResponse rsp;

    req.set_request_id("itest_transmit_image");
    req.set_user_id(kValidUserId);
    req.set_session_id("");
    req.set_chat_session_id(kSessionSuccess);
    req.mutable_message_content()->set_message_type(imserver::MessageType::IMAGE);
    req.mutable_message_content()->mutable_image_message()->set_image_content("itest_image_binary");

    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    assertTargetIds(rsp, {kValidUserId, kTargetUserId});

    PersistedMessageRow row;
    ASSERT_TRUE(waitForMessagePersisted(rsp.message_info().message_id(), row));
    EXPECT_EQ(static_cast<int>(imserver::MessageType::IMAGE), row.messageType);
    EXPECT_FALSE(row.fileId.empty());
}

TEST_F(MsgTransmitIntegrationTest, FileMessage_ForwardAndPersistCorrectly)
{
    imserver::MsgTransmitService_Stub stub(channel_.get());
    brpc::Controller cntl;
    imserver::NewMessageRequest req;
    imserver::GetTransmitTargetResponse rsp;

    const std::string filePayload = "itest_file_binary_payload";
    req.set_request_id("itest_transmit_file");
    req.set_user_id(kValidUserId);
    req.set_session_id("");
    req.set_chat_session_id(kSessionSuccess);
    req.mutable_message_content()->set_message_type(imserver::MessageType::FILE);
    req.mutable_message_content()->mutable_file_message()->set_file_name("itest_doc.txt");
    req.mutable_message_content()->mutable_file_message()->set_file_size(
        static_cast<std::int64_t>(filePayload.size()));
    req.mutable_message_content()->mutable_file_message()->set_file_contents(filePayload);

    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    assertTargetIds(rsp, {kValidUserId, kTargetUserId});

    PersistedMessageRow row;
    ASSERT_TRUE(waitForMessagePersisted(rsp.message_info().message_id(), row));
    EXPECT_EQ(static_cast<int>(imserver::MessageType::FILE), row.messageType);
    EXPECT_FALSE(row.fileId.empty());
    EXPECT_EQ("itest_doc.txt", row.fileName);
    EXPECT_EQ(static_cast<long long>(filePayload.size()), row.fileSize);
}

TEST_F(MsgTransmitIntegrationTest, SpeechMessage_ForwardAndPersistCorrectly)
{
    imserver::MsgTransmitService_Stub stub(channel_.get());
    brpc::Controller cntl;
    imserver::NewMessageRequest req;
    imserver::GetTransmitTargetResponse rsp;

    req.set_request_id("itest_transmit_speech");
    req.set_user_id(kValidUserId);
    req.set_session_id("");
    req.set_chat_session_id(kSessionSuccess);
    req.mutable_message_content()->set_message_type(imserver::MessageType::SPEECH);
    req.mutable_message_content()->mutable_speech_message()->set_file_contents("itest_speech_binary");

    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    assertTargetIds(rsp, {kValidUserId, kTargetUserId});

    PersistedMessageRow row;
    ASSERT_TRUE(waitForMessagePersisted(rsp.message_info().message_id(), row));
    EXPECT_EQ(static_cast<int>(imserver::MessageType::SPEECH), row.messageType);
    EXPECT_FALSE(row.fileId.empty());
}

TEST_F(MsgTransmitIntegrationTest, SessionWithoutMembers_StillPersistsWithEmptyTargets)
{
    imserver::MsgTransmitService_Stub stub(channel_.get());
    brpc::Controller cntl;
    imserver::NewMessageRequest req;
    imserver::GetTransmitTargetResponse rsp;

    req.set_request_id("itest_transmit_no_member");
    req.set_user_id(kValidUserId);
    req.set_session_id("");
    req.set_chat_session_id(kSessionNoMember);
    req.mutable_message_content()->set_message_type(imserver::MessageType::STRING);
    req.mutable_message_content()->mutable_string_message()->set_content("no member session message");

    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    EXPECT_EQ(0, rsp.target_id_size());

    PersistedMessageRow row;
    ASSERT_TRUE(waitForMessagePersisted(rsp.message_info().message_id(), row));
    EXPECT_EQ(static_cast<int>(imserver::MessageType::STRING), row.messageType);
}

TEST_F(MsgTransmitIntegrationTest, SenderNotExist_FailsAndDoesNotPersist)
{
    const std::int64_t beforeCount = countSessionMessages(kSessionSuccess);
    ASSERT_GE(beforeCount, 0);

    imserver::MsgTransmitService_Stub stub(channel_.get());
    brpc::Controller cntl;
    imserver::NewMessageRequest req;
    imserver::GetTransmitTargetResponse rsp;

    req.set_request_id("itest_transmit_bad_sender");
    req.set_user_id("im_gtest_sender_not_exist");
    req.set_session_id("");
    req.set_chat_session_id(kSessionSuccess);
    req.mutable_message_content()->set_message_type(imserver::MessageType::STRING);
    req.mutable_message_content()->mutable_string_message()->set_content("should_not_persist");

    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_FALSE(rsp.success());

    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const std::int64_t afterCount = countSessionMessages(kSessionSuccess);
    ASSERT_GE(afterCount, 0);
    EXPECT_EQ(beforeCount, afterCount);
}

TEST_F(MsgTransmitIntegrationTest, StringBoundary_SingleCharPersists)
{
    imserver::MsgTransmitService_Stub stub(channel_.get());
    brpc::Controller cntl;
    imserver::NewMessageRequest req;
    imserver::GetTransmitTargetResponse rsp;

    req.set_request_id("itest_transmit_single_char");
    req.set_user_id(kValidUserId);
    req.set_session_id("");
    req.set_chat_session_id(kSessionBoundary);
    req.mutable_message_content()->set_message_type(imserver::MessageType::STRING);
    req.mutable_message_content()->mutable_string_message()->set_content("a");

    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    PersistedMessageRow row;
    ASSERT_TRUE(waitForMessagePersisted(rsp.message_info().message_id(), row));
    EXPECT_EQ(static_cast<int>(imserver::MessageType::STRING), row.messageType);
    EXPECT_EQ("a", row.content);
}

TEST_F(MsgTransmitIntegrationTest, StringBoundary_LargeTextPersists)
{
    const std::string largeText(32 * 1024, 'x');

    imserver::MsgTransmitService_Stub stub(channel_.get());
    brpc::Controller cntl;
    imserver::NewMessageRequest req;
    imserver::GetTransmitTargetResponse rsp;

    req.set_request_id("itest_transmit_large_text");
    req.set_user_id(kValidUserId);
    req.set_session_id("");
    req.set_chat_session_id(kSessionBoundary);
    req.mutable_message_content()->set_message_type(imserver::MessageType::STRING);
    req.mutable_message_content()->mutable_string_message()->set_content(largeText);

    stub.GetTransmitTarget(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    PersistedMessageRow row;
    ASSERT_TRUE(waitForMessagePersisted(rsp.message_info().message_id(), row));
    EXPECT_EQ(static_cast<int>(imserver::MessageType::STRING), row.messageType);
    EXPECT_EQ(largeText.size(), queryPersistedContentLength(rsp.message_info().message_id()));
}

} // namespace

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

