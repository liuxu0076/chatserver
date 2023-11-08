//
// Created by 蛋蛋 on 11/1/23.
//

#ifndef CHAT_CHATSERVICE_H
#define CHAT_CHATSERVICE_H

#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include <functional>
#include <mutex>

#include "usermodel.h"
#include "offlinemessagemodel.h"
#include "friendmodel.h"
#include "groupmodel.h"
#include "redis.h"
#include "json.hpp"

using json = nlohmann::json;
using namespace std;
using namespace muduo;
using namespace muduo::net;

//表示处理消息的事件回调方法类型
using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp time)>;

//聊天服务器业务类
class ChatService{
public:
    //获取单例对象的接口函数
    static ChatService* instance();

    //处理登录业务
    void login(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //处理注册业务
    void reg(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //处理注销业务
    void loginout(const TcpConnectionPtr& conn, json& js, Timestamp time);
    //一对一聊天业务
    void oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time);
    //添加好友业务
    void addFriend(const TcpConnectionPtr& conn, json& js, Timestamp time);
    //创建群组业务
    void createGroup(const TcpConnectionPtr& conn, json& js, Timestamp time);
    //加入群组业务
    void addGroup(const TcpConnectionPtr& conn, json& js, Timestamp time);
    //群组聊天业务
    void groupChat(const TcpConnectionPtr& conn, json& js, Timestamp time);
    //获取消息对应的处理器
    MsgHandler getHandler(int msgid);
    //服务器异常，业务重置方法
    void reset();

    void handleRedisSubscribeMessage(int, const string&);

    //处理客户端异常退出
    void clientCloseException(const TcpConnectionPtr &conn);
private:
    ChatService();
    //存储消息id和其对应的业务处理方法
    unordered_map<int, MsgHandler> _msgHandlerMap;

    //存储在线用户的通信连接
    unordered_map<int, TcpConnectionPtr> _userConnMap;

    //定义互斥锁，保证——userConnMap的线程安全问题
    mutex _connMutex;
    //数据操作类对象
    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    //redis操作对象
    Redis _redis;
};

#endif //CHAT_CHATSERVICE_H