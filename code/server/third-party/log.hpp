#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/async.h>
#include <iostream>

namespace Log{
    /// @brief 全局日志对象
    std::shared_ptr<spdlog::logger> gLogger;

    /// @brief 初始化全局日志器
    /// @param isRelease 是否为release模式
    /// @param file release模式有效,表示日志输出到哪个文件内
    /// @param level release模式有效,表示哪个日志级别及其以上的日志将会输出
    void initGlogger(bool isRelease,const std::string &file="log.txt",spdlog::level::level_enum level=spdlog::level::level_enum::info)
    {
        if(isRelease)//如果是release模式
        {
            gLogger=spdlog::basic_logger_mt("gLoggerRelease",file);//创建文件日志器
            gLogger->set_level(level);//设置日志级别
            gLogger->flush_on(level);//设置日志刷新级别
        }
        else //否则是debug模式
        {
            gLogger=spdlog::stdout_color_mt("gLoggerDebug");//创建控制台日志器
            gLogger->set_level(spdlog::level::level_enum::trace);//设置日志级别为trace
            gLogger->flush_on(spdlog::level::level_enum::trace);//设置日志刷新级别为trace
        }

        //设置日志器的输出格式
        gLogger->set_pattern("[%n][%Y-%m-%d %H:%M:%S][%t][%-8l]%v");
    }

}

#define LOG_TRACE(format,...) Log::gLogger->trace(std::string("[{}:{}]")+format,__FILE__,__LINE__,##__VA_ARGS__);
#define LOG_DEBUG(format,...) Log::gLogger->debug(std::string("[{}:{}]")+format,__FILE__,__LINE__,##__VA_ARGS__);
#define LOG_INFO(format,...)  Log::gLogger->info (std::string("[{}:{}]")+format,__FILE__,__LINE__,##__VA_ARGS__);
#define LOG_WARN(format,...)  Log::gLogger->warn (std::string("[{}:{}]")+format,__FILE__,__LINE__,##__VA_ARGS__);
#define LOG_ERROR(format,...) Log::gLogger->error(std::string("[{}:{}]")+format,__FILE__,__LINE__,##__VA_ARGS__);
#define LOG_FATAL(format,...) Log::gLogger->critical(std::string("[{}:{}]")+format,__FILE__,__LINE__,##__VA_ARGS__);  