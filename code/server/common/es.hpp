#pragma once

#include <json/json.h>
#include <elasticlient/client.h>
#include <cpr/cpr.h>
#include <sstream>
#include <memory>
#include <unordered_set>

#include "log.hpp"

namespace imserver{

/// @brief 将Json::Value序列化为字符串
/// @param src 输入参数：待序列化的 Json::Value 格式数据
/// @param dest 输出参数：序列化后得到的 std::string 格式字符串
/// @return 序列化成功返回 true，失败返回 false
bool serialize(const Json::Value &src,std::string & dest)
{
    //创建json流写入器构建器
    Json::StreamWriterBuilder swBuilder;

    //通过构建器创建 流写入器实例
    std::unique_ptr<Json::StreamWriter> sw(swBuilder.newStreamWriter());

    std::ostringstream oss;

    //将json序列化成字符串
    if(sw->write(src,&oss) != 0)
    {
        LOG_ERROR("json序列化失败");

        return false;
    }

    dest=oss.str();
    return true;
};


/// @brief 将 std::string 字符串反序列化为 Json::Value 对象
/// @param src 输入参数：待反序列化的 std::string 格式字符串
/// @param dest 输出参数：反序列化后得到的 Json::Value 格式数据
/// @return 反序列化成功返回 true，失败返回 false
bool parse(const std::string &src,Json::Value &dest)
{
    // 创建 Jsoncpp 的字符读取器构建器，用于构建 CharReader 反序列化工具对象
    Json::CharReaderBuilder crBuilder;

    //通过构建器创建 CharReader 实例,
    // CharReader 是 Jsoncpp 中负责执行具体反序列化操作的核心类
    std::unique_ptr<Json::CharReader> cr(crBuilder.newCharReader());

    std::string errs;//保存解析的错误信息
    
    //解析src到dest内
    if(cr->parse(src.c_str(),src.c_str()+src.size(),&dest,&errs) == false)
    {
        LOG_ERROR("json反序列化失败,错误信息:{},反序列化字符串:{}",errs.c_str(),src.c_str());
        return false;
    }

    return true;
}


/// @brief 封装ES新增索引操作的类
class ESIndex
{
public:
    /// @brief 构造函数,提供索引的相关信息
    /// @param client ES客户端
    /// @param name 索引名字
    /// @param id 索引id
    /// @param type 索引内文档(数据)的类型
    ESIndex(const std::shared_ptr<elasticlient::Client> &client,
            const std::string &name,
            const std::string &id="",
            const std::string &type="_doc"):
            _client(client),
            _name(name),
            _type(type),
            _id(id)
    {}

    /// @brief 新增一个字段到索引中
    /// @param key 字段名
    /// @param type 字段类型，默认是text
    /// @param analyzer 分词器，默认是ik_max_word
    /// @param enabled 是否开启索引，默认是true
    /// @return 返回当前对象引用，支持链式调用
    ESIndex &append(const std::string &key,
                    const std::string &type="text",
                    const std::string &analyzer="ik_max_word",
                    const bool enabled=true)
    {
        Json::Value jsKey;
        jsKey["type"]=type;
        jsKey["analyzer"]=analyzer;
        if(enabled == false) jsKey["enabled"]=false;

        _properties[key]=jsKey;


        return *this;
    }

    /// @brief 往ES服务器创建索引
    /// @return 创建成功返回true，失败返回false
    bool create()
    {
        createSettings();
        createMappings();

        //将_index进行序列化,存入body
        std::string body;
        if(serialize(_index,body)==false)
        {
            return false;
        }

        LOG_DEBUG("创建索引请求正文:{}",body);
        
        try
        {
            auto rsp=_client->index(_name,_type,_id,body);
            LOG_INFO("创建索引 {} 请求的响应状态码:{},响应正文:{}",_name,rsp.status_code,rsp.text);
            if(rsp.status_code<200 || rsp.status_code>=300) return false;
        }
        catch(const std::exception& e)
        {
            LOG_ERROR("创建索引出现异常:{}",e.what());
            return false;
        }

        return true;
    }

private:
    /// @brief 创建索引的设置
    void createSettings()
    {
        Json::Value ik;
        ik["tokenizer"]="ik_max_word";

        Json::Value analyzer;
        analyzer["ik"]=ik;

        Json::Value analysis;
        analysis["analyzer"]=analyzer;

        Json::Value settings;
        settings["analysis"]=analysis;

        _index["settings"]=settings;
    }


    /// @brief 创建索引的映射
    void createMappings()
    {
        Json::Value mappings;
        mappings["dynamic"]=true;
        mappings["properties"]=_properties;

        _index["mappings"]=mappings;
    }

private:
    /// @brief ES客户端
    std::shared_ptr<elasticlient::Client> _client;
    /// @brief 索引名
    std::string _name;
    /// @brief 索引内文档(数据)的类型
    std::string _type;
    /// @brief 索引id
    std::string _id;
    /// @brief mappings:properties字段
    Json::Value _properties;
    /// @brief 创建索引请求的完整正文(json)
    Json::Value _index;
};


/// @brief 封装ES新增文档(数据)操作的类
class ESInsert
{
public:
    /// @brief 构造函数,提供文档(数据)的相关信息
    /// @param client ES客户端
    /// @param name 文档(数据)所属的索引的名字
    /// @param id 文档(数据)的id
    /// @param type 文档(数据)的类型
    ESInsert(const std::shared_ptr<elasticlient::Client> &client,
            const std::string &name,
            const std::string &id="",
            const std::string &type="_doc"):
            _client(client),
            _name(name),
            _type(type),
            _id(id)
    {}

    /// @brief 往文档(数据)中新增一个字段(键值对)
    /// @tparam T 字段的值的类型
    /// @param key 键
    /// @param value 值
    /// @return 返回当前对象
    template<class T>
    ESInsert &append(const std::string &key,const T &value)
    {
        _data[key]=value;
        return *this;
    }

    /// @brief 往ES服务端中插入文档(数据)
    /// @return 插入成功返回true,否则返回false
    bool insert()
    {
        std::string body;
        if(serialize(_data,body)==false)
        {
            return false;
        }

        LOG_DEBUG("插入文档(数据)请求正文:{}",body);
        
        try
        {
            auto rsp=_client->index(_name,_type,_id,body);
            LOG_INFO("插入文档(数据) {} 请求的响应状态码:{},响应正文:{}",_name,rsp.status_code,rsp.text);
            if(rsp.status_code<200 || rsp.status_code>=300) return false;
        }
        catch(const std::exception& e)
        {
            LOG_ERROR("插入文档(数据)出现异常:{}",e.what());
            return false;
        }

        return true;
    }

private:
    /// @brief ES客户端
    std::shared_ptr<elasticlient::Client> _client;
    /// @brief 文档(数据)所属的索引的名字
    std::string _name;
    /// @brief 文档(数据)的类型 
    std::string _type;
    /// @brief 文档(数据)的id
    std::string _id;
    /// @brief 文档(数据)的json格式
    Json::Value _data;
};

/// @brief 封装ES删除文档(数据)操作的类
class ESRemove
{
public:
    /// @brief 构造函数,提供要删除的文档(数据)的信息
    /// @param client ES客户端
    /// @param name 文档(数据)所属的索引的名字
    /// @param id 文档(数据)id
    /// @param type 文档(数据)的类型
    ESRemove(const std::shared_ptr<elasticlient::Client> &client,
            const std::string &name,
            const std::string &id="",
            const std::string &type="_doc"):
            _client(client),
            _name(name),
            _type(type),
            _id(id)
    {}

    /// @brief 在ES服务端内移除文档(数据)
    /// @return 移除成功返回true,否则返回false
    bool remove()
    {
        try
        {
            auto rsp=_client->remove(_name,_type,_id);
            LOG_INFO("删除文档(数据) {} 请求的响应状态码:{},响应正文:{}",_name,rsp.status_code,rsp.text);
            if(rsp.status_code<200 || rsp.status_code>=300) return false;
        }
        catch(const std::exception& e)
        {
            LOG_ERROR("删除文档(数据)出现异常:{}",e.what());
            return false;
        }

        return true;
    }

private:
    /// @brief ES客户端
    std::shared_ptr<elasticlient::Client> _client;
    /// @brief 文档(数据)所属的索引的名字
    std::string _name;
    /// @brief 文档(数据)的类型 
    std::string _type;
    /// @brief 文档(数据)id
    std::string _id;
};

/// @brief 封装ES查询操作的类
class ESSearch
{
public:
    /// @brief 构造函数,初始化与查询操作有关的信息
    /// @param client ES客户端
    /// @param name 索引名字
    /// @param type 文档(数据)的类型
    ESSearch(const std::shared_ptr<elasticlient::Client> &client,
             const std::string &name,
             const std::string &type="_doc"):
             _client(client),
             _name(name),
             _type(type)
    {}    

    /// @brief 新增查询子句
    /// @param boolConditionType 条件表达式类型,例如must,should,must_no等
    /// @param searchClauseType 搜索子句类型,例如match,terms等
    /// @param key 键
    /// @param value 值
    /// @return 
    ESSearch &append(const std::string &boolConditionType,
                const std::string &searchClauseType,
                const std::string &key,
                const std::string &value)
    {
        ///判断是不是合法的boolConditionType和searchClauseType
        if(_boolConditionTypes.count(boolConditionType) &&
           _searchClauseTypes.count(searchClauseType))
        {

            Json::Value kv;
            kv[key]=value;

            Json::Value searchClause;
            searchClause[searchClauseType]=kv;

            _bool[boolConditionType].append(searchClause);
        }

        return *this;
    }

    /// @brief 向ES服务器内发送查询请求
    /// @return 请求后返回请求的结果
    Json::Value search()
    {
        //创建完整的查询正文
        createBody();

        //对json格式的正文_body进行序列化
        std::string body;
        if(serialize(_body,body)==false)
        {
            return Json::Value();
        }

        LOG_DEBUG("查询请求正文:{}",body);

        //客户端发起查询请求
        cpr::Response rsp;
        try
        {
            rsp=_client->search(_name,_type,body);
            LOG_INFO("查询 {} 请求的响应状态码:{},响应正文:{}",_name,rsp.status_code,rsp.text);
            if(rsp.status_code<200 || rsp.status_code>=300) return Json::Value();
        }
        catch(const std::exception& e)
        {
            LOG_ERROR("查询出现异常:{}",e.what());
            return Json::Value();
        }

        //对响应正文进行反序列化
        Json::Value rspJson;
        if(parse(rsp.text,rspJson)==false)
        {
            return Json::Value();
        }

        return rspJson["hits"]["hits"];
    }

private:
    /// @brief 创建完整的查询正文
    void createBody()
    {
        //构造完整json格式的查询请求正文
        Json::Value query;
        query["bool"]=_bool;
        _body["query"]=query;
    }

private:
    /// @brief 有效的bool条件,例如must,must_not,should等
    static const std::unordered_set<std::string> _boolConditionTypes;

    /// @brief 有效的查询子句,例如match,terms等
    static const std::unordered_set<std::string> _searchClauseTypes;

    /// @brief ES客户端
    std::shared_ptr<elasticlient::Client> _client;

    /// @brief 索引名字
    std::string _name;

    /// @brief 文档(数据)的类型
    std::string _type;

    /// @brief 查询表达式
    Json::Value _bool;
    /// @brief 查询请求
    Json::Value _body;
};

const std::unordered_set<std::string> ESSearch::_boolConditionTypes={
    "must",
    "must_not",
    "should"
};

const std::unordered_set<std::string> ESSearch::_searchClauseTypes={
    "match",
    "terms"
};



}