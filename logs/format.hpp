#ifndef __M_FORMAT_HPP_
#define __M_FORMAT_HPP_
#include"level.hpp"
#include"message.hpp"
#include"util.hpp"
#include<ctime>
#include<vector>
#include<assert.h>
#include<sstream>
#include<memory>

namespace ppalog
{                                  
    //抽象格式化子类基类
    class FormatItem
    {
       public:
         using ptr=std::shared_ptr<FormatItem>;
         virtual ~FormatItem() {}
         virtual void format(std::ostream &os, const LogMsg &msg) = 0;  //纯虚函数，子类必须实现该函数
    };
    class MsgFormatItem:public FormatItem//日志主体消息格式化子类
    {
       public:
         void format(std::ostream &out,const LogMsg &msg) override
         {
             out<<msg._payload;
         }
    };
    class LevelFormatItem:public FormatItem//日志级别格式化子类
    {
       public:
         void format(std::ostream &out,const LogMsg &msg) override
         {
             out<<LogLevel::toString(msg._level);
         }
    };
    class TimeFormatItem:public FormatItem//日志时间格式化子类
    {
       public:
         TimeFormatItem(const std::string &fmt="%H:%M:%S"): _time_fmt(fmt) {}  //构造函数，默认时间格式为"%H:%M:%S"
         void format(std::ostream &out,const LogMsg &msg) override
         {
             struct tm t;
             localtime_r(&msg._ctime, &t);
             char tmp[32]={0};
             strftime(tmp, 31, _time_fmt.c_str(), &t);
             out<<tmp;
         }
         private:
             std::string _time_fmt;  //时间格式
    };
    class FileFormatItem:public FormatItem//日志文件名称格式化子类
    {
       public:
         void format(std::ostream &out,const LogMsg &msg) override
         {
             out<<msg._file;
         }
    };
    class LineFormatItem:public FormatItem//日志行号格式化子类
    {
       public:
         void format(std::ostream &out,const LogMsg &msg) override
         {
             out<<msg._line;
         }
    };
    class ThreadFormatItem:public FormatItem//日志线程id格式化子类
    {
       public:
        ThreadFormatItem(const std::string &str = ""){}
        virtual void format(std::ostream &os, const LogMsg &msg) 
        {
            os << msg._tid;
        }
    };
    class LoggerFormatItem:public FormatItem//日志器名称格式化子类
    {
       public:
         void format(std::ostream &out,const LogMsg &msg) override
         {
             out<<msg._logger;
         }
    };
    class TabFormatItem:public FormatItem//制表符格式化子类
    {
       public:
         void format(std::ostream &out,const LogMsg &msg) override
         {
             out<<"\t";
         }
    };
    class NLineFormatItem:public FormatItem//换行符格式化子类
    {
       public:
         void format(std::ostream &out,const LogMsg &msg) override
         {
             out<<"\n";
         }
    };
    class OtherFormatItem:public FormatItem//其他格式化子类
    {
       public:
         OtherFormatItem(const std::string &str): _str(str){} 
         void format(std::ostream &out,const LogMsg &msg) override
         {
             out<<_str;
         }
         private:
             std::string _str;  
    };



    class Formatter
    {
        public:
            using ptr=std::shared_ptr<Formatter>;
            Formatter(const std::string &pattern="[%d{%H:%M:%S}][%t][%p][%c][%f:%l] %m%n"):
             _pattern(pattern)  
             {
               assert(parsePattern()==true);
             }
             //格式化日志消息
             void format(std::ostream &out,const LogMsg &msg)
             {
                for(auto &item:_items)
                {
                    item->format(out,msg);
                }
             }
             std::string format(const LogMsg &msg)
             {
                std::stringstream ss;
                format(ss,msg);
                return ss.str();
             }
             private:
             //解析日志格式
             bool parsePattern()
             {
                //1.对格式化规则字符串进行解析
                //%cab%%cde[%d{%H:%M:%S}][%p]%T%m%n
                std::vector<std::pair<std::string,std::string>> fmt_order;
                size_t pos=0;
                std::string key;
                std::string val;
                while(pos<_pattern.size())
                {
                    //处理原始字符，判断是否是%，不是就是原始字符
                    
                    if(_pattern[pos]!='%')
                    {
                        val.push_back(_pattern[pos++]);
                        continue;
                    }
                    //代码能走下来就说明pos位置是%
                    if(pos+1<_pattern.size()&&_pattern[pos+1]=='%')
                    {
                        val.push_back('%');
                        pos+=2;
                        continue;
                    }
                    
                    //能走到这代表%后面是个格式化字符，原始字符串处理完毕
                    if(val.empty()==false)
                    {
                        fmt_order.push_back(std::make_pair("",val));
                        val.clear();
                    }
                   
                    //这时pos指向的是%位置，是格式化字符的处理
                    pos+=1;//这一步之后，pos指向格式化字符的位置
                    if(pos==_pattern.size())
                    {
                        std::cout<<"%之后，没有对应的格式化字符！\n";
                        return false;
                    }
                    key=_pattern[pos];
                    //这是pos指向格式化字符后的位置
                    pos+=1;
                    if(pos<_pattern.size()&&_pattern[pos]=='{')
                    {
                        pos+=1;//这时候pos指向子规则的起始位置
                        while(pos<_pattern.size()&&_pattern[pos]!='}')
                        {
                            val.push_back(_pattern[pos++]);
                        }
                        //走到末尾跳出了循环，代表没有找到}，格式是错误的
                        if(pos==_pattern.size())
                        {
                            std::cout<<"子规则{}匹配出错！\n";
                            return false;//没有找到}
                        }
                        pos+=1;//这时候pos指向的是}的位置，向后走一步，就走到了下次处理的新位置
                    }
                    fmt_order.push_back(std::make_pair(key,val));
                    key.clear();
                    val.clear();
                }
                //2.根据解析得到的数据初始化格式化子项数组成员
                for(auto &it : fmt_order)
                {
                    _items.push_back(createItem(it.first,it.second));
                }
                return true;
             }
        
        //根据格式化字符串中的占位符创建对应的格式化子类对象
            FormatItem::ptr createItem(const std::string &key,const std::string &val)
            {
                //%d表示日期
                //%t表示线程id
                //%c表示日志器名称
                //%f表示源文件名称
                //%l表示源文件行号
                //%p表示日志级别
                //%m表示日志主体消息
                //%n表示换行符
                //%T表示制表符缩进
               if(key=="d")return std::make_shared<TimeFormatItem>(val);
               if(key=="t")return std::make_shared<ThreadFormatItem>();
               if(key=="c")return std::make_shared<LoggerFormatItem>();
               if(key=="f")return std::make_shared<FileFormatItem>();
               if(key=="l")return std::make_shared<LineFormatItem>();
               if(key=="p")return std::make_shared<LevelFormatItem>();
               if(key=="T")return std::make_shared<TabFormatItem>();
               if(key=="m")return std::make_shared<MsgFormatItem>();
               if(key=="n")return std::make_shared<NLineFormatItem>();
               if(key.empty())return std::make_shared<OtherFormatItem>(val);
               std::cout<<"未知的格式化占位符！%"<<key<<std::endl;
               abort();
               return FormatItem::ptr();
            }  
            //创建格式化子类对象
        private:
            std::string _pattern;  //日志格式
            std::vector<FormatItem::ptr> _items;  //格式化子类

    };
}


#endif