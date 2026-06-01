#include"../logs/ppalog.h"
#include<unistd.h>

//功能测试
//扩展一个以时间为日志文件滚动切换类型的日志落地模块
//1.以时间进行文件滚动，实际上是以时间段进行滚动
//实现思想：time（nullptr）%gap；以当前系统时间取模时间段大小，可以得到当前时间段是第几个时间段
//time（nullptr）%60；当前就是第n个60s；每次以当前系统时间取模，判断与当前文件时间是否一致，如果不一致就切换文件
enum class TimeGap
{
    GAP_SECOND,
    GAP_MINUTE,
    GAP_HOUR,
    GAP_DAY,
};
class RollByTimeSink:public ppalog::LogSink
    {
        public:
           RollByTimeSink(const std::string &basename,TimeGap gap_type):_basename(basename)
           {
            switch(gap_type)
            {
                case TimeGap::GAP_SECOND:
                    _gap_size=1;
                    break;
                case TimeGap::GAP_MINUTE:
                    _gap_size=60;
                    break;
                case TimeGap::GAP_HOUR: 
                    _gap_size=60*60;
                    break;
                case TimeGap::GAP_DAY:
                    _gap_size=60*60*24;
                    break;
            }
            _cur_gap=_gap_size==1 ? ppalog::util::Date::getTime() : ppalog::util::Date::getTime()%_gap_size;//获取当前时间段编号
            std::string filename=createNewFile();
            ppalog::util::File::createDirectory(ppalog::util::File::path(filename));
            _ofs.open(filename,std::ios::binary|std::ios::app);
            assert(_ofs.is_open());
           }
        //将日志消息写入到指定文件,写入前判断文件时间段，超过了当前时间段就要切换文件
           void log(const char *data,size_t len)
           {
            time_t cur=ppalog::util::Date::getTime();
            if((cur%_gap_size)!=_cur_gap)
            {
                _ofs.close();  
                std::string filename=createNewFile();
                _ofs.open(filename,std::ios::binary|std::ios::app);
                assert(_ofs.is_open());

            }
            _ofs.write(data,len);
            assert(_ofs.good());
           }
        private:
           std::string createNewFile()
           {
            time_t t=ppalog::util::Date::getTime();
            struct tm lt;
            localtime_r(&t,&lt);
            std::stringstream filename;
            filename<<_basename;
            filename<<lt.tm_year+1900;
            filename<<lt.tm_mon+1;
            filename<<lt.tm_mday;
            filename<<lt.tm_hour;
            filename<<lt.tm_min;
            filename<<lt.tm_sec;
            filename<<".log";
            return filename.str();
           }
        private:
           std::string _basename;  //文件名称   
           std::ofstream _ofs;  //文件输出流对象
           size_t _cur_gap;//当前时间段编号
           size_t _gap_size;//时间段大小，以秒为单位
    };



    int main()
    {
        std::unique_ptr<ppalog::LoggerBuilder> builder(new ppalog::GlobalLoggerBuilder());
        builder->buildLoggerName("async_logger");
        builder->buildLoggerLevel(ppalog::LogLevel::value::WARN);
        builder->buildFormatter("[%c][%f:%l]%m%n");
        builder->buildLoggerType(ppalog::LoggerType::LOGGER_ASYNC); 
        // builder->buildEnableUnSafeAsync();
        // builder->buildSink<ppalog::FileSink>("./logfile/async.log");
        // builder->buildSink<ppalog::StdoutSink>();
        builder->buildSink<RollByTimeSink>("./logfile/roll-async-by-time",TimeGap::GAP_SECOND);
        ppalog::Logger::ptr logger=builder->build(); 
        size_t cur=ppalog::util::Date::getTime();
        while(ppalog::util::Date::getTime()<cur+5)
        {
            logger->fatal("这是一条测试日志");
            usleep(1000);
        }
     

        return 0;
    }