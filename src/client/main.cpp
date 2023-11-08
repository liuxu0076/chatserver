#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <chrono>
#include <ctime>

using namespace std;
using json = nlohmann::json;

#include "unistd.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "group.h"
#include "user.h"
#include "public.h"

//记录当前系统登录的用户信息
User g_currentUser;
//记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
//记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
//显示当前登录成功用户的基本信息
void showCurrentUserData();
//控制主菜单页面程序
bool isMainMenuRunning = false;

//接收线程
void readTaskHandler(int clientfd);
//获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime();
//主聊天页面程序
void mainMenu(int clientfd);

//聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char** argv){
    if (argc < 3){
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    //解析通过命令行参数传递的ip和port
    char* ip = argv[1];
    uint16_t port = atoi(argv[2]);

    //创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd){
        cerr << "socket create error" << endl;
        exit(-1);
    }

    //填写client需要连接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    //client与server进行连接
    if (-1 == connect(clientfd, (sockaddr*)&server, sizeof(sockaddr_in))){
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    //main线程用于接收用户输入，负责发送数据
    for(;;){
        //显示首页面菜单 登录、注册、退出
        cout << "========================" << endl;
        cout << "1.login" << endl;
        cout << "2.register" << endl;
        cout << "3.quit" << endl;
        cout << "========================" << endl;
        cout << "choice" << endl;
        int choice = 0;
        cin >> choice;
        cin.get();//读掉缓冲区残留的回车

        switch (choice) {
            case 1://login 业务
            {
                int id = 0;
                char pwd[50] = {0};
                cout << "userid:";
                cin >> id;
                cin.get();//读掉缓冲区残留的回车
                cout << "userpassword:";
                cin.getline(pwd, 50);

                json js;
                js["msgid"] = LOGIN_MSG;
                js["id"] = id;
                js["password"] = pwd;
                string request = js.dump();

                int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
                if (len == -1){
                    cerr << "send login msg error:" << request << endl;
                } else{
                    char buffer[1024] = {0};
                    len = recv(clientfd, buffer, 1024, 0);
                    if (-1 == len){
                        cerr << "recv login response error" << endl;
                    } else{
                        json responsejs = json::parse(buffer);
                        if (0 != responsejs["errno"].get<int>()){
                            cerr << responsejs["errmsg"] << endl;
                        } else{//登录成功
                            //记录当前用户的id和name
                            g_currentUser.setId(responsejs["id"].get<int>());
                            g_currentUser.setName(responsejs["name"]);

                            //记录当前用户的好友列表信息
                            if (responsejs.contains("friends")){
                                //初始化好友信息
                                g_currentUserFriendList.clear();
                                vector<string> vec = responsejs["friends"];
                                for (string& str : vec){
                                    json js = js.parse(str);
                                    User user;
                                    user.setId(js["id"].get<int>());
                                    user.setName(js["name"]);
                                    user.setState(js["state"]);
                                    g_currentUserFriendList.push_back(user);
                                }
                            }

                            //记录当前用户的群组列表信息
                            if (responsejs.contains("groups")){
                                //初始化群组信息
                                g_currentUserGroupList.clear();
                                vector<string> vec1 = responsejs["groups"];
                                for (string& groupstr : vec1){
                                    json grpjs = json::parse(groupstr);
                                    Group group;
                                    group.setId(grpjs["id"].get<int>());
                                    group.setName(grpjs["groupname"]);
                                    group.setDesc(grpjs["groupdesc"]);

                                    vector<string> vec2 = grpjs["users"];
                                    for (string& userstr : vec2){
                                        GroupUser user;
                                        json js = json::parse(userstr);
                                        user.setId(js["id"].get<int>());
                                        user.setName(js["name"]);
                                        user.setState(js["state"]);
                                        user.setRole(js["role"]);
                                        group.getUsers().push_back(user);
                                    }
                                    g_currentUserGroupList.push_back(group);
                                }
                            }

                            //显示登录用户的基本信息
                            showCurrentUserData();

                            //显示当前用户的离线消息 个人聊天信息或群组消息
                            if (responsejs.contains("offlinemsg")){
                                vector<string> vec = responsejs["offlinemsg"];
                                for (string& str : vec){
                                    json js = json::parse(str);
                                    //time +[id]+name+"said: " + xxx
                                    if (ONE_CHAT_MSG == js["msgid"].get<int>()) {
                                        cout << js["time"] << " [" << js["id"] << "]"
                                             << js["name"] << " said: " << js["msg"] << endl;
                                    } else{
                                        cout << "groupmsg[" << js["groupid"] << "]:" <<
                                             js["time"].get<string>() << " [" << js["id"] << "]"
                                             << js["name"].get<string>() <<
                                                     " said:" << js["msg"].get<string>() << endl;
                                    }
                                }
                            }

                            //登录成功，启动接收线程负责接收数据,该线程只启动一次
                            static int readthreadnumber = 0;
                            if (readthreadnumber == 0){
                                std::thread readTask(readTaskHandler, clientfd);
                                readTask.detach();
                                ++readthreadnumber;
                            }


                            //进入聊天主菜单页面
                            isMainMenuRunning = true;
                            mainMenu(clientfd);
                        }
                    }
                }

            }
            break;
            case 2://register业务
            {
                char name[50] = {0};
                char pwd[50] = {0};
                cout << "username:";
                cin.getline(name, 50);
                cout << "userpassword:";
                cin.getline(pwd, 50);

                json js;
                js["msgid"] = REG_MSG;
                js["name"] = name;
                js["password"] = pwd;
                string request = js.dump();

                int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
                if (len == -1){
                    cerr << "send reg msg error:" << request << endl;
                } else{
                    char buffer[1024] = {0};
                    len = recv(clientfd, buffer, 1024, 0);
                    if (-1 == len){
                        cerr << "recv reg response error" << endl;
                    } else{
                        json reponsejs = json::parse(buffer);
                        if (0 != reponsejs["errno"].get<int>()){//注册失败
                            cerr << name << " is already exist, register error!" << endl;
                        } else {//注册成功
                            cout << name << "register success, userid is " << reponsejs["id"]
                                << ", do not forget it!" << endl;
                        }
                    }
                }
            }
            break;
            case 3://quit业务
            {
                close(clientfd);
                exit(0);
            }
            default:
                cerr << "invalid input !" << endl;
                break;
        }
    }
    return 0;
}

//显示当前登录成功用户的基本信息
void showCurrentUserData(){
    cout << "========================login user========================" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "------------------------friend list------------------------" << endl;
    if (!g_currentUserFriendList.empty()){
        for (User& user: g_currentUserFriendList){
            cout << "friendid:" << user.getId() << " friendname: "<<
            user.getName() << " friendstate:" << user.getState() << endl;
        }
    }
    cout << "------------------------group list------------------------" << endl;
    if (!g_currentUserGroupList.empty()){
        for (Group& group : g_currentUserGroupList){
            cout << "groupid:" <<group.getId() << " groupname:" << group.getName()
            << " groupdesc:" << group.getDesc() << endl;
            for (GroupUser& user : group.getUsers()){
                cout << "groupuserid:" << user.getId() << " groupusername:" <<
                user.getName() << " groupuserstate:" << user.getState()
                << " groupuserstate:" << user.getRole() << endl;
            }
        }
    }
    cout << "==========================================================" << endl;
}

//接收线程
void readTaskHandler(int clientfd){
    for(;;){
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);
        if (-1 == len || 0 == len){
            close(clientfd);
            exit(-1);
        }

        //接收ChatServer转发的数据，反序列化为json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        if (ONE_CHAT_MSG == msgtype){
            cout << js["time"].get<string>() << " [" << js["id"] << "]"
            << js["name"].get<string>() << " said:" << js["msg"].get<string>() << endl;
            continue;
        } else if (GROUP_CHAT_MSG == msgtype){
            cout << "groupmsg[" << js["groupid"] << "]:" <<
            js["time"].get<string>() << " [" << js["id"] << "]"
                 << js["name"].get<string>() << " said:" << js["msg"].get<string>() << endl;
            continue;
        }
    }
}
//获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime(){
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm* ptm = localtime(&tt);
    char date[60] = {0};

    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int) ptm->tm_year + 1900, (int) ptm->tm_mon + 1, (int) ptm->tm_mday,
            (int) ptm->tm_hour, (int) ptm->tm_min, (int) ptm->tm_sec);
    return std::string(date);
}

//"help" command handler
void help(int fd = 0, string str = "");
//"chat" command handler
void chat(int, string);
//"addfriend" command handler
void addfriend(int, string);
//"creategroup" command handler
void creategroup(int, string);
//"addgroup" command handler
void addgroup(int, string);
//"groupchat" command handler
void groupchat(int, string);
//"loginout" command handler
void loginout(int, string);

//系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
        {"help", "show all supported command, ex help"},
        {"chat", "one to one chat, ex chat:friendid:message"},
        {"addfriend", "add friend, ex addfriend:friendid"},
        {"creategroup", "create group, ex creategroup:groupname:groupdesc"},
        {"addgroup", "add group, ex addgroup:groupid"},
        {"groupchat", "group chat, ex groupchat:groupid:message"},
        {"loginout", "login out, ex loginout"}
};

//注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
        {"help", help},
        {"chat", chat},
        {"addfriend", addfriend},
        {"creategroup", creategroup},
        {"addgroup", addgroup},
        {"groupchat", groupchat},
        {"loginout", loginout}
};
//主聊天页面程序
void mainMenu(int clientfd){
    help();

    char buffer[1024] = {0};

    while (isMainMenuRunning){
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command;//存储命令
        int idx = commandbuf.find(":");
        if (-1 == idx){
            command = commandbuf;
        } else{
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end()){
            cerr << "invalid input command!" << endl;
            continue;
        }
        //调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能不需要修改该函数
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx));//调用命令处理方法

    }
}

//"help" command handler
void help(int, string){
    cout << "show command list >>> " << endl;
    for (auto& p : commandMap){
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}

//"chat" command handler
void chat(int clientfd, string str){
    int idx = str.find(":");//friendid:message
    if (-1 == idx){
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();

    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len){
        cerr << "send chat msg error -> " << buffer << endl;
    }
}

//"addfriend" command handler
void addfriend(int clientfd, string str){
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len){
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

//"creategroup" command handler
void creategroup(int clientfd, string str){
    int idx = str.find(":");
    if (-1 == idx){
        cerr << "invalid creategroup command!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;

    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len){
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}
//"addgroup" command handler
void addgroup(int clientfd, string str) {
    int groupid = stoi(str);
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;

    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len){
        cerr << "send addgroup msg error ->" << buffer << endl;
        return;
    }
}
//"groupchat" command handler
void groupchat(int clientfd, string str) {
    int idx = str.find(":");//groupid:message
    if (-1 == idx){
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["groupid"] = groupid;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();

    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len){
        cerr << "send groupchat msg error -> " << buffer << endl;
    }
}
//"loginout" command handler
void loginout(int clientfd, string str) {
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len){
        cerr << "send loginout msg error -> " << buffer << endl;
    } else{
        isMainMenuRunning = false;
    }
}