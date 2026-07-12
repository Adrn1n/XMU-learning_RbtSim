#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

using namespace std;

static bool verbose = false;

static bool starts_with(const string &s, const string &prefix)
{
    if (s.size() < prefix.size())
        return false;
    return s.compare(0, prefix.size(), prefix) == 0;
}

static string addr_to_string(const sockaddr_in &addr)
{
    char ip[64];
    memset(ip, 0, sizeof(ip));
    inet_ntop(AF_INET, &(addr.sin_addr), ip, sizeof(ip));

    char buf[128];
    snprintf(buf, sizeof(buf), "%s:%d", ip, ntohs(addr.sin_port));
    return string(buf);
}

static bool send_msg(int sock, const sockaddr_in &addr, const string &msg)
{
    ssize_t n = sendto(sock,
                       msg.c_str(),
                       msg.size() + 1, // 注意这里 +1，发送字符串结尾 '\0'
                       0,
                       (const sockaddr *)&addr,
                       sizeof(addr));

    if (n < 0)
    {
        perror("sendto");
        return false;
    }

    cout << "[send] " << msg << endl;
    return true;
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

static void print_help(bool goalie)
{
    cout << endl;
    cout << "================ Lab2 Commands ================" << endl;
    cout << "Input FULL command with parentheses:" << endl;
    cout << endl;
    cout << "  (turn 90)" << endl;
    cout << "  (turn -90)" << endl;
    cout << "  (turn_neck 45)" << endl;
    cout << "  (dash 100)" << endl;
    cout << "  (dash -50)" << endl;
    cout << "  (kick 100 0)" << endl;
    cout << "  (kick 60 45)" << endl;
    cout << "  (tackle 100)" << endl;
    cout << "  (change_view wide high)" << endl;
    cout << "  (change_view narrow high)" << endl;
    cout << "  (sense_body)" << endl;

    if (goalie)
    {
        cout << "  (catch 0)" << endl;
    }
    else
    {
        cout << "  catch is only valid for goalie." << endl;
    }

    cout << endl;
    cout << "Helper commands:" << endl;
    cout << "  help        show help" << endl;
    cout << "  center      move to (-0.5, 0), useful for kick test" << endl;
    cout << "  center0     move to (0, 0)" << endl;
    cout << "  goaliepos   move to (-50, 0)" << endl;
    cout << "  verbose on  show all server messages" << endl;
    cout << "  verbose off hide normal server messages" << endl;
    cout << "  quit        exit" << endl;
    cout << "================================================" << endl;
    cout << endl;
}

static void drain_messages(int sock,
                           bool print_sense_body,
                           bool &play_on)
{
    int count = 0;

    while (count < 200 && wait_socket(sock, 0))
    {
        string msg;
        sockaddr_in from;

        if (!recv_msg(sock, msg, from))
        {
            break;
        }

        bool important = false;

        if (verbose)
            important = true;

        if (starts_with(msg, "(error"))
            important = true;
        if (starts_with(msg, "(warning"))
            important = true;

        if (print_sense_body && starts_with(msg, "(sense_body"))
        {
            important = true;
        }

        if (msg.find("referee play_on") != string::npos)
        {
            if (!play_on)
            {
                play_on = true;
                cout << endl;
                cout << "[info] referee play_on received. "
                     << "Now you can test dash/turn/kick/tackle." << endl;
                cout << endl;
            }
        }

        if (important)
        {
            cout << "[recv] " << msg << endl;
        }

        count++;
    }
}

static void wait_and_print_response(int sock,
                                    bool print_sense_body,
                                    bool &play_on)
{
    /*
        After sending a command, wait a short time and print only useful messages.
        This avoids terminal flooding.
    */

    for (int i = 0; i < 8; ++i)
    {
        if (wait_socket(sock, 100))
        {
            string msg;
            sockaddr_in from;

            if (!recv_msg(sock, msg, from))
            {
                continue;
            }

            bool important = false;

            if (verbose)
                important = true;

            if (starts_with(msg, "(error"))
                important = true;
            if (starts_with(msg, "(warning"))
                important = true;

            if (print_sense_body && starts_with(msg, "(sense_body"))
            {
                important = true;
            }

            if (msg.find("referee play_on") != string::npos)
            {
                if (!play_on)
                {
                    play_on = true;
                    cout << endl;
                    cout << "[info] referee play_on received. "
                         << "Now you can test dash/turn/kick/tackle." << endl;
                    cout << endl;
                }
            }

            if (important)
            {
                cout << "[recv] " << msg << endl;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    string team_name = "lab2";
    string server_ip = "127.0.0.1";
    int server_port = 6000;
    bool goalie = false;

    if (argc >= 2)
    {
        team_name = argv[1];
    }

    if (argc >= 3)
    {
        server_ip = argv[2];
    }

    for (int i = 3; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg == "goalie")
        {
            goalie = true;
        }
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 1;
    }

    sockaddr_in init_server_addr;
    memset(&init_server_addr, 0, sizeof(init_server_addr));
    init_server_addr.sin_family = AF_INET;
    init_server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &init_server_addr.sin_addr) <= 0)
    {
        cerr << "Invalid server ip: " << server_ip << endl;
        close(sock);
        return 1;
    }

    cout << "========================================" << endl;
    cout << " Lab2 RoboCup Interactive Client" << endl;
    cout << " Team   : " << team_name << endl;
    cout << " Server : " << server_ip << ":" << server_port << endl;
    cout << " Goalie : " << (goalie ? "yes" : "no") << endl;
    cout << "========================================" << endl;

    string init_cmd;

    if (goalie)
    {
        init_cmd = "(init " + team_name + " (version 15) (goalie))";
    }
    else
    {
        init_cmd = "(init " + team_name + " (version 15))";
    }

    send_msg(sock, init_server_addr, init_cmd);

    sockaddr_in action_server_addr;
    memset(&action_server_addr, 0, sizeof(action_server_addr));

    bool initialized = false;

    while (!initialized)
    {
        if (!wait_socket(sock, 5000))
        {
            cerr << "Timeout waiting init response." << endl;
            close(sock);
            return 1;
        }

        string msg;
        sockaddr_in from;

        if (!recv_msg(sock, msg, from))
        {
            continue;
        }

        /*
            Important:
            Only accept real init message.
            Do not use msg.find("(init"), because server_param contains effort_init.
        */
        if (starts_with(msg, "(init"))
        {
            initialized = true;
            action_server_addr = from;

            cout << "[recv] " << msg << endl;
            cout << "Initialized OK." << endl;
            cout << "Action port: " << addr_to_string(action_server_addr) << endl;
        }
    }

    /*
        Move player to center area before kickoff.
    */

    if (goalie)
    {
        send_msg(sock, action_server_addr, "(move -50 0)");
    }
    else
    {
        send_msg(sock, action_server_addr, "(move -0.5 0)");
    }

    bool play_on = false;

    cout << endl;
    cout << "Player moved to initial position." << endl;
    cout << "If game has not started, click Kick Off in monitor." << endl;
    cout << "Type help to show command list." << endl;
    cout << endl;

    print_help(goalie);

    /*
        Clean old messages silently.
    */
    drain_messages(sock, false, play_on);

    while (true)
    {
        /*
            Drain background messages silently before input.
            This prevents socket buffer from accumulating too much data.
            It also avoids flooding the terminal.
        */
        drain_messages(sock, false, play_on);

        cout << "cmd> ";
        cout.flush();

        string line;
        if (!getline(cin, line))
        {
            break;
        }

        if (line.empty())
        {
            continue;
        }

        if (line == "quit")
        {
            cout << "Quit." << endl;
            break;
        }

        if (line == "help")
        {
            print_help(goalie);
            continue;
        }

        if (line == "verbose on")
        {
            verbose = true;
            cout << "verbose = on" << endl;
            continue;
        }

        if (line == "verbose off")
        {
            verbose = false;
            cout << "verbose = off" << endl;
            continue;
        }

        if (line == "center")
        {
            send_msg(sock, action_server_addr, "(move -0.5 0)");
            wait_and_print_response(sock, false, play_on);
            continue;
        }

        if (line == "center0")
        {
            send_msg(sock, action_server_addr, "(move 0 0)");
            wait_and_print_response(sock, false, play_on);
            continue;
        }

        if (line == "goaliepos")
        {
            send_msg(sock, action_server_addr, "(move -50 0)");
            wait_and_print_response(sock, false, play_on);
            continue;
        }

        /*
            User must input full command, such as:
            (dash 100)
            not:
            dash 100
        */
        if (line[0] != '(')
        {
            cout << "[error] Please input FULL server command with parentheses." << endl;
            cout << "Example: (dash 100), (turn 90), (kick 100 0)" << endl;
            continue;
        }

        bool print_sense_body = false;

        if (starts_with(line, "(sense_body"))
        {
            print_sense_body = true;
        }

        if (!play_on)
        {
            cout << "[notice] Game may not be play_on. "
                 << "If action has no effect, click Kick Off in monitor." << endl;
        }

        send_msg(sock, action_server_addr, line);
        wait_and_print_response(sock, print_sense_body, play_on);
    }

    close(sock);
    return 0;
}
