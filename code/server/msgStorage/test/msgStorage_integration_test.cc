#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <brpc/channel.h>

#include "msgStorage.pb.h"

DEFINE_string(msg_storage_host, "127.0.0.1", "msgStorage brpc 监听地址");
DEFINE_int32(msg_storage_port, 10004, "msgStorage brpc 端口");

DEFINE_string(mysql_cmd,
              "mysql -uroot -p123456 im",
              "用于写入测试数据的 mysql 客户端命令前缀（含库名）");

/// 与 ODB 生成的表结构保持一致（脚本内含 DROP TABLE，会清空整张表）
DEFINE_string(
    msg_storage_message_schema_sql,
    "/home/hoshinoqaq/Project/Instant-Messaging-System/code/server/msgStorage/build/message.sql",
    "message 表结构 SQL（message.sql）路径");
DEFINE_string(
    msg_storage_user_schema_sql,
    "/home/hoshinoqaq/Project/Instant-Messaging-System/code/server/user/build/user.sql",
    "user 表结构 SQL（user.sql）路径");
DEFINE_string(
    msg_storage_chat_session_member_schema_sql,
    "/home/hoshinoqaq/Project/Instant-Messaging-System/code/server/msgTransmit/build/chat-session-member.sql",
    "chat_session_member 表结构 SQL（chat-session-member.sql）路径");

namespace
{

/// GetHistoryMessage 窄窗半宽（秒）：以库中消息的 UNIX 秒为中心 ±kHistoryHalfWindowSeconds
constexpr std::int64_t kHistoryHalfWindowSeconds = 600;
/// MessageSearch 使用的唯一关键字前缀，避免与线上/历史数据冲突
const char *kSearchToken = "im_gtest_search_token_v1";

/// 仅用于 GetRecentMessage 集成测试，勿与其它用例冲突
const char *kChatSessionId = "im_gtest_recent_only";
const char *kUserId = "00163E226257-9fc60749dca4-0002";

const char *kMsg1 = "im_recent_m1";
const char *kMsg2 = "im_recent_m2";
const char *kMsg3 = "im_recent_m3";

const char *kTime1 = "2026-05-01 10:00:00";
const char *kTime2 = "2026-05-01 11:00:00";
const char *kTime3 = "2026-05-01 12:00:00";

int runShell(const std::string &cmd)
{
    return std::system(cmd.c_str());
}

bool mysqlExec(const std::string &sql)
{
    std::ostringstream cmd;
    cmd << FLAGS_mysql_cmd << " -e \"" << sql << "\" 2>/dev/null";
    return runShell(cmd.str()) == 0;
}

/// 执行 ODB 生成的建表脚本（含 DROP）。用于使本地库结构与代码一致；重复执行结果相同。
bool applySchemaFromFile(const std::string &path)
{
    std::ostringstream cmd;
    cmd << FLAGS_mysql_cmd << " < \"" << path << "\" 2>/dev/null";
    return runShell(cmd.str()) == 0;
}

/// 插入集成测试会用到的发送者用户（列名与 user.sql 一致）。
void seedIntegrationTestUser()
{
    std::ostringstream sql;
    sql << "DELETE FROM user WHERE user_id='" << kUserId << "';"
        << "INSERT INTO user (user_id,nickname,description,password,phone,avatar_id) VALUES "
        << "('" << kUserId << "','im_gtest_storage_u1','msgStorage integration test',NULL,NULL,NULL);";
    ASSERT_TRUE(mysqlExec(sql.str())) << "seed integration test user failed";
}

/// 某条消息在库中的 UNIX 秒，用于与 GetHistoryMessage 请求区间对齐
std::int64_t mysqlMessageCreateUnix(const char *message_id)
{
    std::ostringstream q;
    q << FLAGS_mysql_cmd
      << " -N -B -e \"SELECT UNIX_TIMESTAMP(create_time) FROM message WHERE message_id='"
      << message_id << "' LIMIT 1;\" 2>/dev/null";
    FILE *fp = popen(q.str().c_str(), "r");
    if (!fp)
        return 0;
    char buf[64] = {};
    if (!fgets(buf, sizeof(buf), fp))
    {
        pclose(fp);
        return 0;
    }
    pclose(fp);
    return static_cast<std::int64_t>(std::stoll(buf));
}

/// 向 message 表写入三条按时间递增的消息（仅依赖 MySQL，无需 ES）
void seedRecentMessageFixtures()
{
    std::ostringstream sql;
    sql << "DELETE FROM message WHERE chat_session_id='" << kChatSessionId << "';"
        << "INSERT INTO message (message_id,chat_session_id,user_id,message_type,create_time,content) VALUES "
        << "('" << kMsg1 << "','" << kChatSessionId << "','" << kUserId << "',0,'" << kTime1 << "','recent_one'),"
        << "('" << kMsg2 << "','" << kChatSessionId << "','" << kUserId << "',0,'" << kTime2 << "','recent_two'),"
        << "('" << kMsg3 << "','" << kChatSessionId << "','" << kUserId << "',0,'" << kTime3 << "','recent_three');";

    ASSERT_TRUE(mysqlExec(sql.str())) << "seed mysql failed";
}

/// 会话内混合 STRING / IMAGE / FILE / SPEECH（仅 MySQL）
const char *kChatSessionMixed = "im_gtest_recent_mixed";

void seedRecentMixedMediaFixtures()
{
    std::ostringstream sql;
    sql << "DELETE FROM message WHERE chat_session_id='" << kChatSessionMixed << "';"
        << "INSERT INTO message "
        << "(message_id,chat_session_id,user_id,message_type,create_time,content,file_id,file_name,file_size) VALUES "
        << "('im_mix_s1','" << kChatSessionMixed << "','" << kUserId
        << "',0,'2026-06-10 10:00:00','mix_text_a',NULL,NULL,NULL),"
        << "('im_mix_img','" << kChatSessionMixed << "','" << kUserId
        << "',1,'2026-06-10 11:00:00',NULL,'mix_img_uuid_001',NULL,NULL),"
        << "('im_mix_file','" << kChatSessionMixed << "','" << kUserId
        << "',2,'2026-06-10 12:00:00',NULL,'mix_file_uuid_002','notes.txt',2048),"
        << "('im_mix_sp','" << kChatSessionMixed << "','" << kUserId
        << "',3,'2026-06-10 13:00:00',NULL,'mix_sp_uuid_003',NULL,NULL),"
        << "('im_mix_s2','" << kChatSessionMixed << "','" << kUserId
        << "',0,'2026-06-10 14:00:00','mix_text_b',NULL,NULL,NULL);";

    ASSERT_TRUE(mysqlExec(sql.str())) << "seed mixed mysql failed";
}

/// GetHistoryMessage：会话内 STRING×2 / IMAGE / FILE / SPEECH，按时间从早到晚分布于同一天
const char *kChatSessionHistory = "im_gtest_history_mixed";
/// MessageSearch：目标会话 + 旁路会话，校验 chat_session_id 过滤
const char *kChatSessionSearch = "im_gtest_search_session_v1";
const char *kChatSessionSearchOther = "im_gtest_search_other_session_v1";

void seedHistoryMixedFixtures()
{
    std::ostringstream sql;
    sql << "DELETE FROM message WHERE chat_session_id='" << kChatSessionHistory << "';"
        << "INSERT INTO message "
        << "(message_id,chat_session_id,user_id,message_type,create_time,content,file_id,file_name,file_size) VALUES "
        << "('im_hist_s1','" << kChatSessionHistory << "','" << kUserId
        << "',0,'2026-07-01 08:00:00','hist_txt_a',NULL,NULL,NULL),"
        << "('im_hist_img','" << kChatSessionHistory << "','" << kUserId
        << "',1,'2026-07-01 09:00:00',NULL,'hist_img_uuid',NULL,NULL),"
        << "('im_hist_file','" << kChatSessionHistory << "','" << kUserId
        << "',2,'2026-07-01 10:00:00',NULL,'hist_file_uuid','report.pdf',8192),"
        << "('im_hist_sp','" << kChatSessionHistory << "','" << kUserId
        << "',3,'2026-07-01 11:00:00',NULL,'hist_sp_uuid',NULL,NULL),"
        << "('im_hist_s2','" << kChatSessionHistory << "','" << kUserId
        << "',0,'2026-07-01 12:00:00','hist_txt_b',NULL,NULL,NULL);";

    ASSERT_TRUE(mysqlExec(sql.str())) << "seed history mysql failed";
}

/// MessageSearch 用例通过直接写 ES 索引准备数据（MessageSearch 查询走 ES，不依赖 message 表）
void seedMessageSearchFixtures()
{
    const std::string deleteCmd =
        "curl -s -o /dev/null -X DELETE 'http://127.0.0.1:9200/message/_doc/im_search_txt?refresh=true' && "
        "curl -s -o /dev/null -X DELETE 'http://127.0.0.1:9200/message/_doc/im_search_img?refresh=true' && "
        "curl -s -o /dev/null -X DELETE 'http://127.0.0.1:9200/message/_doc/im_search_file?refresh=true' && "
        "curl -s -o /dev/null -X DELETE 'http://127.0.0.1:9200/message/_doc/im_search_speech?refresh=true' && "
        "curl -s -o /dev/null -X DELETE 'http://127.0.0.1:9200/message/_doc/im_search_noise?refresh=true' && "
        "curl -s -o /dev/null -X DELETE 'http://127.0.0.1:9200/message/_doc/im_search_other_session?refresh=true'";
    ASSERT_EQ(0, runShell(deleteCmd)) << "cleanup search docs failed";

    std::ostringstream put;
    put << "curl -s -o /dev/null -X PUT 'http://127.0.0.1:9200/message/_doc/im_search_txt?refresh=true' "
        << "-H 'Content-Type: application/json' "
        << "-d '{\"message_id\":\"im_search_txt\",\"user_id\":\"" << kUserId
        << "\",\"chat_session_id\":\"" << kChatSessionSearch
        << "\",\"create_time\":1782864000,\"message_type\":0,"
        << "\"content\":\"" << kSearchToken << " text payload\"}'"
        << " && "
        << "curl -s -o /dev/null -X PUT 'http://127.0.0.1:9200/message/_doc/im_search_img?refresh=true' "
        << "-H 'Content-Type: application/json' "
        << "-d '{\"message_id\":\"im_search_img\",\"user_id\":\"" << kUserId
        << "\",\"chat_session_id\":\"" << kChatSessionSearch
        << "\",\"create_time\":1782867600,\"message_type\":1,"
        << "\"content\":\"" << kSearchToken << " image payload\","
        << "\"file_id\":\"search_img_file_id\"}'"
        << " && "
        << "curl -s -o /dev/null -X PUT 'http://127.0.0.1:9200/message/_doc/im_search_file?refresh=true' "
        << "-H 'Content-Type: application/json' "
        << "-d '{\"message_id\":\"im_search_file\",\"user_id\":\"" << kUserId
        << "\",\"chat_session_id\":\"" << kChatSessionSearch
        << "\",\"create_time\":1782871200,\"message_type\":2,"
        << "\"content\":\"" << kSearchToken << " file payload\","
        << "\"file_id\":\"search_file_file_id\",\"file_name\":\"spec.pdf\",\"file_size\":4096}'"
        << " && "
        << "curl -s -o /dev/null -X PUT 'http://127.0.0.1:9200/message/_doc/im_search_speech?refresh=true' "
        << "-H 'Content-Type: application/json' "
        << "-d '{\"message_id\":\"im_search_speech\",\"user_id\":\"" << kUserId
        << "\",\"chat_session_id\":\"" << kChatSessionSearch
        << "\",\"create_time\":1782874800,\"message_type\":3,"
        << "\"content\":\"" << kSearchToken << " speech payload\","
        << "\"file_id\":\"search_speech_file_id\"}'"
        << " && "
        << "curl -s -o /dev/null -X PUT 'http://127.0.0.1:9200/message/_doc/im_search_noise?refresh=true' "
        << "-H 'Content-Type: application/json' "
        << "-d '{\"message_id\":\"im_search_noise\",\"user_id\":\"" << kUserId
        << "\",\"chat_session_id\":\"" << kChatSessionSearch
        << "\",\"create_time\":1782878400,\"message_type\":0,"
        << "\"content\":\"search_noise_without_token\"}'"
        << " && "
        << "curl -s -o /dev/null -X PUT 'http://127.0.0.1:9200/message/_doc/im_search_other_session?refresh=true' "
        << "-H 'Content-Type: application/json' "
        << "-d '{\"message_id\":\"im_search_other_session\",\"user_id\":\"" << kUserId
        << "\",\"chat_session_id\":\"" << kChatSessionSearchOther
        << "\",\"create_time\":1782882000,\"message_type\":0,"
        << "\"content\":\"" << kSearchToken << " cross session payload\"}'";

    ASSERT_EQ(0, runShell(put.str())) << "seed message search docs failed";
}

/// 对齐 ODB 生成的三座表结构，并插入固定测试用户；与各 TestSuite 中的 DELETE+INSERT _fixture 叠加后，重复运行测试二进制结果一致。
class MsgStorageIntegrationEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        ASSERT_TRUE(applySchemaFromFile(FLAGS_msg_storage_message_schema_sql))
            << "apply message schema: " << FLAGS_msg_storage_message_schema_sql;
        ASSERT_TRUE(applySchemaFromFile(FLAGS_msg_storage_user_schema_sql))
            << "apply user schema: " << FLAGS_msg_storage_user_schema_sql;
        ASSERT_TRUE(applySchemaFromFile(FLAGS_msg_storage_chat_session_member_schema_sql))
            << "apply chat_session_member schema: " << FLAGS_msg_storage_chat_session_member_schema_sql;
        seedIntegrationTestUser();
    }
};

std::shared_ptr<brpc::Channel> makeChannel()
{
    auto channel = std::make_shared<brpc::Channel>();
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = 8000;
    options.max_retry = 1;
    const std::string addr =
        FLAGS_msg_storage_host + ":" + std::to_string(FLAGS_msg_storage_port);
    if (channel->Init(addr.c_str(), &options) != 0)
        return nullptr;
    return channel;
}

class MsgStorageRecentMessageTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        seedRecentMessageFixtures();
    }

    void SetUp() override
    {
        channel_ = makeChannel();
        ASSERT_NE(channel_, nullptr);
    }

    std::shared_ptr<brpc::Channel> channel_;
};

/// Recent N：库中按 create_time DESC 取最近两条，再在服务端 reverse 成时间正序
TEST_F(MsgStorageRecentMessageTest, ReturnsLastTwoNewestThenAscendingTimeOrder)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetRecentMessageRequest req;
    imserver::GetRecentMessageResponse rsp;

    req.set_request_id("itest_recent_basic");
    req.set_chat_session_id(kChatSessionId);
    req.set_message_count(2);
    req.set_cur_time(0);
    req.set_user_id("");
    req.set_session_id("");

    stub.GetRecentMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    ASSERT_EQ(2, rsp.message_info_size());
    EXPECT_EQ(kMsg2, rsp.message_info(0).message_id());
    EXPECT_EQ(std::string("recent_two"),
              rsp.message_info(0).message_content().string_message().content());
    EXPECT_EQ(kMsg3, rsp.message_info(1).message_id());
    EXPECT_EQ(std::string("recent_three"),
              rsp.message_info(1).message_content().string_message().content());
}

/// message_count 覆盖会话内全部消息时，按时间从早到晚排列
TEST_F(MsgStorageRecentMessageTest, ReturnsAllMessagesWhenCountLargeEnough)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetRecentMessageRequest req;
    imserver::GetRecentMessageResponse rsp;

    req.set_request_id("itest_recent_all");
    req.set_chat_session_id(kChatSessionId);
    req.set_message_count(100);
    req.set_cur_time(0);
    req.set_user_id("");
    req.set_session_id("");

    stub.GetRecentMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    ASSERT_EQ(3, rsp.message_info_size());
    EXPECT_EQ(kMsg1, rsp.message_info(0).message_id());
    EXPECT_EQ(kMsg2, rsp.message_info(1).message_id());
    EXPECT_EQ(kMsg3, rsp.message_info(2).message_id());
}

/// user_id 存在且用户不存在时鉴权失败
TEST_F(MsgStorageRecentMessageTest, FailsWhenUserDoesNotExist)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetRecentMessageRequest req;
    imserver::GetRecentMessageResponse rsp;

    req.set_request_id("itest_recent_auth_fail");
    req.set_chat_session_id(kChatSessionId);
    req.set_message_count(2);
    req.set_cur_time(0);
    req.set_user_id("invalid_user_im_gtest_xxxxxxxx");
    req.set_session_id("");

    stub.GetRecentMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_FALSE(rsp.success());
}

/// 最近 N 条中包含图片、文件、语音消息时的 RPC 字段填充
class MsgStorageRecentMixedMediaTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        seedRecentMixedMediaFixtures();
    }

    void SetUp() override
    {
        channel_ = makeChannel();
        ASSERT_NE(channel_, nullptr);
    }

    std::shared_ptr<brpc::Channel> channel_;
};

TEST_F(MsgStorageRecentMixedMediaTest, ReturnsRecentFiveWithImageFileSpeechAndText)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetRecentMessageRequest req;
    imserver::GetRecentMessageResponse rsp;

    req.set_request_id("itest_recent_mixed");
    req.set_chat_session_id(kChatSessionMixed);
    req.set_message_count(5);
    req.set_cur_time(0);
    req.set_user_id("");
    req.set_session_id("");

    stub.GetRecentMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    ASSERT_EQ(5, rsp.message_info_size());

    const auto &m0 = rsp.message_info(0).message_content();
    EXPECT_EQ(imserver::MessageType::STRING, m0.message_type());
    EXPECT_EQ("mix_text_a", m0.string_message().content());

    const auto &m1 = rsp.message_info(1).message_content();
    EXPECT_EQ(imserver::MessageType::IMAGE, m1.message_type());
    EXPECT_EQ("mix_img_uuid_001", m1.image_message().file_id());

    const auto &m2 = rsp.message_info(2).message_content();
    EXPECT_EQ(imserver::MessageType::FILE, m2.message_type());
    EXPECT_EQ("mix_file_uuid_002", m2.file_message().file_id());
    EXPECT_EQ("notes.txt", m2.file_message().file_name());
    EXPECT_EQ(2048, m2.file_message().file_size());

    const auto &m3 = rsp.message_info(3).message_content();
    EXPECT_EQ(imserver::MessageType::SPEECH, m3.message_type());
    EXPECT_EQ("mix_sp_uuid_003", m3.speech_message().file_id());

    const auto &m4 = rsp.message_info(4).message_content();
    EXPECT_EQ(imserver::MessageType::STRING, m4.message_type());
    EXPECT_EQ("mix_text_b", m4.string_message().content());
}

/// 最近 3 条：时间倒序取 im_mix_s2、im_mix_sp、im_mix_file，reverse 后为 文件→语音→文字
TEST_F(MsgStorageRecentMixedMediaTest, RecentThreeEndsWithNewestTextMessage)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetRecentMessageRequest req;
    imserver::GetRecentMessageResponse rsp;

    req.set_request_id("itest_recent_mixed_n3");
    req.set_chat_session_id(kChatSessionMixed);
    req.set_message_count(3);
    req.set_cur_time(0);
    req.set_user_id("");
    req.set_session_id("");

    stub.GetRecentMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    ASSERT_EQ(3, rsp.message_info_size());
    EXPECT_EQ("im_mix_file", rsp.message_info(0).message_id());
    EXPECT_EQ(imserver::MessageType::FILE,
              rsp.message_info(0).message_content().message_type());

    EXPECT_EQ("im_mix_sp", rsp.message_info(1).message_id());
    EXPECT_EQ(imserver::MessageType::SPEECH,
              rsp.message_info(1).message_content().message_type());

    EXPECT_EQ("im_mix_s2", rsp.message_info(2).message_id());
    EXPECT_EQ(imserver::MessageType::STRING,
              rsp.message_info(2).message_content().message_type());
    EXPECT_EQ("mix_text_b", rsp.message_info(2).message_content().string_message().content());
}

/// GetHistoryMessage：多种消息类型与时间区间
class MsgStorageHistoryMessageTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        seedHistoryMixedFixtures();
    }

    void SetUp() override
    {
        channel_ = makeChannel();
        ASSERT_NE(channel_, nullptr);
    }

    std::shared_ptr<brpc::Channel> channel_;
};

/// 窄窗覆盖会话内首条至末条消息的 UNIX 秒（由 MySQL 推导），拉取全部 5 条，按时间升序校验各 MessageType
TEST_F(MsgStorageHistoryMessageTest, NarrowSpanFromFirstToLast_ReturnsFiveMessagesAllTypesInOrder)
{
    const std::int64_t tsFirst = mysqlMessageCreateUnix("im_hist_s1");
    const std::int64_t tsLast = mysqlMessageCreateUnix("im_hist_s2");
    ASSERT_GT(tsFirst, 0);
    ASSERT_GT(tsLast, 0);

    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetHistoryMessageRequest req;
    imserver::GetHistoryMessageResponse rsp;

    req.set_request_id("itest_hist_narrow_span");
    req.set_chat_session_id(kChatSessionHistory);
    req.set_user_id("");
    req.set_session_id("");
    req.set_start_time(tsFirst - kHistoryHalfWindowSeconds);
    req.set_over_time(tsLast + kHistoryHalfWindowSeconds);

    stub.GetHistoryMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    ASSERT_EQ(5, rsp.message_info_size());

    EXPECT_EQ("im_hist_s1", rsp.message_info(0).message_id());
    EXPECT_EQ(imserver::MessageType::STRING,
              rsp.message_info(0).message_content().message_type());
    EXPECT_EQ("hist_txt_a", rsp.message_info(0).message_content().string_message().content());

    EXPECT_EQ("im_hist_img", rsp.message_info(1).message_id());
    EXPECT_EQ(imserver::MessageType::IMAGE,
              rsp.message_info(1).message_content().message_type());
    EXPECT_EQ("hist_img_uuid", rsp.message_info(1).message_content().image_message().file_id());

    EXPECT_EQ("im_hist_file", rsp.message_info(2).message_id());
    EXPECT_EQ(imserver::MessageType::FILE,
              rsp.message_info(2).message_content().message_type());
    EXPECT_EQ("hist_file_uuid", rsp.message_info(2).message_content().file_message().file_id());
    EXPECT_EQ("report.pdf", rsp.message_info(2).message_content().file_message().file_name());
    EXPECT_EQ(8192, rsp.message_info(2).message_content().file_message().file_size());

    EXPECT_EQ("im_hist_sp", rsp.message_info(3).message_id());
    EXPECT_EQ(imserver::MessageType::SPEECH,
              rsp.message_info(3).message_content().message_type());
    EXPECT_EQ("hist_sp_uuid", rsp.message_info(3).message_content().speech_message().file_id());

    EXPECT_EQ("im_hist_s2", rsp.message_info(4).message_id());
    EXPECT_EQ(imserver::MessageType::STRING,
              rsp.message_info(4).message_content().message_type());
    EXPECT_EQ("hist_txt_b", rsp.message_info(4).message_content().string_message().content());
}

/// 窄区间：以库中 FILE 行 create_time 的 Unix 秒为中心，仅应命中该条
TEST_F(MsgStorageHistoryMessageTest, NarrowRange_ReturnsOnlyFileMessageAtTenOClock)
{
    const std::int64_t fileTs = mysqlMessageCreateUnix("im_hist_file");
    ASSERT_GT(fileTs, 0);
    const std::int64_t tStart = fileTs - kHistoryHalfWindowSeconds;
    const std::int64_t tOver = fileTs + kHistoryHalfWindowSeconds;

    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetHistoryMessageRequest req;
    imserver::GetHistoryMessageResponse rsp;

    req.set_request_id("itest_hist_narrow");
    req.set_chat_session_id(kChatSessionHistory);
    req.set_user_id("");
    req.set_session_id("");
    req.set_start_time(tStart);
    req.set_over_time(tOver);

    stub.GetHistoryMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    ASSERT_EQ(1, rsp.message_info_size());
    EXPECT_EQ("im_hist_file", rsp.message_info(0).message_id());
    EXPECT_EQ(imserver::MessageType::FILE,
              rsp.message_info(0).message_content().message_type());
    EXPECT_EQ("hist_file_uuid", rsp.message_info(0).message_content().file_message().file_id());
}

TEST_F(MsgStorageHistoryMessageTest, FailsWhenUserDoesNotExist)
{
    const std::int64_t midTs = mysqlMessageCreateUnix("im_hist_file");
    ASSERT_GT(midTs, 0);

    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetHistoryMessageRequest req;
    imserver::GetHistoryMessageResponse rsp;

    req.set_request_id("itest_hist_auth");
    req.set_chat_session_id(kChatSessionHistory);
    req.set_user_id("bad_user_hist_im_gtest");
    req.set_session_id("");
    req.set_start_time(midTs - kHistoryHalfWindowSeconds);
    req.set_over_time(midTs + kHistoryHalfWindowSeconds);

    stub.GetHistoryMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_FALSE(rsp.success());
}

TEST_F(MsgStorageHistoryMessageTest, UnknownSession_ReturnsEmptySuccess)
{
    const std::int64_t midTs = mysqlMessageCreateUnix("im_hist_file");
    ASSERT_GT(midTs, 0);

    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetHistoryMessageRequest req;
    imserver::GetHistoryMessageResponse rsp;

    req.set_request_id("itest_hist_empty_sess");
    req.set_chat_session_id("im_no_such_session_hist_999");
    req.set_user_id("");
    req.set_session_id("");
    req.set_start_time(midTs - kHistoryHalfWindowSeconds);
    req.set_over_time(midTs + kHistoryHalfWindowSeconds);

    stub.GetHistoryMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    EXPECT_EQ(0, rsp.message_info_size());
}

/// start_time > over_time：MessageTable 直接返回空，RPC 仍成功
TEST_F(MsgStorageHistoryMessageTest, InvertedRange_ReturnsEmptySuccess)
{
    const std::int64_t midTs = mysqlMessageCreateUnix("im_hist_file");
    ASSERT_GT(midTs, 0);

    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetHistoryMessageRequest req;
    imserver::GetHistoryMessageResponse rsp;

    req.set_request_id("itest_hist_inverted");
    req.set_chat_session_id(kChatSessionHistory);
    req.set_user_id("");
    req.set_session_id("");
    req.set_start_time(midTs + 1000);
    req.set_over_time(midTs - 1000);

    stub.GetHistoryMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    EXPECT_EQ(0, rsp.message_info_size());
}

/// 退化区间 start_time == over_time == 库中某条消息的 UNIX 秒（闭区间两端重合），仍应命中该条
TEST_F(MsgStorageHistoryMessageTest, DegenerateSingleSecondRange_IncludesThatMessage)
{
    const std::int64_t fileTs = mysqlMessageCreateUnix("im_hist_file");
    ASSERT_GT(fileTs, 0);

    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetHistoryMessageRequest req;
    imserver::GetHistoryMessageResponse rsp;

    req.set_request_id("itest_hist_degenerate");
    req.set_chat_session_id(kChatSessionHistory);
    req.set_user_id("");
    req.set_session_id("");
    req.set_start_time(fileTs);
    req.set_over_time(fileTs);

    stub.GetHistoryMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();

    ASSERT_EQ(1, rsp.message_info_size());
    EXPECT_EQ("im_hist_file", rsp.message_info(0).message_id());
}

/// 两条相邻消息 UNIX 秒之间的严格内部区间（不含边界），中间无消息时应为空
TEST_F(MsgStorageHistoryMessageTest, StrictGapBetweenAdjacentMessages_ReturnsEmpty)
{
    const std::int64_t tsImg = mysqlMessageCreateUnix("im_hist_img");
    const std::int64_t tsFile = mysqlMessageCreateUnix("im_hist_file");
    ASSERT_GT(tsImg, 0);
    ASSERT_GT(tsFile, 0);
    ASSERT_LT(tsImg, tsFile);

    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::GetHistoryMessageRequest req;
    imserver::GetHistoryMessageResponse rsp;

    req.set_request_id("itest_hist_gap");
    req.set_chat_session_id(kChatSessionHistory);
    req.set_user_id("");
    req.set_session_id("");
    req.set_start_time(tsImg + 1);
    req.set_over_time(tsFile - 1);

    stub.GetHistoryMessage(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    EXPECT_EQ(0, rsp.message_info_size());
}

class MsgStorageMessageSearchTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        seedMessageSearchFixtures();
    }

    void SetUp() override
    {
        channel_ = makeChannel();
        ASSERT_NE(channel_, nullptr);
    }

    std::shared_ptr<brpc::Channel> channel_;
};

/// 指定 chat_session_id 搜索 token：结果至少覆盖 STRING/IMAGE/FILE/SPEECH 四种类型
/// 备注：当前 ES bool 查询未设置 minimum_should_match，可能附带返回同会话其它文档。
TEST_F(MsgStorageMessageSearchTest, SessionSearchContainsAllFourTypes)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::MessageSearchRequest req;
    imserver::MessageSearchResponse rsp;
    req.set_request_id("itest_search_all_types");
    req.set_user_id("");
    req.set_session_id("");
    req.set_chat_session_id(kChatSessionSearch);
    req.set_search_key(kSearchToken);

    stub.MessageSearch(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    ASSERT_GE(rsp.message_info_size(), 4);

    bool seenText = false;
    bool seenImage = false;
    bool seenFile = false;
    bool seenSpeech = false;
    for (const auto &info : rsp.message_info())
    {
        if (info.message_id() == "im_search_txt")
        {
            seenText = true;
            EXPECT_EQ(imserver::MessageType::STRING, info.message_content().message_type());
            EXPECT_EQ(std::string(kSearchToken) + " text payload",
                      info.message_content().string_message().content());
        }
        else if (info.message_id() == "im_search_img")
        {
            seenImage = true;
            EXPECT_EQ(imserver::MessageType::IMAGE, info.message_content().message_type());
            EXPECT_EQ("search_img_file_id", info.message_content().image_message().file_id());
        }
        else if (info.message_id() == "im_search_file")
        {
            seenFile = true;
            EXPECT_EQ(imserver::MessageType::FILE, info.message_content().message_type());
            EXPECT_EQ("search_file_file_id", info.message_content().file_message().file_id());
            EXPECT_EQ("spec.pdf", info.message_content().file_message().file_name());
            EXPECT_EQ(4096, info.message_content().file_message().file_size());
        }
        else if (info.message_id() == "im_search_speech")
        {
            seenSpeech = true;
            EXPECT_EQ(imserver::MessageType::SPEECH, info.message_content().message_type());
            EXPECT_EQ("search_speech_file_id", info.message_content().speech_message().file_id());
        }
        EXPECT_EQ(kChatSessionSearch, info.chat_session_id());
    }
    EXPECT_TRUE(seenText);
    EXPECT_TRUE(seenImage);
    EXPECT_TRUE(seenFile);
    EXPECT_TRUE(seenSpeech);
}

/// 边界：chat_session_id 为空时不做会话过滤，应至少包含旁路会话文档
TEST_F(MsgStorageMessageSearchTest, EmptyChatSessionSearchesAcrossSessions)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::MessageSearchRequest req;
    imserver::MessageSearchResponse rsp;
    req.set_request_id("itest_search_global");
    req.set_user_id("");
    req.set_session_id("");
    req.set_chat_session_id("");
    req.set_search_key(kSearchToken);

    stub.MessageSearch(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    ASSERT_GE(rsp.message_info_size(), 5);

    bool seenOtherSession = false;
    for (const auto &info : rsp.message_info())
    {
        if (info.message_id() == "im_search_other_session")
        {
            seenOtherSession = true;
            EXPECT_EQ(kChatSessionSearchOther, info.chat_session_id());
        }
    }
    EXPECT_TRUE(seenOtherSession);
}

/// 边界：在当前查询构造下（must + should 且 should 非强制），未知关键字仍会返回会话内文档
TEST_F(MsgStorageMessageSearchTest, UnknownKeywordStillReturnsSessionMessages)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::MessageSearchRequest req;
    imserver::MessageSearchResponse rsp;
    req.set_request_id("itest_search_empty");
    req.set_user_id("");
    req.set_session_id("");
    req.set_chat_session_id(kChatSessionSearch);
    req.set_search_key("im_gtest_search_not_exists_zzz");

    stub.MessageSearch(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(rsp.success()) << rsp.error_message();
    ASSERT_GE(rsp.message_info_size(), 1);
    for (const auto &info : rsp.message_info())
    {
        EXPECT_EQ(kChatSessionSearch, info.chat_session_id());
    }
}

/// 边界：用户身份校验失败时应直接失败
TEST_F(MsgStorageMessageSearchTest, FailsWhenUserDoesNotExist)
{
    imserver::MessageStorageService_Stub stub(channel_.get());
    brpc::Controller cntl;

    imserver::MessageSearchRequest req;
    imserver::MessageSearchResponse rsp;
    req.set_request_id("itest_search_auth_fail");
    req.set_user_id("bad_user_search_im_gtest");
    req.set_session_id("");
    req.set_chat_session_id(kChatSessionSearch);
    req.set_search_key(kSearchToken);

    stub.MessageSearch(&cntl, &req, &rsp, nullptr);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_FALSE(rsp.success());
}

} // namespace

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    testing::AddGlobalTestEnvironment(new MsgStorageIntegrationEnvironment);
    return RUN_ALL_TESTS();
}
