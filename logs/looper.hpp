// 实现异步工作器
#ifndef __M_LOOPER_HPP_
#define __M_LOOPER_HPP_
#include"buffer.hpp"
#include<mutex>
#include<condition_variable>
#include<thread>
#include<functional>
#include<memory>
#include<atomic>
#include<chrono>
#include<cstdlib>

namespace ppalog
{
    using Functor=std::function<void(Buffer &)>;
    enum class AsyncType
    {
        ASYNC_SAFE,//安全状态，表示缓冲区满了则阻塞，避免资源耗尽的风险
        ASYNC_UNSAFE//不考虑资源耗尽的问题，无限扩容。常用于测试
    };
    // 从环境变量取值，未设置时用默认值（便于做阈值/超时参数扫描而不必重编）
    inline size_t envNotifyBytes(size_t def)
    {
        const char *e=std::getenv("PPALOG_NOTIFY_BYTES");
        return e?(size_t)strtoull(e,nullptr,10):def;
    }
    inline int envNotifyMs(int def)
    {
        const char *e=std::getenv("PPALOG_NOTIFY_MS");
        return e?atoi(e):def;
    }
    class AsyncLooper
    {
        public:
            using ptr=std::shared_ptr<AsyncLooper>;
            // notify_bytes: 累计写入达到该字节数才唤醒消费者一次（0 = 每条都唤醒，等同原行为）
            // notify_timeout_ms: 消费者最长阻塞时间，超时也醒来排空，避免低负载下日志长时间滞留
            AsyncLooper(const Functor &cb,AsyncType loop_type=AsyncType::ASYNC_SAFE,
                        size_t notify_bytes=0,int notify_timeout_ms=0):_stop(false),
             _looper_type(loop_type),
            _notify_bytes(envNotifyBytes(notify_bytes)),
            _notify_timeout_ms(envNotifyMs(notify_timeout_ms)),
            _pending_bytes(0),
            _thread(std::thread(&AsyncLooper::threadEntry,this)),
            _callBack(cb) {}
            ~AsyncLooper(){stop();}
            void stop()
            {
                _stop=true;//将退出标志设置为true
                _cond_con.notify_all();//唤醒所有工作线程
                _thread.join();//等待工作线程的退出
            }
            void push(const char*data,size_t len)
            {
                //1.无限扩容-非安全
                //2.固定大小-安全 生产缓冲区中数据满了就阻塞
                std::unique_lock<std::mutex> lock(_mutex);
                //条件变量空值，若缓冲区剩余空间大小大于数据长度，则可以添加数据
                if(_looper_type==AsyncType::ASYNC_SAFE)
                   _cond_pro.wait(lock,[&](){return _pro_buf.writeAbleSize()>=len;});
                //能够走下来代表满足了条件，可以向缓冲区添加数据
                _pro_buf.push(data,len);
                //批量唤醒：累计数据量达到阈值才通知一次，避免逐条 notify_one 的开销
                _pending_bytes+=len;
                if(_notify_bytes==0||_pending_bytes>=_notify_bytes)
                {
                    _pending_bytes=0;
                    _cond_con.notify_one();//唤醒消费者线程，对缓冲区的数据进行处理
                }
                //未达阈值且生产突然停止时，由消费者侧 wait_for 的超时兜底唤醒
            }
        private:
        //线程入口函数--对消费缓冲区中的数据进行处理，处理完毕后，初始化缓冲区，交换缓冲区
            void threadEntry()
            {
                auto pred=[&](){return _stop||!_pro_buf.empty();};
                while(1)
                {
                    //为互斥锁设置一个生命周期，当缓冲区交换完毕后就解锁（并不对数据的处理过程加锁保护）
                    {
                    //1.判断生产缓冲区有没有数据，有则交换，无则阻塞
                    std::unique_lock<std::mutex> lock(_mutex);
                    //退出标志被设置且缓冲区已无数据，再退出，否则可能会造成生产缓冲区中有数据但没有被完全处理
                    if(_stop&&_pro_buf.empty())break;
                    //若当前是退出前被唤醒，或者有数据被唤醒，则返回真，继续向下运行，否则重新陷入休眠
                    //有超时设置时用 wait_for 兜底：即使生产者没有攒够阈值，也最多等 N 毫秒就排空
                    if(_notify_timeout_ms>0)
                        _cond_con.wait_for(lock,std::chrono::milliseconds(_notify_timeout_ms),pred);
                    else
                        _cond_con.wait(lock,pred);
                    _con_buf.swap(_pro_buf);
                    _pending_bytes=0;//交换后累计计数清零，重新开始攒批
                    //2.唤醒生产者线程
                    if(_looper_type==AsyncType::ASYNC_SAFE)
                       _cond_pro.notify_all();
                    }
                    //3.被唤醒后，对消费缓冲区进行数据处理（超时醒来但无数据的空批直接跳过）
                    if(!_con_buf.empty())
                        _callBack(_con_buf);
                    //4.初始化消费缓冲区
                    _con_buf.reset();
                }
            }
        private:
            Functor _callBack;//具体对缓冲区数据进行处理的回调函数，由使用者传入
        private:
            AsyncType _looper_type;
            size_t _notify_bytes;//批量唤醒阈值（字节）
            int _notify_timeout_ms;//消费者阻塞超时（毫秒）
            size_t _pending_bytes;//自上次通知以来累计写入的字节数（受 _mutex 保护）
            std::atomic<bool> _stop;//停止标志
            Buffer _pro_buf;//生产缓冲区
            Buffer _con_buf;//消费缓冲区
            std::mutex _mutex;
            std::condition_variable _cond_pro;
            std::condition_variable _cond_con;
            std::thread _thread;//异步工作器对应的工作线程
    };
}

#endif
