#include"../logs/ppalog.h"
#include<thread>
#include<vector>
#include<chrono>
//功能测试


void bench(const std::string &logger_name,size_t thr_count,size_t msg_count,size_t msg_len)
{
    //获取日志器
    ppalog::Logger::ptr logger=ppalog::getLogger(logger_name);
    if(logger.get()==nullptr){return;}
    std::cout<<"测试日志"<<msg_count<<"条， 总大小"<<(msg_count*msg_len)/1024<<"KB\n";
    //组织指定长度的日志消息
    std::string msg(msg_len - 1,'A');//少一个字节，思是为了给末尾添加换行
    //创建指定数量的线程
    std::vector<std::thread> _threads;
    std::vector<double>cost_arry(thr_count);
    size_t msg_ptr_thr=msg_count/thr_count;//总日志数量/线程数量就是每个线程要输出的日志数量
    for(int i=0;i<thr_count;i++)
    {
        _threads.emplace_back([&,i](){
    //线程函数内部开始计时 
    auto start=std::chrono::high_resolution_clock::now();
    //开是循环写日志
    for(int j=0;j<msg_ptr_thr;j++)
    {
        logger->fatal("%s",msg.c_str());
    }
    //线程函数内部结束计时
    auto end=std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> cost=end-start;
    cost_arry[i]=cost.count();
    std::cout<<"线程"<<i<<": "<<"\t输出日志数量："<<msg_ptr_thr<<", 耗时"<<cost.count()<<"s"<<std::endl;

    });
    }
    for(int i=0;i<thr_count;i++)
    {
        _threads[i].join();
    }
    //计算总耗时:在多线程中，每个线程都会耗费时间，但是线程是并发处理的，因此耗时最高的那个就是总时间
    double max_cost=cost_arry[0];
    for(int i=0;i<thr_count;i++)max_cost=max_cost<cost_arry[i]?cost_arry[i]:max_cost;
    size_t msg_per_sec=msg_count/max_cost;
    size_t size_per_sec=(msg_count*msg_len)/(max_cost*1024);
    //进行输出打印
    std::cout<<"\t总耗时: "<<max_cost<<"s\n";
    std::cout<<"\t每秒输出日志数量: "<<msg_per_sec<<"条\n";
    std::cout<<"\t每秒输出日志大小: "<<size_per_sec<<"KB\n";
}

void sync_bench()
{
    
    std::unique_ptr<ppalog::LoggerBuilder> builder(new ppalog::GlobalLoggerBuilder());
    builder->buildLoggerName("sync_logger");
    //builder->buildLoggerLevel(ppalog::LogLevel::value::DEBUG);
    builder->buildFormatter("%m%n");
    builder->buildLoggerType(ppalog::LoggerType::LOGGER_SYNC); 
    // builder->buildEnableUnSafeAsync();
    builder->buildSink<ppalog::FileSink>("./logfile/sync.log");
    builder->build(); 
    bench("sync_logger",3,1000000,100);
}
void async_bench()
{
    
    std::unique_ptr<ppalog::LoggerBuilder> builder(new ppalog::GlobalLoggerBuilder());
    builder->buildLoggerName("async_logger");
    //builder->buildLoggerLevel(ppalog::LogLevel::value::DEBUG);
    builder->buildFormatter("%m%n");
    builder->buildLoggerType(ppalog::LoggerType::LOGGER_ASYNC); 
    builder->buildEnableUnSafeAsync();//开启非安全模式-为了将实际落地时间排除在外
    builder->buildSink<ppalog::FileSink>("./logfile/sync.log");
    builder->build(); 
    bench("async_logger",3,1000000,100);
}

int main()
{
    async_bench();
    return 0;
}
