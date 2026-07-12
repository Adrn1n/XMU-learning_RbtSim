CLIENT="./client"
TEAM="lab2_2"
SERVER_IP="127.0.0.1"

clear

echo "请选择实验任务："
echo
echo "  1) 一个球员带球从后场到前场，距离球门小于20米时射门"
echo "  2) 两个球员互相传球2次后，其中一人带球射门"
echo
echo "  q) 退出"
echo

read -p "请输入选择 [1/2/q]: " choice

if [ "$choice" = "q" ] || [ "$choice" = "Q" ]; then
    echo "退出。"
    exit 0
fi

# 只检查编译后的产物，不检查 client.cpp
if [ ! -x "$CLIENT" ]; then
    echo "[error] 找不到可执行文件：$CLIENT"
    echo
    echo "请先手动编译生成 client，例如："
    echo "  g++ client.cpp -o client -std=c++98"
    echo
    echo "或者确认当前目录下存在可执行文件："
    echo "  ./client"
    echo
    exit 1
fi

echo
echo "Team      : $TEAM"
echo "Server IP : $SERVER_IP"
echo "Client    : $CLIENT"
echo

case "$choice" in
    1)
        echo "=========================================="
        echo "启动任务 1：单人带球射门"
        echo "=========================================="
        echo
        echo "说明："
        echo "  将启动 1 个球员。"
        echo "  球员会自动追球、带球推进。"
        echo "  当距离对方球门小于 20 米时自动射门。"
        echo
        echo "提示："
        echo "  请确认 rcssserver 和 rcssmonitor 已启动。"
        echo "  如果比赛未开始，请在 monitor 中点击 Kick Off。"
        echo
        
        "$CLIENT" one "$TEAM" "$SERVER_IP"
    ;;
    
    2)
        echo "=========================================="
        echo "启动任务 2：双人传球两次后射门"
        echo "=========================================="
        echo
        echo "说明："
        echo "  将自动启动 2 个 client 进程。"
        echo "  一个 client 进程代表一个球员。"
        echo
        echo "  第一个进程通常成为 1 号球员。"
        echo "  第二个进程通常成为 2 号球员。"
        echo
        echo "任务流程："
        echo "  1号追球并传给2号。"
        echo "  2号接球后传回1号。"
        echo "  1号带球推进。"
        echo "  1号距离球门小于20米时射门。"
        echo
        echo "提示："
        echo "  请确认 rcssserver 和 rcssmonitor 已启动。"
        echo "  如果比赛未开始，请在 monitor 中点击 Kick Off。"
        echo
        
        "$CLIENT" two "$TEAM" "$SERVER_IP" &
        PID1=$!
        
        sleep 1
        
        "$CLIENT" two "$TEAM" "$SERVER_IP" &
        PID2=$!
        
        echo
        echo "两个球员进程已启动。"
        echo "PID1 = $PID1"
        echo "PID2 = $PID2"
        echo
        echo "按 Ctrl+C 可以结束两个球员。"
        echo
        
        trap "echo; echo 正在结束球员进程...; kill $PID1 $PID2 2>/dev/null; exit 0" INT TERM
        
        wait
    ;;
    
    *)
        echo "[error] 无效选择：$choice"
        exit 1
    ;;
esac
