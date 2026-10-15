// ppalog 性能压测工具（参数化）
//
// 用法: ./bench <mode> <sink> <threads> <msgs> <len>
//   mode : sync | async | unsafe | filtered
//          sync     同步日志器
//          async    异步日志器(ASYNC_SAFE, 缓冲区满则阻塞)
//          unsafe   异步日志器(ASYNC_UNSAFE, 无限扩容)
//          filtered 级别设为 OFF, 只测"级别过滤"这一层的纯调用开销
//   sink : file | null   (file=./logfile/bench.log, null=/dev/null)
//   threads : 生产者线程数
//   msgs    : 总日志条数(尽量能被 threads 整除)
//   len     : 单条日志字节数(含结尾换行)
//
// 说明:
//   使用 LocalLoggerBuilder(不注册到全局单例), 这样 logger 析构时机确定,
//   在进程内即可测出异步工作线程把残留数据"排空(drain)"的耗时, 无需改库代码。
//   指标:
//     producer : 生产者(调用线程)全部完成写入的墙钟时间
//     drain    : logger 析构 -> AsyncLooper::stop() -> join 排空缓冲的时间
//     total    : producer + drain (异步端到端真实耗时)

#include"../logs/ppalog.h"
#include<thread>
#include<vector>
#include<chrono>
#include<string>
#include<cstring>
#include<cstdio>
#include<cstdlib>

using Clock = std::chrono::steady_clock;

static double sec(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double>(b - a).count();
}

struct Cfg
{
    std::string mode;
    std::string sink;
    size_t threads;
    size_t msgs;
    size_t len;
};

static ppalog::Logger::ptr make_logger(const Cfg &c, const std::string &path)
{
    std::unique_ptr<ppalog::LoggerBuilder> b(new ppalog::LocalLoggerBuilder());
    b->buildLoggerName("bench_" + c.mode);
    b->buildFormatter("%m%n");                       // 最小格式化, 聚焦写入路径
    b->buildLoggerType(c.mode == "sync" || c.mode == "filtered"
                           ? ppalog::LoggerType::LOGGER_SYNC
                           : ppalog::LoggerType::LOGGER_ASYNC);
    if(c.mode == "unsafe") b->buildEnableUnSafeAsync();
    b->buildLoggerLevel(c.mode == "filtered"
                            ? ppalog::LogLevel::value::OFF
                            : ppalog::LogLevel::value::DEBUG);
    if(c.sink == "null") b->buildSink<ppalog::FileSink>("/dev/null");
    else                 b->buildSink<ppalog::FileSink>(path);
    return b->build();
}

static void run(const Cfg &c)
{
    const std::string path = "./logfile/bench.log";
    ppalog::Logger::ptr logger = make_logger(c, path);
    if(logger.get() == nullptr) { std::cout << "build logger failed\n"; return; }

    const std::string msg(c.len > 0 ? c.len - 1 : 0, 'A');   // 末尾换行由 %n 提供
    const size_t per = c.msgs / c.threads;
    std::vector<double> thr_time(c.threads, 0.0);
    std::vector<std::thread> threads;
    threads.reserve(c.threads);

    auto t0 = Clock::now();
    for(size_t i = 0; i < c.threads; i++)
    {
        threads.emplace_back([&, i]() {
            auto s = Clock::now();
            for(size_t j = 0; j < per; j++)
                logger->fatal("%s", msg.c_str());
            thr_time[i] = sec(s, Clock::now());
        });
    }
    for(auto &t : threads) t.join();
    auto t1 = Clock::now();

    logger.reset();            // 析构 -> 异步则触发 stop()/join 排空缓冲; 同步则无意义
    auto t2 = Clock::now();

    const double producer = sec(t0, t1);
    const double drain    = sec(t1, t2);
    const double total    = sec(t0, t2);
    const double bytes    = (double)c.msgs * (double)c.len;

    double lat_sum = 0.0;
    for(size_t i = 0; i < c.threads; i++) lat_sum += thr_time[i] / (double)per;
    const double avg_lat = lat_sum / (double)c.threads;      // 单线程视角单条平均延迟

    printf("RESULT mode=%-8s sink=%-4s thr=%zu msgs=%zu len=%zu | "
           "producer=%.4fs drain=%.4fs total=%.4fs | "
           "front=%.0f msg/s (%.1f MB/s) e2e=%.0f msg/s (%.1f MB/s) avg_lat=%.0f ns\n",
           c.mode.c_str(), c.sink.c_str(), c.threads, c.msgs, c.len,
           producer, drain, total,
           (double)c.msgs / producer, bytes / producer / 1048576.0,
           (double)c.msgs / total,    bytes / total    / 1048576.0,
           avg_lat * 1e9);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if(argc != 6)
    {
        std::cout << "usage: " << argv[0]
                  << " <sync|async|unsafe|filtered> <file|null> <threads> <msgs> <len>\n";
        return 1;
    }
    Cfg c;
    c.mode    = argv[1];
    c.sink    = argv[2];
    c.threads = (size_t)strtoul(argv[3], nullptr, 10);
    c.msgs    = (size_t)strtoul(argv[4], nullptr, 10);
    c.len     = (size_t)strtoul(argv[5], nullptr, 10);
    if(c.threads == 0) c.threads = 1;
    if(c.msgs < c.threads) c.msgs = c.threads;
    run(c);
    return 0;
}
