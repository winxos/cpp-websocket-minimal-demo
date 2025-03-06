#define _CRT_SECURE_NO_WARNINGS

#include <uWebSockets/App.h>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include <mutex>
#include <set>
#include <chrono>

using namespace std;

struct PerSocketData {};

int main() {
    struct ConnectionManager {
        set<uWS::WebSocket<false, true, PerSocketData>*> connections;
        mutex connectionMutex;
    };

    ConnectionManager connManager;

    uWS::App app;

    // WebSocket配置
    app.ws<PerSocketData>("/*", {
        .open = [&connManager](auto* ws) {
            {
                lock_guard<mutex> lock(connManager.connectionMutex);
                connManager.connections.insert(ws);
            }
            cout << "WebSocket连接建立，当前连接数: " << connManager.connections.size() << endl;
        },
        .message = [](auto* ws, auto message, auto opCode) {
            // 处理消息（示例：打印并回传）
            std::cout << "Received: " << message << std::endl;
            ws->send(message, opCode);
        },
        .close = [&connManager](auto* ws, int code, string_view message) {
            {
                lock_guard<mutex> lock(connManager.connectionMutex);
                connManager.connections.erase(ws);
            }
            cout << "WebSocket连接关闭，剩余连接数: " << connManager.connections.size() << endl;
        }
        });

    // HTTP GET路由
    app.get("/status", [&connManager](auto* res, auto* req) {
        res->writeStatus("200 OK")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"status": "running", "clients": ")" + to_string(connManager.connections.size()) + "\"}");
        });

    app.get("/*", [&connManager](auto* res, auto* req) {
        res->writeStatus("200 OK")
            ->writeHeader("Content-Type", "text/html")
            ->end(R"(<head>GrainX CDY</head>)");
        });
    // 视频流推送线程
    thread([&connManager]() {
        cv::VideoCapture cap(0); // 使用摄像头，测试时可生成虚拟图像
        if (!cap.isOpened()) {
            cerr << "无法打开摄像头" << endl;
            return;
        }

        while (true) {
            cv::Mat frame;
            cap >> frame;
            if (frame.empty()) continue;
            cv::resize(frame, frame, cv::Size(300, 300));
            // 生成灰度图
            cv::Mat grayFrame;
            cv::cvtColor(frame, grayFrame, cv::COLOR_BGR2GRAY);

            // 编码为JPEG
            vector<uchar> colorBuf, grayBuf;
            cv::imencode(".jpg", frame, colorBuf, { cv::IMWRITE_JPEG_QUALITY, 80 });
            cv::imencode(".jpg", grayFrame, grayBuf, { cv::IMWRITE_JPEG_QUALITY, 80 });
            // 添加2字节标识头
            vector<uchar> colorPacket(2 + colorBuf.size());
            colorPacket[0] = 0x00; // 类型标识高位
            colorPacket[1] = 0x01; // 原始帧标识
            memcpy(&colorPacket[2], colorBuf.data(), colorBuf.size());

            vector<uchar> grayPacket(2 + grayBuf.size());
            grayPacket[0] = 0x00;
            grayPacket[1] = 0x02; // 灰度帧标识
            memcpy(&grayPacket[2], grayBuf.data(), grayBuf.size());

            // 发送二进制数据
            {
                lock_guard<mutex> lock(connManager.connectionMutex);
                for (auto ws : connManager.connections) {
                    ws->send(string_view((char*)colorPacket.data(), colorPacket.size()), uWS::BINARY);
                    ws->send(string_view((char*)grayPacket.data(), grayPacket.size()), uWS::BINARY);
                }
            }

            this_thread::sleep_for(25ms);
        }
        }).detach();

        // 启动服务器
        app.listen(9001, [](auto* listenSocket) {
            if (listenSocket) {
                cout << "服务器已启动，监听端口 9001" << endl;
                cout << "测试地址: http://localhost:9001" << endl;
            }
            });

        app.run();
}
