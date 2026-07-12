#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <cerrno>

using namespace std;

static bool verbose = false;

static bool starts_with(const string &s, const string &prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static double clamp_double(double v, double lo, double hi)
{
    if (v < lo)
        return lo;

    if (v > hi)
        return hi;

    return v;
}

static bool wait_socket(int sock, int timeout_ms)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sock + 1, &rfds, NULL, NULL, &tv);

    if (ret < 0)
    {
        if (errno == EINTR)
        {
            return false;
        }

        perror("select");
        return false;
    }

    return ret > 0 && FD_ISSET(sock, &rfds);
}

static bool recv_msg(int sock, string &msg, sockaddr_in &from)
{
    char buf[8192];
    socklen_t from_len = sizeof(from);
    memset(&from, 0, sizeof(from));

    ssize_t n = recvfrom(sock,
                         buf,
                         sizeof(buf) - 1,
                         0,
                         (sockaddr *)&from,
                         &from_len);

    if (n < 0)
    {
        return false;
    }

    buf[n] = '\0';
    msg = buf;

    return true;
}

static bool send_msg(int sock, const sockaddr_in &addr, const string &msg)
{
    ssize_t n = sendto(sock,
                       msg.c_str(),
                       msg.size() + 1,
                       0,
                       (const sockaddr *)&addr,
                       sizeof(addr));

    if (n < 0)
    {
        perror("sendto");
        return false;
    }

    if (verbose)
    {
        cout << "[send] " << msg << endl;
    }

    return true;
}

static string cmd_turn(double angle)
{
    angle = clamp_double(angle, -180.0, 180.0);

    char buf[128];
    snprintf(buf, sizeof(buf), "(turn %.2f)", angle);

    return string(buf);
}

static string cmd_dash(double power)
{
    power = clamp_double(power, -100.0, 100.0);

    char buf[128];
    snprintf(buf, sizeof(buf), "(dash %.2f)", power);

    return string(buf);
}

static string cmd_kick(double power, double dir)
{
    power = clamp_double(power, 0.0, 100.0);
    dir = clamp_double(dir, -180.0, 180.0);

    char buf[128];
    snprintf(buf, sizeof(buf), "(kick %.2f %.2f)", power, dir);

    return string(buf);
}

static string cmd_say(const string &s)
{
    return "(say " + s + ")";
}

static bool parse_object_by_key(const string &msg,
                                const string &key,
                                double &dist,
                                double &dir)
{
    size_t pos = msg.find(key);

    if (pos == string::npos)
    {
        return false;
    }

    pos += key.size();

    while (pos < msg.size() && msg[pos] == ' ')
    {
        pos++;
    }

    char *endptr = nullptr;

    dist = strtod(msg.c_str() + pos, &endptr);

    if (endptr == msg.c_str() + pos)
    {
        return false;
    }

    dir = strtod(endptr, &endptr);

    return true;
}

static bool parse_ball(const string &msg, double &dist, double &dir)
{
    return parse_object_by_key(msg, "((b)", dist, dir);
}

/*
    根据本方所在 side 判断进攻球门。

    side == 'l':
        本方在左边，进攻右球门 g r。

    side == 'r':
        本方在右边，进攻左球门 g l。
*/
static bool parse_goal(const string &msg,
                       char side,
                       double &dist,
                       double &dir)
{
    if (side == 'l')
    {
        return parse_object_by_key(msg, "((g r)", dist, dir);
    }
    else
    {
        return parse_object_by_key(msg, "((g l)", dist, dir);
    }
}

static bool parse_teammate_exact(const string &msg,
                                 const string &team,
                                 int unum,
                                 double &dist,
                                 double &dir)
{
    string key = "((p \"" + team + "\" " + to_string(unum) + ")";
    return parse_object_by_key(msg, key, dist, dir);
}

/*
    备用队友解析：
        有时服务器视觉消息里能看到队友，但不一定能看到号码。
        这个函数只要找到本队球员，就提取其距离和方向。
*/
static bool parse_any_teammate(const string &msg,
                               const string &team,
                               double &dist,
                               double &dir)
{
    string key = "((p \"" + team + "\"";
    size_t pos = msg.find(key);

    if (pos == string::npos)
    {
        return false;
    }

    size_t close_pos = msg.find(")", pos);

    if (close_pos == string::npos)
    {
        return false;
    }

    close_pos++;

    while (close_pos < msg.size() && msg[close_pos] == ' ')
    {
        close_pos++;
    }

    char *endptr = nullptr;

    dist = strtod(msg.c_str() + close_pos, &endptr);

    if (endptr == msg.c_str() + close_pos)
    {
        return false;
    }

    dir = strtod(endptr, &endptr);

    return true;
}

static bool parse_teammate(const string &msg,
                           const string &team,
                           int target_unum,
                           double &dist,
                           double &dir)
{
    if (parse_teammate_exact(msg, team, target_unum, dist, dir))
    {
        return true;
    }

    return parse_any_teammate(msg, team, dist, dir);
}

/*
    判断球是否在可踢范围内。

    rcssserver 中默认可踢距离大约在 1 米左右。
    这里取 1.0，比 0.7 更稳。
*/
static bool ball_kickable(double ballDist)
{
    return ballDist < 1.0;
}

/*
    追球逻辑：
        看不到球：转身找球
        看到球但方向偏差大：转向球
        方向基本正确：向球冲刺
*/
static string action_chase_ball(bool seeBall,
                                double ballDist,
                                double ballDir)
{
    if (!seeBall)
    {
        return cmd_turn(60);
    }

    if (fabs(ballDir) > 12.0)
    {
        return cmd_turn(ballDir);
    }

    if (ballDist > 8.0)
    {
        return cmd_dash(100);
    }

    if (ballDist > 3.0)
    {
        return cmd_dash(80);
    }

    return cmd_dash(60);
}

/*
    带球或射门：
        距离对方球门 < 20 米：
            大力射门。
        否则：
            小力量朝球门方向踢，形成带球推进。
*/
static string action_dribble_or_shoot(bool seeGoal,
                                      double goalDist,
                                      double goalDir)
{
    if (seeGoal)
    {
        if (goalDist < 20.0)
        {
            return cmd_kick(100, goalDir);
        }

        double power = 28.0;

        if (goalDist > 35.0)
        {
            power = 32.0;
        }

        return cmd_kick(power, goalDir);
    }

    /*
        看不到球门时，先轻轻向正前方带球。
    */
    return cmd_kick(25, 0);
}

/*
    向队友传球：
        根据队友距离调整力量。
*/
static string action_pass_to_teammate(double mateDist,
                                      double mateDir)
{
    double power = 35.0 + mateDist * 2.0;
    power = clamp_double(power, 40.0, 80.0);

    return cmd_kick(power, mateDir);
}

/*
    任务一：
        一个球员：
            追球 -> 带球推进 -> 小于 20 米射门。
*/
static string decide_one_player(const string &seeMsg,
                                char side)
{
    double ballDist = 0;
    double ballDir = 0;
    double goalDist = 0;
    double goalDir = 0;

    bool seeBall = parse_ball(seeMsg, ballDist, ballDir);
    bool seeGoal = parse_goal(seeMsg, side, goalDist, goalDir);

    if (!seeBall)
    {
        return cmd_turn(60);
    }

    if (!ball_kickable(ballDist))
    {
        return action_chase_ball(seeBall, ballDist, ballDir);
    }

    return action_dribble_or_shoot(seeGoal, goalDist, goalDir);
}

/*
    任务二：
        两个球员：
            stage 0:
                1号追球，拿到球后传给2号，说 P1。

            stage 1:
                2号听到 P1 后追球，拿到球后传回1号，说 P2。

            stage 2:
                1号听到 P2 后追球，带球推进，小于20米射门。
*/
static string decide_two_players(const string &seeMsg,
                                 const string &team,
                                 char side,
                                 int unum,
                                 int &stage,
                                 bool &alreadySaidPass1,
                                 bool &alreadySaidPass2,
                                 int sock,
                                 const sockaddr_in &serverAddr)
{
    double ballDist = 0;
    double ballDir = 0;
    double goalDist = 0;
    double goalDir = 0;

    bool seeBall = parse_ball(seeMsg, ballDist, ballDir);
    bool seeGoal = parse_goal(seeMsg, side, goalDist, goalDir);

    /*
        本实验只用 1 号和 2 号。
        如果误启动更多球员，其它球员只原地观察。
    */
    if (unum != 1 && unum != 2)
    {
        return cmd_turn(30);
    }

    /*
        stage 0:
            1号负责第一脚传球。
    */
    if (stage == 0)
    {
        if (unum == 1)
        {
            if (!seeBall || !ball_kickable(ballDist))
            {
                return action_chase_ball(seeBall, ballDist, ballDir);
            }

            double mateDist = 0;
            double mateDir = 0;
            bool seeMate = parse_teammate(seeMsg, team, 2, mateDist, mateDir);

            if (seeMate)
            {
                /*
                    先广播 P1，让 2 号进入接球阶段。
                */
                if (!alreadySaidPass1)
                {
                    send_msg(sock, serverAddr, cmd_say("P1"));
                    alreadySaidPass1 = true;
                }

                stage = 1;

                return action_pass_to_teammate(mateDist, mateDir);
            }

            /*
                球在脚下但看不到2号，转身找队友。
            */
            return cmd_turn(45);
        }
        else
        {
            /*
                2号等待接球，缓慢转身观察。
            */
            return cmd_turn(20);
        }
    }

    /*
        stage 1:
            2号负责第二脚传球。
    */
    if (stage == 1)
    {
        if (unum == 2)
        {
            if (!seeBall || !ball_kickable(ballDist))
            {
                return action_chase_ball(seeBall, ballDist, ballDir);
            }

            double mateDist = 0;
            double mateDir = 0;
            bool seeMate = parse_teammate(seeMsg, team, 1, mateDist, mateDir);

            if (seeMate)
            {
                /*
                    广播 P2，让 1 号进入带球射门阶段。
                */
                if (!alreadySaidPass2)
                {
                    send_msg(sock, serverAddr, cmd_say("P2"));
                    alreadySaidPass2 = true;
                }

                stage = 2;

                return action_pass_to_teammate(mateDist, mateDir);
            }

            /*
                球在脚下但看不到1号，转身找队友。
            */
            return cmd_turn(45);
        }
        else
        {
            /*
                1号等待回传。
            */
            return cmd_turn(20);
        }
    }

    /*
        stage 2:
            1号带球射门。
            2号不再抢球，避免干扰。
    */
    if (stage >= 2)
    {
        if (unum == 1)
        {
            if (!seeBall || !ball_kickable(ballDist))
            {
                return action_chase_ball(seeBall, ballDist, ballDir);
            }

            return action_dribble_or_shoot(seeGoal, goalDist, goalDir);
        }
        else
        {
            return cmd_turn(20);
        }
    }

    return cmd_turn(30);
}

static void print_usage(const char *prog)
{
    cout << "Usage:" << endl;
    cout << "  " << prog << endl;
    cout << "  " << prog << " one" << endl;
    cout << "  " << prog << " two" << endl;
    cout << "  " << prog << " MODE TEAM SERVER_IP" << endl;
    cout << endl;
    cout << "Default:" << endl;
    cout << "  MODE      = one" << endl;
    cout << "  TEAM      = lab2" << endl;
    cout << "  SERVER_IP = 127.0.0.1" << endl;
    cout << endl;
    cout << "MODE:" << endl;
    cout << "  one    one player dribble and shoot" << endl;
    cout << "  two    two players pass twice then shoot" << endl;
    cout << endl;
    cout << "Normally, do not run this program manually." << endl;
    cout << "Use ./lab2_2.sh instead." << endl;
}

int main(int argc, char *argv[])
{
    string mode = "one";
    string team = "lab2";
    string server_ip = "127.0.0.1";

    if (argc >= 2)
    {
        string arg1 = argv[1];

        if (arg1 == "help" || arg1 == "-h" || arg1 == "--help")
        {
            print_usage(argv[0]);
            return 0;
        }

        if (arg1 == "one" || arg1 == "two")
        {
            mode = arg1;
        }
        else
        {
            cerr << "[error] invalid mode: " << arg1 << endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (argc >= 3)
    {
        team = argv[2];
    }

    if (argc >= 4)
    {
        server_ip = argv[3];
    }

    if (argc > 4)
    {
        cerr << "[error] too many arguments." << endl;
        print_usage(argv[0]);
        return 1;
    }

    int server_port = 6000;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        perror("socket");
        return 1;
    }

    sockaddr_in initAddr;
    memset(&initAddr, 0, sizeof(initAddr));
    initAddr.sin_family = AF_INET;
    initAddr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &initAddr.sin_addr) <= 0)
    {
        cerr << "[error] invalid server ip: " << server_ip << endl;
        close(sock);
        return 1;
    }

    cout << "============================================" << endl;
    cout << " Lab2 RoboCup Auto Player" << endl;
    cout << " Team   : " << team << endl;
    cout << " Server : " << server_ip << ":" << server_port << endl;
    cout << " Mode   : " << mode << endl;
    cout << " Note   : one process = one player" << endl;
    cout << "============================================" << endl;

    /*
        初始化球员。
    */
    string initCmd = "(init " + team + " (version 15))";
    send_msg(sock, initAddr, initCmd);

    sockaddr_in actionAddr;
    memset(&actionAddr, 0, sizeof(actionAddr));

    char side = 'l';
    int unum = 0;
    bool initialized = false;

    while (!initialized)
    {
        if (!wait_socket(sock, 5000))
        {
            cerr << "[error] timeout waiting init response." << endl;
            close(sock);
            return 1;
        }

        string msg;
        sockaddr_in from;

        if (!recv_msg(sock, msg, from))
        {
            continue;
        }

        if (starts_with(msg, "(init"))
        {
            initialized = true;
            actionAddr = from;

            /*
                init 消息通常形如：
                    (init l 1 before_kick_off)
                    (init r 2 before_kick_off)
            */
            sscanf(msg.c_str(), "(init %c %d", &side, &unum);

            cout << "[init] " << msg << endl;
            cout << "[info] side = " << side << ", unum = " << unum << endl;
        }
    }

    /*
        使用宽视野，便于看到球、球门和队友。
    */
    send_msg(sock, actionAddr, "(change_view wide high)");

    /*
        开球前移动球员到实验所需位置。

        one:
            单人放在后场。

        two:
            1号在后场中路；
            2号在前方偏侧位置，便于 1号 -> 2号 -> 1号。
    */
    if (mode == "one")
    {
        if (side == 'l')
        {
            send_msg(sock, actionAddr, "(move -35 0)");
        }
        else
        {
            send_msg(sock, actionAddr, "(move 35 0)");
        }
    }
    else
    {
        if (side == 'l')
        {
            if (unum == 1)
            {
                send_msg(sock, actionAddr, "(move -35 0)");
            }
            else if (unum == 2)
            {
                send_msg(sock, actionAddr, "(move -20 10)");
            }
            else
            {
                send_msg(sock, actionAddr, "(move -40 -15)");
            }
        }
        else
        {
            if (unum == 1)
            {
                send_msg(sock, actionAddr, "(move 35 0)");
            }
            else if (unum == 2)
            {
                send_msg(sock, actionAddr, "(move 20 10)");
            }
            else
            {
                send_msg(sock, actionAddr, "(move 40 -15)");
            }
        }
    }

    cout << "[info] player initialized." << endl;
    cout << "[info] if game has not started, click Kick Off in rcssmonitor." << endl;

    bool play_on = false;

    /*
        双人任务状态。
    */
    int stage = 0;
    bool alreadySaidPass1 = false;
    bool alreadySaidPass2 = false;

    /*
        避免同一个 see 周期重复决策。
    */
    int lastSeeTime = -1;

    while (true)
    {
        if (!wait_socket(sock, 1000))
        {
            continue;
        }

        string msg;
        sockaddr_in from;

        if (!recv_msg(sock, msg, from))
        {
            continue;
        }

        if (verbose)
        {
            cout << "[recv] " << msg << endl;
        }

        /*
            监听裁判消息。
        */
        if (msg.find("referee play_on") != string::npos)
        {
            if (!play_on)
            {
                cout << "[info] referee play_on received." << endl;
            }

            play_on = true;
        }

        /*
            双人模式：
                监听队友 say 的 P1 / P2。
        */
        if (mode == "two" && starts_with(msg, "(hear"))
        {
            if (msg.find("P1") != string::npos)
            {
                if (stage < 1)
                {
                    stage = 1;
                    cout << "[info] heard P1, stage = 1" << endl;
                }
            }

            if (msg.find("P2") != string::npos)
            {
                if (stage < 2)
                {
                    stage = 2;
                    cout << "[info] heard P2, stage = 2" << endl;
                }
            }
        }

        /*
            未开球时不执行动作。
        */
        if (!play_on)
        {
            continue;
        }

        /*
            只基于 see 消息进行动作决策。
        */
        if (!starts_with(msg, "(see"))
        {
            continue;
        }

        int seeTime = -1;
        sscanf(msg.c_str(), "(see %d", &seeTime);

        if (seeTime == lastSeeTime)
        {
            continue;
        }

        lastSeeTime = seeTime;

        string action;

        if (mode == "one")
        {
            action = decide_one_player(msg, side);
        }
        else
        {
            action = decide_two_players(msg,
                                        team,
                                        side,
                                        unum,
                                        stage,
                                        alreadySaidPass1,
                                        alreadySaidPass2,
                                        sock,
                                        actionAddr);
        }

        if (!action.empty())
        {
            send_msg(sock, actionAddr, action);

            cout << "[t=" << seeTime
                 << " unum=" << unum
                 << " stage=" << stage
                 << "] " << action << endl;
        }
    }

    close(sock);
    return 0;
}
