#ifndef __M_PPALOG_H__
#define __M_PPALOG_H__
#include"logger.hpp"
namespace ppalog
{
    //提供获取指定日志器的全局接口（避免用户自己操作单例对象）
    Logger::ptr getLogger(const std::string &name)
    {
        return ppalog::LoggerManager::getInstance().getLogger(name);
    }
    Logger::ptr rootLogger()
    {
        return ppalog::LoggerManager::getInstance().rootLogger();
    }
    //使用宏函数对对日志器的接口进行代理
    #define debug(fmt,...) debug(__FILE__,__LINE__,fmt,##__VA_ARGS__)
    #define info(fmt,...) info(__FILE__,__LINE__,fmt,##__VA_ARGS__)
    #define warn(fmt,...) warn(__FILE__,__LINE__,fmt,##__VA_ARGS__)
    #define error(fmt,...) error(__FILE__,__LINE__,fmt,##__VA_ARGS__)
    #define fatal(fmt,...) fatal(__FILE__,__LINE__,fmt,##__VA_ARGS__)
    //提供宏函数，直接通过默认日志器进行日志的标准输出打印
    #define DEBUG(fmt,...) ppalog::rootLogger()->debug(fmt,##__VA_ARGS__)
    #define INFO(fmt,...) ppalog::rootLogger()->info(fmt,##__VA_ARGS__)
    #define WARN(fmt,...) ppalog::rootLogger()->warn(fmt,##__VA_ARGS__)
    #define ERROR(fmt,...) ppalog::rootLogger()->error(fmt,##__VA_ARGS__)
    #define FATAL(fmt,...) ppalog::rootLogger()->fatal(fmt,##__VA_ARGS__)

}




#endif