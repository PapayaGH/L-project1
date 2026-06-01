// 实用工具的实现
// 1.获取系统时间
// 2判断文件是否存在
// 3.获取文件所在路径
// 4.创建目录
#ifndef __M_UTIL_HPP_
#define __M_UTIL_HPP_
#include<string>
#include<sys/stat.h>
#include<iostream>
#include<ctime>
#include<cstddef>
#include<unistd.h>

namespace ppalog
{
    namespace util
    {
        class Date
        {
            public:
                static size_t getTime()     //获取系统时间，单位为秒
                {
                    return (size_t)time(nullptr);
                }
        };
        class File
        {
            public:
                static bool exists(const std::string& pathname)   //判断文件是否存在
                {
                    struct stat st;
                    if(stat(pathname.c_str(), &st) < 0)
                    {
                        return false;
                    }
                    return true;
                   // return (access(pathname.c_str(), F_OK) == 0);  只能在linux系统上使用，不能在windows系统上使用
                }
                static std::string path(const std::string& pathname)  //获取文件所在路径
                {
                    size_t pos=pathname.find_last_of("/\\");
                    if(pos != std::string::npos)
                    {
                        return pathname.substr(0, pos+1);
                    }
                    return ".";
                }
                static void createDirectory(const std::string& pathname)   //创建目录
                {
                    size_t pos=0,idx=0;
                    while(idx<pathname.size())
                    {
                        pos =pathname.find_first_of("/\\", idx);
                        if(pos == std::string::npos)
                        {
                            mkdir(pathname.c_str(), 0777);
                        }
                        std::string parent_dir=pathname.substr(0, pos+1);
                        // if（parent_dir == "."||parent_dir == "..")
                        // {
                        //     idx=pos+1;
                        //     continue;
                        // }
                        if(exists(parent_dir)==true)
                        {
                            idx=pos+1;
                            continue;
                        }
                        mkdir(parent_dir.c_str(), 0777);
                        idx=pos+1;
                    }
                }
        };
    }
}


#endif