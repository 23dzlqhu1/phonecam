// test_gateway_parser.cpp
// 编译: cl /EHsc /std:c++17 test_gateway_parser.cpp
// 或者用 Qt: qmake + make

#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <sstream>

struct GatewayInfo {
    std::string interfaceName;
    std::string gatewayIp;
};

// 模拟 C++ getAllGateways() 的解析逻辑
std::vector<GatewayInfo> parseIpconfig(const std::string& output) {
    std::vector<GatewayInfo> result;
    
    // IPv4 regex
    std::regex ipv4Re("(\\d+\\.\\d+\\.\\d+\\.\\d+)");
    
    std::string currentAdapter;
    bool inAdapterSection = false;
    bool lookingForNextLineGw = false;
    
    std::istringstream stream(output);
    std::string line;
    int lineNum = 0;
    
    while (std::getline(stream, line)) {
        lineNum++;
        
        // 去除 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // 跳过空行
        if (line.empty()) {
            continue;
        }
        
        // 检测 adapter header
        bool isAdapterHeader = false;
        if (line.find("adapter") != std::string::npos ||
            line.find("适配器") != std::string::npos) {
            size_t colonPos = line.rfind(':');
            if (colonPos > 0 && colonPos == line.size() - 1) {
                isAdapterHeader = true;
            }
        }
        
        if (isAdapterHeader) {
            size_t colonPos = line.rfind(':');
            currentAdapter = line.substr(0, colonPos);
            
            // 移除前缀
            std::vector<std::string> prefixes = {
                "Ethernet adapter ",
                "Wireless LAN adapter ",
                "Local Area Connection adapter ",
                "以太网适配器 ",
                "无线局域网适配器 "
            };
            for (const auto& prefix : prefixes) {
                if (currentAdapter.substr(0, prefix.size()) == prefix) {
                    currentAdapter = currentAdapter.substr(prefix.size());
                    break;
                }
            }
            
            inAdapterSection = true;
            lookingForNextLineGw = false;
            std::cout << "[Line " << lineNum << "] ADAPTER: " << currentAdapter << std::endl;
            continue;
        }
        
        // 不在 adapter section 中则跳过
        if (!inAdapterSection) {
            continue;
        }
        
        // 处理续行（IPv6 后面的 IPv4）
        if (lookingForNextLineGw) {
            lookingForNextLineGw = false;
            if (!line.empty() && line[0] == ' ') {
                std::smatch match;
                if (std::regex_search(line, match, ipv4Re)) {
                    GatewayInfo info;
                    info.interfaceName = currentAdapter;
                    info.gatewayIp = match[1];
                    result.push_back(info);
                    std::cout << "[Line " << lineNum << "]   CONTINUATION GATEWAY: " 
                              << info.gatewayIp << " (adapter: " << info.interfaceName << ")" << std::endl;
                } else {
                    std::cout << "[Line " << lineNum << "]   CONTINUATION (no IPv4): " << line << std::endl;
                }
            } else {
                std::cout << "[Line " << lineNum << "]   CONTINUATION (not indented): " << line << std::endl;
            }
        }
        
        // 检测 Default Gateway 行
        if (line.find("Default Gateway") != std::string::npos ||
            line.find("默认网关") != std::string::npos) {
            std::cout << "[Line " << lineNum << "]   DEFAULT GATEWAY LINE: " << line << std::endl;
            
            size_t colonPos = line.rfind(':');
            if (colonPos != std::string::npos && colonPos + 1 < line.size()) {
                std::string valuePart = line.substr(colonPos + 1);
                
                // 去除前导空格
                size_t firstNonSpace = valuePart.find_first_not_of(' ');
                if (firstNonSpace != std::string::npos) {
                    valuePart = valuePart.substr(firstNonSpace);
                }
                
                std::smatch match;
                if (std::regex_search(valuePart, match, ipv4Re)) {
                    GatewayInfo info;
                    info.interfaceName = currentAdapter;
                    info.gatewayIp = match[1];
                    result.push_back(info);
                    std::cout << "[Line " << lineNum << "]   GATEWAY FOUND: " 
                              << info.gatewayIp << " (adapter: " << info.interfaceName << ")" << std::endl;
                } else {
                    lookingForNextLineGw = true;
                    std::cout << "[Line " << lineNum << "]   NO IPv4 on this line, looking for next..." << std::endl;
                }
            } else {
                lookingForNextLineGw = true;
                std::cout << "[Line " << lineNum << "]   No value after colon, looking for next..." << std::endl;
            }
        }
    }
    
    return result;
}

int main() {
    // 模拟 ipconfig 输出（带 \r\n 换行符）
    std::string ipconfigOutput = 
        "\r\n"
        "Windows IP 配置\r\n"
        "\r\n"
        "\r\n"
        "以太网适配器 以太网:\r\n"
        "\r\n"
        "   连接特定的 DNS 后缀 . . . . . . . : stu.edu.cn\r\n"
        "   本地链接 IPv6 地址. . . . . . . . : fe80::a8d9:7ae4:a984:c9a2%6\r\n"
        "   IPv4 地址 . . . . . . . . . . . . : 10.60.37.1\r\n"
        "   子网掩码  . . . . . . . . . . . . : 255.255.255.0\r\n"
        "   默认网关. . . . . . . . . . . . . : 10.60.37.254\r\n"
        "\r\n"
        "无线局域网适配器 本地连接* 8:\r\n"
        "\r\n"
        "   媒体状态  . . . . . . . . . . . . : 媒体已断开连接\r\n"
        "   连接特定的 DNS 后缀 . . . . . . . : \r\n"
        "\r\n"
        "无线局域网适配器 本地连接* 9:\r\n"
        "\r\n"
        "   媒体状态  . . . . . . . . . . . . : 媒体已断开连接\r\n"
        "   连接特定的 DNS 后缀 . . . . . . . : \r\n"
        "\r\n"
        "以太网适配器 以太网 4:\r\n"
        "\r\n"
        "   媒体状态  . . . . . . . . . . . . : 媒体已断开连接\r\n"
        "   连接特定的 DNS 后缀 . . . . . . . : \r\n"
        "\r\n"
        "无线局域网适配器 WLAN:\r\n"
        "\r\n"
        "   连接特定的 DNS 后缀 . . . . . . . : \r\n"
        "   IPv6 地址 . . . . . . . . . . . . : 2409:895a:41a6:8629:10b5:94d7:eb02:ebdb\r\n"
        "   临时 IPv6 地址. . . . . . . . . . : 2409:895a:41a6:8629:855e:292d:2b15:2d3d\r\n"
        "   本地链接 IPv6 地址. . . . . . . . : fe80::9852:a7b1:af46:e4e1%7\r\n"
        "   IPv4 地址 . . . . . . . . . . . . : 10.142.34.70\r\n"
        "   子网掩码  . . . . . . . . . . . . : 255.255.255.0\r\n"
        "   默认网关. . . . . . . . . . . . . : fe80::a474:f0ff:feeb:f140%7\r\n"
        "                                       10.142.34.164\r\n"
        "\r\n"
        "以太网适配器 蓝牙网络连接:\r\n"
        "\r\n"
        "   媒体状态  . . . . . . . . . . . . : 媒体已断开连接\r\n"
        "   连接特定的 DNS 后缀 . . . . . . . : \r\n"
        "\r\n";
    
    std::cout << "测试解析 ipconfig 输出..." << std::endl;
    std::cout << "========================================" << std::endl;
    
    auto gateways = parseIpconfig(ipconfigOutput);
    
    std::cout << "========================================" << std::endl;
    std::cout << "解析结果:" << std::endl;
    std::cout << "发现 " << gateways.size() << " 个网关:" << std::endl;
    for (const auto& gw : gateways) {
        std::cout << "  - " << gw.interfaceName << " -> " << gw.gatewayIp << std::endl;
    }
    
    return 0;
}
