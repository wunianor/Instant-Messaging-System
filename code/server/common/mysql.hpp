#pragma once


#include <memory>
#include <odb/forward.hxx>
#include <odb/mysql/connection-factory.hxx>
#include <odb/mysql/forward.hxx>
#include <odb/database.hxx>       // odb::database 完整定义
#include <odb/transaction.hxx>    // odb::transaction 完整定义
#include <odb/mysql/database.hxx> // MySQL数据库的具体实现
#include <odb/mysql/transaction.hxx> // MySQL事务的具体实现

namespace imserver
{

    
class ODBFactory
{
public:
    static std::shared_ptr<odb::core::database> create(
        const std::string &user,
        const std::string &password,
        const std::string &host,
        const std::string &db,
        const std::string &characterSet,
        uint16_t port,
        int connectionPoolCnt
    )
    {
        std::unique_ptr<odb::mysql::connection_pool_factory> cpf(new odb::mysql::connection_pool_factory(connectionPoolCnt));

        return std::make_shared<odb::mysql::database>(
            user,
            password,
            db,
            host,
            port,
            "",
            characterSet,
            0,
            std::move(cpf)
        );
    }

};




}