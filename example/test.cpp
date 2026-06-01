#include"../logs/ppalog.h"
#include<unistd.h>



    void test_log(const std::string &name)
    {
        INFO("%s","测试开始");
        ppalog::Logger::ptr logger=ppalog::LoggerManager::getInstance().getLogger(name);
        logger->debug("%s","测试日志");
        logger->info("%s","测试日志");
        logger->warn("%s","测试日志");
        logger->error("%s","测试日志");
        logger->fatal("%s","测试日志");
    //     size_t count=0;
    // while(count<500000)
    // {
    //     logger->fatal("测试日志-%d",count++);
    // }
        INFO("%s","测试结束");
        // DEBUG("%s","测试日志");
        // INFO("%s","测试日志");
        // WARN("%s","测试日志");
        // ERROR("%s","测试日志");
        // FATAL("%s","测试日志");
    //     size_t count=0;
    // while(count<500000)
    // {
    //     FATAL("测试日志-%d",count++);
    // }


     }

int main()
{
    // //读取文件数据，一点一点写入缓冲区，最终将缓冲区数据写入文件，判断生成的新文件和源文件是否一致
    // std::ifstream ifs("./logfile/test.log",std::ios::binary);
    // if(ifs.is_open()==false){std::cout<<"open failed\n";return -1;}
    // ifs.seekg(0,std::ios::end);//读写位置跳转的末尾
    // size_t fsize=ifs.tellg();//获取当前读写位置和起始位置的偏移量
    // ifs.seekg(0,std::ios::beg);
    // std::string body;
    // body.resize(fsize);
    // ifs.read(&body[0],fsize);
    // if(ifs.good()==false){std::cout<<"read error";return -1;}
    // ifs.close();
    // std::cout<<fsize<<std::endl;
    // ppalog::Buffer buffer;
    // for(int i=0;i<body.size();i++)
    // {
    //     buffer.push(&body[i],1);
    // }
    // std::cout<<buffer.readAbleSize()<<std::endl;

    // std::ofstream ofs("./logfile/tmp.log",std::ios::binary);
    // //ofs.write(buffer.begin(),buffer.readAbleSize());
    // size_t rsize=buffer.readAbleSize();
    // for(int i=0;i<rsize;i++)
    // {
    //     ofs.write(buffer.begin(),1);
    //     if(ofs.good()==false){std::cout<<"write error\n";return -1;}
    //     buffer.moveReader(1);
    // }
    // if(ofs.good()==false){std::cout<<"write error\n";return -1;}
    // ofs.close();





    // std::string logger_name="sync_logger";
    // ppalog::LogLevel::value limit=ppalog::LogLevel::value::WARN;
    // ppalog::Formatter::ptr fmt(new ppalog::Formatter("[%d{%H:%M:%S}][%c][%f:%l][%p]%T%m%n"));
    // ppalog::LogSink::ptr stdout_lsp=ppalog::SinkFactory::create<ppalog::StdoutSink>();
    // ppalog::LogSink::ptr file_lsp=ppalog::SinkFactory::create<ppalog::FileSink>("./logfile/test.log");
    // ppalog::LogSink::ptr roll_lsp=ppalog::SinkFactory::create<ppalog::RollBySizeSink>("./logfile/roll-",1024*1024);
    // std::vector<ppalog::LogSink::ptr> sinks={stdout_lsp,file_lsp,roll_lsp};
    // ppalog::Logger::ptr logger(new ppalog::SyncLogger(logger_name,limit,fmt,sinks));

    std::unique_ptr<ppalog::LoggerBuilder> builder(new ppalog::GlobalLoggerBuilder());
    builder->buildLoggerName("async_logger");
    builder->buildLoggerLevel(ppalog::LogLevel::value::DEBUG);
    builder->buildFormatter("[%c][%f:%l][%p]%m%n");
    builder->buildLoggerType(ppalog::LoggerType::LOGGER_SYNC); 
    // builder->buildEnableUnSafeAsync();
    builder->buildSink<ppalog::FileSink>("./logfile/sync.log");
    builder->buildSink<ppalog::StdoutSink>();
    builder->buildSink<ppalog::RollBySizeSink>("./logfile/roll-sync-by-size",1024*1024);
    builder->build(); 
    test_log("async_logger");




    

    // ppalog::LogMsg msg(ppalog::LogLevel::value::INFO,25,"main.c","root","格式化功能测试....");
    // ppalog::Formatter fmt;//"abc%%abc[%d{%H:%M:%S}]%m%n"
    // std::string str=fmt.format(msg);
    // // ppalog::LogSink::ptr stdout_lsp=ppalog::SinkFactory::create<ppalog::StdoutSink>();
    // // ppalog::LogSink::ptr file_lsp=ppalog::SinkFactory::create<ppalog::FileSink>("./logfile/test.log");
    // // ppalog::LogSink::ptr roll_lsp=ppalog::SinkFactory::create<ppalog::RollBySizeSink>("./logfile/roll-",1024*1024);
    // ppalog::LogSink::ptr time_lsp=ppalog::SinkFactory::create<RollByTimeSink>("./logfile/rolltime-",TimeGap::GAP_SECOND);
    // time_t old=ppalog::util::Date::getTime();
    // while(ppalog::util::Date::getTime()<old+5)
    // {
    //     time_lsp->log(str.c_str(),str.size());
    //     usleep(1000);
    // }


//    std::cout<<ppalog::LogLevel().toString(ppalog::LogLevel::value::DEBUG)<<std::endl;
//    std::cout<<ppalog::LogLevel().toString(ppalog::LogLevel::value::INFO)<<std::endl;
//    std::cout<<ppalog::LogLevel().toString(ppalog::LogLevel::value::WARN)<<std::endl;
//    std::cout<<ppalog::LogLevel().toString(ppalog::LogLevel::value::ERROR)<<std::endl;
//    std::cout<<ppalog::LogLevel().toString(ppalog::LogLevel::value::FATAL)<<std::endl;
//    std::cout<<ppalog::LogLevel().toString(ppalog::LogLevel::value::OFF)<<std::endl;

//    std::cout<<biglog::util::Date::getTime()<<std::endl;
//    std::string pathname="./abc/bcd/a.txt";
//    biglog::util::File::creatDirectory(biglog::util::File::path(pathname));

    return 0;
}
