#pragma once

#include <ctime>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <iomanip>
#include <random>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <net/if.h>
#include <errno.h>

#include "log.hpp"

namespace imserver
{

/// @brief 将 Unix 纪元秒转换为服务器本地时区的 naive ptime（与 MySQL TIMESTAMP 经 ODB 读出的 create_time 语义对齐）
inline boost::posix_time::ptime unixSecondsToServerLocalPtime(std::time_t epochSeconds)
{
#ifdef _WIN32
    std::tm tmBuf{};
    localtime_s(&tmBuf, &epochSeconds);
#else
    std::tm tmBuf{};
    localtime_r(&epochSeconds, &tmBuf);
#endif
    namespace pt = boost::posix_time;
    namespace gd = boost::gregorian;
    const gd::date d(
        tmBuf.tm_year + 1900,
        tmBuf.tm_mon + 1,
        tmBuf.tm_mday);
    const pt::time_duration td(tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    return pt::ptime(d, td);
}

/// @brief 将 naive ptime（按服务器本地墙钟理解，与入库一致）转为 Unix 纪元秒，用于 MessageInfo.timestamp 等
inline std::time_t serverLocalPtimeToUnixSeconds(const boost::posix_time::ptime &pt)
{
    if (pt.is_special())
        return 0;
    std::tm tmBuf{};
    tmBuf.tm_year = static_cast<int>(pt.date().year()) - 1900;
    tmBuf.tm_mon = static_cast<int>(pt.date().month()) - 1;
    tmBuf.tm_mday = static_cast<int>(pt.date().day());
    tmBuf.tm_hour = static_cast<int>(pt.time_of_day().hours());
    tmBuf.tm_min = static_cast<int>(pt.time_of_day().minutes());
    tmBuf.tm_sec = static_cast<int>(pt.time_of_day().seconds());
    tmBuf.tm_isdst = -1;
    return std::mktime(&tmBuf);
}

/// @brief 获取第一张有效网卡的12位无冒号MAC地址
/// @return 返回第一张有效网卡的12位无冒号MAC地址,获取失败返回"000000000000"
std::string getFirstMacAddress() {
    struct ifaddrs* ifList = nullptr;
    struct ifaddrs* iface = nullptr;
    std::string firstMac(12,'0');

    // 1. 获取所有网络接口列表，失败则返回空字符串
    if (getifaddrs(&ifList) == -1) {
        LOG_ERROR("获取网络接口失败:{}",strerror(errno));
        return firstMac;
    }

    // 2. 遍历所有网络接口，查找第一张有效网卡
    for (iface = ifList; iface != nullptr; iface = iface->ifa_next) {
        // 过滤条件：接口已启用、非环回网卡、AF_PACKET类型（存储MAC地址）
        if (!(iface->ifa_flags & IFF_UP) || 
            (iface->ifa_flags & IFF_LOOPBACK) || 
            iface->ifa_addr->sa_family != AF_PACKET) {
            continue;
        }

        // 3. 提取MAC地址（标准6字节），直接格式化为12位无冒号字符串
        struct sockaddr_ll* sockAddr = (struct sockaddr_ll*)iface->ifa_addr;
        if (sockAddr->sll_halen == 6) {
            char macBuf[13] = {0}; // 12位有效字符 + 1位结束符
            // 无冒号拼接6字节十六进制，生成12位字符串
            snprintf(macBuf, sizeof(macBuf), "%02X%02X%02X%02X%02X%02X",
                     sockAddr->sll_addr[0], sockAddr->sll_addr[1], sockAddr->sll_addr[2],
                     sockAddr->sll_addr[3], sockAddr->sll_addr[4], sockAddr->sll_addr[5]);
            firstMac = std::string(macBuf);
            break; // 找到第一张有效网卡，立即退出循环
        }
    }

    // 4. 释放网络接口列表内存
    freeifaddrs(ifList);
    return firstMac;
}

/// @brief 生成一个由 12位MAC地址+6个占1字节的16进制随机数+1个2字节自增长16进制数 作为唯一ID
/// @return 返回 总长度为28位(12+12+4) 字符串形式的uuid
std::string uuid()
{
    std::random_device rd;//实例化设备随机数对象-用于生成设备随机数
    std::mt19937 generator(rd());//以设备随机数为种子，实例化伪随机数对象
    std::uniform_int_distribution<int> distribution(0,255); //限定数据范围

    std::stringstream ss;

    //1.获取机器第一张网卡的MAC地址
    ss << getFirstMacAddress();
    ss <<"-";//添加分隔符

    //2.生成6个两数位(1字节)的16进制随机数
    for (int i = 0; i < 6; i++) 
    {
        ss << std::setw(2) << std::setfill('0') << std::hex << distribution(generator);
    }
    ss << "-";//添加分隔符

    //3.通过一个静态变量生成一个2字节的编号数字--生成4位16进制数字字符
    static std::atomic<unsigned short> idx(0);
    unsigned short tmp = idx.fetch_add(1);
    ss << std::setw(4) << std::setfill('0') << std::hex << tmp;
    return ss.str();
}


std::string vCode()
{
    std::random_device rd;//实例化设备随机数对象-用于生成设备随机数
    std::mt19937 generator(rd());//以设备随机数为种子，实例化伪随机数对象
    std::uniform_int_distribution<int> distribution(0,9); //限定数据范围

    std::stringstream ss;

    //1.生成6个1位(1字节)的随机数
    for (int i = 0; i < 6; i++) 
    {
        ss << std::setw(1) << std::setfill('0') << std::dec << distribution(generator);
    }
    return ss.str();
}

/// @brief 读取文件内容
/// @param pathName 文件路径
/// @param des 输出型参数
/// @return 成功返回true,否则返回false
bool readFile(const std::string &pathName,std::string &des)
{
    std::ifstream ifs(pathName,std::ios::binary);
    if(ifs.is_open() == false)//打开文件失败
    {
        return false;
    }

    ifs.seekg(0,std::ios::end);//将读取位置移到文件末尾位置
    int fileSize=ifs.tellg();  //获取文件大小
    ifs.seekg(0,std::ios::beg);//将读取位置移到文件起始位置

    //如果文件大小为0,直接返回
    if(0 == fileSize) return true;

    des.resize(fileSize);
    ifs.read(&(des[0]),fileSize); //读取整个文件

    //如果出现任何错误
    if(ifs.good()==false)
    {
        ifs.close();//关闭文件
        return false;
    }

    ifs.close();//关闭文件

    return true;
}


/// @brief 向文件内覆盖写内容
/// @param pathName 文件路径
/// @param src 需要写入文件的内容
/// @return 成功返回true,否则返回false
bool writeFile(const std::string &pathName,const std::string &src)
{
    std::ofstream ofs(pathName,std::ios::binary|std::ios::trunc);
    if(ofs.is_open() == false)//打开文件失败
    {
        return false;
    }

    ofs.write(src.c_str(),src.size());

    //如果出现任何错误
    if(ofs.good()==false)
    {
        ofs.close();//关闭文件
        return false;
    }

    ofs.close();//关闭文件

    return true;

}


}