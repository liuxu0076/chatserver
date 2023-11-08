//
// Created by 蛋蛋 on 11/1/23.
//

#ifndef CHAT_DB_H
#define CHAT_DB_H

#include <mysql/mysql.h>
#include <string>
#include <muduo/base/Logging.h>

using namespace std;

//数据库操作类
class MY_SQL{
public:
    //初始化数据库连接
    MY_SQL();
    //释放数据库连接资源
    ~MY_SQL();
    //连接数据库
    bool connect();
    //更新操作
    bool update(string sql);
    //查询操作
    MYSQL_RES* query(string sql);
    //获取连接
    MYSQL* getConnection();
private:
    MYSQL *_conn;
};

#endif //CHAT_DB_H
