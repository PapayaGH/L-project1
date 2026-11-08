//日志落地模块的实现
//1.抽象落地基类
//2.派生子类
//3.使用工厂模式进行创建和表示的分离
#ifndef __M_SINK_HPP_
#define __M_SINK_HPP_
#include"util.hpp"
#include<memory>
#include<fstream>
#include<cassert>
#include<sstream>
#include<atomic>
#include<mutex>
#include<cerrno>
#include<cstring>


namespace ppalog
{
    class LogSink
    {
        public:
            using ptr=std::shared_ptr<LogSink>;
            LogSink() {}
            virtual ~LogSink() {}
            virtual void log(const char *data,size_t len)=0;
    };
    //落地方向：标准输出
    class StdoutSink:public LogSink
    {
        public:
        //将日志消息写入到标准输出-写入前判断文件大小，超过了最大大小就要切换文件
           void log(const char *data,size_t len)
           {
            std::cout.write(data,len);
           }
    };
    //落地方向：指定文件
    //失败语义（Debug 与 Release 完全一致，不依赖 assert）：
    //  1) 构造时打开失败、或运行期写入失败，立即把该 sink 标记为 failed()，并记录首次失败原因（含 errno）；
    //  2) 一旦 failed()，后续写入全部丢弃并累加 droppedCount()/droppedBytes()，不再做无效 IO、不再重复报错；
    //  3) 不抛出异常，日志调用路径保持无异常语义；
    //  4) fail-stop 以 sink 为单位：同一 logger 的其它 sink 不受影响。
    //注意：ofstream 写入非原子，失败瞬间最后一条记录可能被截断，需配合记录级长度/校验才能识别。
    class FileSink:public LogSink
    {
        public:
           FileSink(const std::string &pathname):_pathname(pathname),_failed(false),
            _dropped_count(0),_dropped_bytes(0)
           {
              //1.创建文件所在的目录
              util::File::createDirectory(util::File::path(pathname));
              //2.创建并打开日志文件
              _ofs.open(_pathname,std::ios::binary|std::ios::app);
              if(!_ofs.is_open())
                  markFailed("open failed");
           }
        //按记录计数：以 '\n' 作为记录分隔符（与 %n 对齐），末尾不足一条也计 1 条
           static size_t countRecords(const char *data,size_t len)
           {
              size_t n=0;
              for(size_t i=0;i<len;i++) if(data[i]=='\n') n++;
              if(len>0 && data[len-1]!='\n') n++;
              return n;
           }
        //将日志消息写入到指定文件
           void log(const char *data,size_t len)
           {
              if(_failed.load(std::memory_order_relaxed))
              {
                  _dropped_count.fetch_add(countRecords(data,len),std::memory_order_relaxed);
                  _dropped_bytes.fetch_add(len,std::memory_order_relaxed);
                  return;
              }
              errno=0;
              _ofs.write(data,len);
              if(!_ofs.good())
              {
                  markFailed("write failed");
                  _dropped_count.fetch_add(countRecords(data,len),std::memory_order_relaxed);
                  _dropped_bytes.fetch_add(len,std::memory_order_relaxed);
              }
           }
        //运行期统计与状态查询（可被其它线程调用）
           size_t droppedCount() const { return _dropped_count.load(std::memory_order_relaxed); }
           size_t droppedBytes() const { return _dropped_bytes.load(std::memory_order_relaxed); }
           bool failed() const { return _failed.load(std::memory_order_relaxed); }
           std::string lastError() const
           {
              std::lock_guard<std::mutex> lock(_err_mutex);
              return _last_error;
           }
        private:
           void markFailed(const char *what)
           {
              bool expected=false;
              if(!_failed.compare_exchange_strong(expected,true)) return;//只记录首次失败
              int err=errno;
              std::ostringstream oss;
              oss<<what<<" path="<<_pathname<<" errno="<<err<<" ("<<strerror(err?err:EIO)<<")";
              std::lock_guard<std::mutex> lock(_err_mutex);
              _last_error=oss.str();
           }
        private:
           std::string _pathname;  //文件名称
           std::ofstream _ofs;  //文件输出流对象
           std::atomic<bool> _failed;  //是否已失效
           std::atomic<size_t> _dropped_count;  //丢弃记录数（按 '\n' 计数）
           std::atomic<size_t> _dropped_bytes;  //丢弃字节
           mutable std::mutex _err_mutex;  //保护 _last_error
           std::string _last_error;  //首次失败原因
    };
    //落地方向：滚动文件（以大小进行滚动）
    class RollBySizeSink:public LogSink
    {
        public:
           //构造时传入文件名，并打开文件，将操作句柄管理起来
           RollBySizeSink(const std::string &basename,size_t max_size):
           _basename(basename),_max_fsize(max_size),_cur_fsize(0),_name_count(0)
           {
            std::string pathname=createNewFile();
            //1.创建文件所在的目录
              util::File::createDirectory(util::File::path(pathname)); 
            //2。创建并打开日志文件
            _ofs.open(pathname,std::ios::binary|std::ios::app);
            assert(_ofs.is_open());
           }
           //将日志消息写入到指定文件-写入前判断文件大小，超过了最大大小就要切换文件
             void log(const char *data,size_t len)
             {
               if(_cur_fsize>=_max_fsize)
               {
                std::string pathname=createNewFile();
                _ofs.close();//关闭原来已经打开的文件
                _ofs.open(pathname,std::ios::binary|std::ios::app);
                assert(_ofs.is_open());
                _cur_fsize=0;
               }
               _ofs.write(data,len);
               assert(_ofs.good());
               _cur_fsize+=len;
             }
        private:
      //进行大小判断，超过指定大小则创建新文件
           std::string createNewFile()
           {
            //获取系统时间，以时间来构造文件名
            time_t t=util::Date::getTime();
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
            filename<<"-";
            filename<<_name_count++;
            filename<<".log";
            return filename.str();
           }
        private:
        //通过基础文件名+扩展文件名（以时间生成）组成一个实际的当前输出文件名
           size_t _name_count;
           std::string _basename;
           std::ofstream _ofs;
           size_t _max_fsize;//记录最大大小，当前文件超过了这个大小就要进行切换文件
           size_t _cur_fsize;//记录当前文件已经写入的数据大小
    };
    
    class SinkFactory
    {
          public:
            template<typename SinkType,typename...Args>
            static LogSink::ptr create(Args &&...args)
            {
               return std::make_shared<SinkType>(std::forward<Args>(args)...);
            }
    };
}
#endif