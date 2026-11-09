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
#include<cstdint>
#include<chrono>
#include<fcntl.h>
#include<unistd.h>


namespace ppalog
{
    //落地方向的刷盘策略（默认 OFF，行为与此前完全一致）
    //  OFF        只依赖 ofstream 的流缓冲，不 flush、不 fsync
    //  EVERY_CALL 每次 log() 调用后 flush + fsync
    //  BY_BYTES   每累计 N 字节 flush + fsync；切分点对齐到 '\n' 记录边界，
    //             单条记录跨过阈值时最多超出“一条记录”的长度
    //  BY_MS      距上次刷盘超过 T 毫秒时 flush + fsync；在写入路径上判断，不另起线程
    //刷盘失败与写入失败语义一致：fail-stop、记录 failed()/lastError()（含 errno），
    //未落盘的数据计入 droppedCount()/droppedBytes()，不抛异常、不 abort。
    struct FlushPolicy
    {
        enum Mode { OFF = 0, EVERY_CALL = 1, BY_BYTES = 2, BY_MS = 3 };
        Mode mode;
        size_t bytes;   // BY_BYTES 的字节阈值
        uint64_t ms;    // BY_MS 的毫秒阈值
        FlushPolicy() : mode(OFF), bytes(0), ms(0) {}
        static FlushPolicy off() { return FlushPolicy(); }
        static FlushPolicy everyCall()
        {
            FlushPolicy p; p.mode = EVERY_CALL; return p;
        }
        static FlushPolicy byBytes(size_t n)
        {
            FlushPolicy p; p.mode = BY_BYTES; p.bytes = n ? n : 1; return p;
        }
        static FlushPolicy byMs(uint64_t t)
        {
            FlushPolicy p; p.mode = BY_MS; p.ms = t ? t : 1; return p;
        }
    };
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
    //  1) 构造时打开失败、或运行期写入/刷盘失败，立即把该 sink 标记为 failed()，并记录首次失败原因（含 errno）；
    //  2) 一旦 failed()，后续写入全部丢弃并累加 droppedCount()/droppedBytes()，不再做无效 IO、不再重复报错；
    //  3) 不抛出异常，日志调用路径保持无异常语义；
    //  4) fail-stop 以 sink 为单位：同一 logger 的其它 sink 不受影响。
    //注意：ofstream 写入非原子，失败瞬间最后一条记录可能被截断，需配合记录级长度/校验才能识别。
    class FileSink:public LogSink
    {
        public:
           FileSink(const std::string &pathname):FileSink(pathname,FlushPolicy::off()) {}
           FileSink(const std::string &pathname,const FlushPolicy &policy):_pathname(pathname),
            _flush(policy),_sync_fd(-1),_since(0),_pending_records(0),_pending_bytes(0),
            _failed(false),_dropped_count(0),_dropped_bytes(0),_synced_records(0),
            _synced_bytes(0),_flush_count(0)
           {
              //1.创建文件所在的目录
              util::File::createDirectory(util::File::path(pathname));
              //2.创建并打开日志文件
              _ofs.open(_pathname,std::ios::binary|std::ios::app);
              if(!_ofs.is_open())
                  markFailed("open failed");
              else if(_flush.mode!=FlushPolicy::OFF)
              {
                  //fsync 走独立的 fd，ofstream 的写入路径保持不变
                  _sync_fd=::open(_pathname.c_str(),O_WRONLY|O_APPEND);
                  if(_sync_fd<0) markFailed("sync fd open failed");
              }
              _last_flush=std::chrono::steady_clock::now();
           }
           ~FileSink()
           {
              //析构时把尚未刷盘的数据尽力落盘（仅在启用策略且未失效时有意义）
              if(_flush.mode!=FlushPolicy::OFF && !_failed.load(std::memory_order_relaxed)
                 && _pending_bytes>0)
                  flushAndSync();
              if(_sync_fd>=0) ::close(_sync_fd);
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
                  countDrop(data,len);
                  return;
              }
              if(_flush.mode!=FlushPolicy::OFF)
              {
                  logWithFlush(data,len);
                  return;
              }
              //默认路径：与引入刷盘策略之前逐字一致，只多一次可预测的分支
              errno=0;
              _ofs.write(data,len);
              if(!_ofs.good())
              {
                  markFailed("write failed");
                  countDrop(data,len);
              }
           }
        //运行期统计与状态查询（可被其它线程调用）
           size_t droppedCount() const { return _dropped_count.load(std::memory_order_relaxed); }
           size_t droppedBytes() const { return _dropped_bytes.load(std::memory_order_relaxed); }
           bool failed() const { return _failed.load(std::memory_order_relaxed); }
        //已随成功的 fsync 落盘的数据量（仅在启用刷盘策略时增长）
           size_t syncedRecords() const { return _synced_records.load(std::memory_order_relaxed); }
           size_t syncedBytes() const { return _synced_bytes.load(std::memory_order_relaxed); }
           size_t flushCount() const { return _flush_count.load(std::memory_order_relaxed); }
           std::string lastError() const
           {
              std::lock_guard<std::mutex> lock(_err_mutex);
              return _last_error;
           }
        protected:
           void countDrop(const char *data,size_t len)
           {
              _dropped_count.fetch_add(countRecords(data,len),std::memory_order_relaxed);
              _dropped_bytes.fetch_add(len,std::memory_order_relaxed);
           }
           void markFailed(const char *what)
           {
              bool expected=false;
              if(!_failed.compare_exchange_strong(expected,true)) return;//只记录首次失败
              int err=errno;
              if(err==0) err=EIO;
              std::ostringstream oss;
              oss<<what<<" path="<<_pathname<<" errno="<<err<<" ("<<strerror(err)<<")";
              std::lock_guard<std::mutex> lock(_err_mutex);
              _last_error=oss.str();
           }
           //写一段数据并累计“已写未刷盘”的计数
           bool writeChunk(const char *data,size_t len)
           {
              if(len==0) return true;
              errno=0;
              _ofs.write(data,len);
              if(!_ofs.good())
              {
                  markFailed("write failed");
                  return false;
              }
              _pending_records+=countRecords(data,len);
              _pending_bytes+=len;
              _since+=len;
              return true;
           }
           //flush 流缓冲 + fsync；成功后把 pending 计入已落盘统计
           bool flushAndSync()
           {
              if(_failed.load(std::memory_order_relaxed)) return false;
              errno=0;
              _ofs.flush();
              if(!_ofs.good())
              {
                  markFailed("flush failed");
                  return false;
              }
              if(_sync_fd<0)
              {
                  errno=EBADF;
                  markFailed("sync fd unavailable");
                  return false;
              }
              errno=0;
              if(::fsync(_sync_fd)!=0)
              {
                  markFailed("fsync failed");
                  return false;
              }
              _synced_records.fetch_add(_pending_records,std::memory_order_relaxed);
              _synced_bytes.fetch_add(_pending_bytes,std::memory_order_relaxed);
              _flush_count.fetch_add(1,std::memory_order_relaxed);
              _pending_records=0;
              _pending_bytes=0;
              _since=0;
              _last_flush=std::chrono::steady_clock::now();
              return true;
           }
           //失败时：把“已写未刷盘”的和 data[off,len) 未写的一并计入丢弃
           void dropPendingPlusRemainder(const char *data,size_t len,size_t off)
           {
              _dropped_count.fetch_add(_pending_records+countRecords(data+off,len-off),
                                       std::memory_order_relaxed);
              _dropped_bytes.fetch_add(_pending_bytes+(len-off),std::memory_order_relaxed);
              _pending_records=0;
              _pending_bytes=0;
           }
           void logWithFlush(const char *data,size_t len)
           {
              if(_flush.mode==FlushPolicy::BY_BYTES)
              {
                  size_t off=0;
                  while(off<len)
                  {
                     size_t room=_flush.bytes>_since?_flush.bytes-_since:0;
                     if(room==0)
                     {
                        if(!flushAndSync()) { dropPendingPlusRemainder(data,len,off); return; }
                        continue;
                     }
                     size_t take=room<len-off?room:len-off;
                     //切分点对齐到记录边界：宁可多带完当前这一条，也不把 fsync 落在半条记录中间
                     if(off+take<len)
                     {
                        size_t k=take;
                        while(off+k<len && data[off+k]!='\n') k++;
                        if(off+k<len) k++;
                        take=k;
                     }
                     if(!writeChunk(data+off,take)) { dropPendingPlusRemainder(data,len,off); return; }
                     off+=take;
                     if(_since>=_flush.bytes)
                     {
                        if(!flushAndSync()) { dropPendingPlusRemainder(data,len,off); return; }
                     }
                  }
                  return;
              }
              if(!writeChunk(data,len)) { dropPendingPlusRemainder(data,len,0); return; }
              bool sync=false;
              if(_flush.mode==FlushPolicy::EVERY_CALL) sync=true;
              else
              {
                  auto now=std::chrono::steady_clock::now();
                  uint64_t elapsed=(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>
                      (now-_last_flush).count();
                  if(elapsed>=_flush.ms) sync=true;
              }
              if(sync && !flushAndSync()) dropPendingPlusRemainder(data,len,len);
           }
        private:
           std::string _pathname;  //文件名称
           std::ofstream _ofs;  //文件输出流对象
           FlushPolicy _flush;  //刷盘策略
           int _sync_fd;  //专用于 fsync 的 fd（OFF 时为 -1）
           size_t _since;  //距上次刷盘的字节数
           size_t _pending_records;  //已写未刷盘的记录数
           size_t _pending_bytes;  //已写未刷盘的字节数
           std::chrono::steady_clock::time_point _last_flush;
           std::atomic<bool> _failed;  //是否已失效
           std::atomic<size_t> _dropped_count;  //丢弃记录数（按 '\n' 计数）
           std::atomic<size_t> _dropped_bytes;  //丢弃字节
           std::atomic<size_t> _synced_records;  //已落盘记录数（累计）
           std::atomic<size_t> _synced_bytes;  //已落盘字节数（累计）
           std::atomic<size_t> _flush_count;  //成功的 flush+fsync 次数
           mutable std::mutex _err_mutex;  //保护 _last_error
           std::string _last_error;  //首次失败原因
    };
    //落地方向：滚动文件（以大小进行滚动）
    //失败语义与 FileSink 一致（Debug / Release 相同，不依赖 assert）：
    //  1) 构造时打开失败、滚动时新文件打开失败、或运行期写入/刷盘失败，立即 failed() 并记录首次失败原因（含 errno）；
    //  2) 一旦 failed()，后续写入全部丢弃并累加 droppedCount()/droppedBytes()，不再做无效 IO；
    //  3) 不抛出异常；
    //  4) 滚动失败后不再尝试创建新文件，避免为每条日志反复失败与反复建文件。
    //启用刷盘策略时，滚动前先把待刷数据 flush + fsync 到旧文件再关闭，避免切换文件丢数据。
    class RollBySizeSink:public LogSink
    {
        public:
           //构造时传入文件名，并打开文件，将操作句柄管理起来
           RollBySizeSink(const std::string &basename,size_t max_size):
           RollBySizeSink(basename,max_size,FlushPolicy::off()) {}
           RollBySizeSink(const std::string &basename,size_t max_size,const FlushPolicy &policy):
           _name_count(0),_basename(basename),_flush(policy),_sync_fd(-1),
           _max_fsize(max_size),_cur_fsize(0),_since(0),_pending_records(0),_pending_bytes(0),
           _failed(false),_dropped_count(0),_dropped_bytes(0),_synced_records(0),
           _synced_bytes(0),_flush_count(0)
           {
            _pathname=createNewFile();
            //1.创建文件所在的目录
              util::File::createDirectory(util::File::path(_pathname)); 
            //2。创建并打开日志文件
            _ofs.open(_pathname,std::ios::binary|std::ios::app);
            if(!_ofs.is_open())
               markFailed("open failed");
            else if(_flush.mode!=FlushPolicy::OFF)
            {
               _sync_fd=::open(_pathname.c_str(),O_WRONLY|O_APPEND);
               if(_sync_fd<0) markFailed("sync fd open failed");
            }
            _last_flush=std::chrono::steady_clock::now();
           }
           ~RollBySizeSink()
           {
              if(_flush.mode!=FlushPolicy::OFF && !_failed.load(std::memory_order_relaxed)
                 && _pending_bytes>0)
                  flushAndSync();
              if(_sync_fd>=0) ::close(_sync_fd);
           }
           //将日志消息写入到指定文件-写入前判断文件大小，超过了最大大小就要切换文件
             void log(const char *data,size_t len)
             {
               if(_failed.load(std::memory_order_relaxed))
               {
                  countDrop(data,len);
                  return;
               }
               if(_flush.mode!=FlushPolicy::OFF)
               {
                  logWithFlush(data,len);
                  return;
               }
               //默认路径：与引入刷盘策略之前逐字一致
               if(_cur_fsize>=_max_fsize)
               {
                if(!roll()) { countDrop(data,len); return; }
               }
               errno=0;
               _ofs.write(data,len);
               if(!_ofs.good())
               {
                  markFailed("write failed");
                  countDrop(data,len);
                  return;
               }
               _cur_fsize+=len;
             }
        //运行期统计与状态查询（可被其它线程调用）
           size_t droppedCount() const { return _dropped_count.load(std::memory_order_relaxed); }
           size_t droppedBytes() const { return _dropped_bytes.load(std::memory_order_relaxed); }
           bool failed() const { return _failed.load(std::memory_order_relaxed); }
           size_t syncedRecords() const { return _synced_records.load(std::memory_order_relaxed); }
           size_t syncedBytes() const { return _synced_bytes.load(std::memory_order_relaxed); }
           size_t flushCount() const { return _flush_count.load(std::memory_order_relaxed); }
           std::string currentFile() const
           {
              std::lock_guard<std::mutex> lock(_err_mutex);
              return _pathname;
           }
           std::string lastError() const
           {
              std::lock_guard<std::mutex> lock(_err_mutex);
              return _last_error;
           }
        protected:
           void countDrop(const char *data,size_t len)
           {
              _dropped_count.fetch_add(FileSink::countRecords(data,len),std::memory_order_relaxed);
              _dropped_bytes.fetch_add(len,std::memory_order_relaxed);
           }
           void markFailed(const char *what)
           {
              bool expected=false;
              if(!_failed.compare_exchange_strong(expected,true)) return;//只记录首次失败
              int err=errno;
              if(err==0) err=EIO;
              std::lock_guard<std::mutex> lock(_err_mutex);
              std::ostringstream oss;
              oss<<what<<" path="<<_pathname<<" errno="<<err<<" ("<<strerror(err)<<")";
              _last_error=oss.str();
           }
           //滚动：先把待刷数据落盘到旧文件，再切换；任一步失败即 fail-stop
           bool roll()
           {
              if(_flush.mode!=FlushPolicy::OFF && _pending_bytes>0)
              {
                 if(!flushAndSync()) return false;
              }
              _ofs.close();//关闭原来已经打开的文件
              std::string pathname=createNewFile();
              {
                 std::lock_guard<std::mutex> lock(_err_mutex);
                 _pathname=pathname;//currentFile() 与外层诊断读取该值
              }
              util::File::createDirectory(util::File::path(pathname));
              _ofs.open(pathname,std::ios::binary|std::ios::app);
              if(!_ofs.is_open())
              {
                 markFailed("roll open failed");
                 return false;
              }
              if(_flush.mode!=FlushPolicy::OFF)
              {
                 if(_sync_fd>=0) ::close(_sync_fd);
                 _sync_fd=::open(pathname.c_str(),O_WRONLY|O_APPEND);
                 if(_sync_fd<0)
                 {
                    markFailed("sync fd open failed");
                    return false;
                 }
              }
              _cur_fsize=0;
              _since=0;
              _last_flush=std::chrono::steady_clock::now();
              return true;
           }
           bool writeChunk(const char *data,size_t len)
           {
              if(len==0) return true;
              errno=0;
              _ofs.write(data,len);
              if(!_ofs.good())
              {
                  markFailed("write failed");
                  return false;
              }
              _pending_records+=FileSink::countRecords(data,len);
              _pending_bytes+=len;
              _since+=len;
              _cur_fsize+=len;
              return true;
           }
           bool flushAndSync()
           {
              if(_failed.load(std::memory_order_relaxed)) return false;
              errno=0;
              _ofs.flush();
              if(!_ofs.good())
              {
                  markFailed("flush failed");
                  return false;
              }
              if(_sync_fd<0)
              {
                  errno=EBADF;
                  markFailed("sync fd unavailable");
                  return false;
              }
              errno=0;
              if(::fsync(_sync_fd)!=0)
              {
                  markFailed("fsync failed");
                  return false;
              }
              _synced_records.fetch_add(_pending_records,std::memory_order_relaxed);
              _synced_bytes.fetch_add(_pending_bytes,std::memory_order_relaxed);
              _flush_count.fetch_add(1,std::memory_order_relaxed);
              _pending_records=0;
              _pending_bytes=0;
              _since=0;
              _last_flush=std::chrono::steady_clock::now();
              return true;
           }
           void dropPendingPlusRemainder(const char *data,size_t len,size_t off)
           {
              _dropped_count.fetch_add(_pending_records+FileSink::countRecords(data+off,len-off),
                                       std::memory_order_relaxed);
              _dropped_bytes.fetch_add(_pending_bytes+(len-off),std::memory_order_relaxed);
              _pending_records=0;
              _pending_bytes=0;
           }
           void logWithFlush(const char *data,size_t len)
           {
              if(_flush.mode==FlushPolicy::BY_BYTES)
              {
                  size_t off=0;
                  while(off<len)
                  {
                     if(_cur_fsize>=_max_fsize)
                     {
                        if(!roll()) { dropPendingPlusRemainder(data,len,off); return; }
                     }
                     size_t room=_flush.bytes>_since?_flush.bytes-_since:0;
                     if(room==0)
                     {
                        if(!flushAndSync()) { dropPendingPlusRemainder(data,len,off); return; }
                        continue;
                     }
                     size_t take=room<len-off?room:len-off;
                     if(off+take<len)
                     {
                        size_t k=take;
                        while(off+k<len && data[off+k]!='\n') k++;
                        if(off+k<len) k++;
                        take=k;
                     }
                     if(!writeChunk(data+off,take)) { dropPendingPlusRemainder(data,len,off); return; }
                     off+=take;
                     if(_since>=_flush.bytes)
                     {
                        if(!flushAndSync()) { dropPendingPlusRemainder(data,len,off); return; }
                     }
                  }
                  return;
              }
              if(_cur_fsize>=_max_fsize)
              {
                 if(!roll()) { dropPendingPlusRemainder(data,len,0); return; }
              }
              if(!writeChunk(data,len)) { dropPendingPlusRemainder(data,len,0); return; }
              bool sync=false;
              if(_flush.mode==FlushPolicy::EVERY_CALL) sync=true;
              else
              {
                  auto now=std::chrono::steady_clock::now();
                  uint64_t elapsed=(uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>
                      (now-_last_flush).count();
                  if(elapsed>=_flush.ms) sync=true;
              }
              if(sync && !flushAndSync()) dropPendingPlusRemainder(data,len,len);
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
           FlushPolicy _flush;
           int _sync_fd;
           size_t _max_fsize;//记录最大大小，当前文件超过了这个大小就要进行切换文件
           size_t _cur_fsize;//记录当前文件已经写入的数据大小
           size_t _since;//距上次刷盘的字节数
           size_t _pending_records;//已写未刷盘的记录数
           size_t _pending_bytes;//已写未刷盘的字节数
           std::chrono::steady_clock::time_point _last_flush;
           std::string _pathname;//当前实际输出文件名（滚动时会变化）
           std::atomic<bool> _failed;  //是否已失效
           std::atomic<size_t> _dropped_count;  //丢弃记录数（按 '\n' 计数）
           std::atomic<size_t> _dropped_bytes;  //丢弃字节
           std::atomic<size_t> _synced_records;  //已落盘记录数（累计）
           std::atomic<size_t> _synced_bytes;  //已落盘字节数（累计）
           std::atomic<size_t> _flush_count;  //成功的 flush+fsync 次数
           mutable std::mutex _err_mutex;  //保护 _pathname 与 _last_error
           std::string _last_error;  //首次失败原因
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
