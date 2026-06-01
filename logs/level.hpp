// 定义枚举类
// 提供转换接口，将枚举类转换为对应的字符串
#ifndef __M_LEVEL_HPP
#define __M_LEVEL_HPP

namespace ppalog
{
    class LogLevel
    {
        public:
            enum class value
            {
                UNKNOW=0,
                DEBUG=1,
                INFO=2,
                WARN=3,
                ERROR=4,
                FATAL=5,
                OFF=6
            };
            static const char*toString(LogLevel::value level) 
            {
                switch(level)
                {
                    case LogLevel::value::UNKNOW:
                        return "UNKNOW";
                    case LogLevel::value::DEBUG:
                        return "DEBUG";
                    case LogLevel::value::INFO:
                        return "INFO";
                    case LogLevel::value::WARN:
                        return "WARN";
                    case LogLevel::value::ERROR:
                        return "ERROR";
                    case LogLevel::value::FATAL:
                        return "FATAL";
                    default:
                        return "UNKNOW";
                }
            }
            
    };
}

#endif