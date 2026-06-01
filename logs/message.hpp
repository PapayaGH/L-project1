// 定义日志消息类，进行日志中间信息的存储
// 1.日志的输出时间
// 2.日志的输出级别
// 3.源文件名称
// 4.源文件行号
// 5.线程id
// 6.日志主体消息
// 7.日志器的名称
#ifndef __M_MESSAGE_HPP_
#define __M_MESSAGE_HPP_
#include<iostream>
#include<thread>
#include<string>
#include"level.hpp"
#include"util.hpp"

namespace ppalog
{
    struct LogMsg
    {
      time_t _ctime;   //日志的输出时间
      LogLevel::value _level;  //日志的输出级别 
      size_t _line;    //源文件行号
      std::thread::id _tid;  //线程id
      std::string _file;     //源文件名称
       std::string _logger;  //日志器的名称
      std::string _payload;      //日志主体消息
      
      LogMsg(LogLevel::value level,size_t line,const std::string file,const std::string logger,const std::string msg)
      :
        _ctime(util::Date::getTime()),
        _level(level),
        _line(line),
        _tid(std::this_thread::get_id()),
        _file(file),
        _logger(logger),
        _payload(msg)
        
      {}
    };
}

#endif