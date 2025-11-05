#ifndef NETWORKCONFIG_H
#define NETWORKCONFIG_H

#include <QString>

class NetworkConfig
{
public:
    NetworkConfig();
    NetworkConfig(const NetworkConfig &other);
    NetworkConfig& operator=(const NetworkConfig &other);

    // 连接配置
    int connectionTimeout;      // 连接超时时间(ms)
    int readTimeout;           // 读取超时时间(ms)
    int maxRetries;            // 最大重试次数
    int retryDelay;            // 重试延迟(ms)
    
    // 缓冲区配置
    int bufferSize;            // 缓冲区大小(bytes)
    int maxBufferSize;         // 最大缓冲区大小(bytes)
    int minBufferThreshold;    // 最小缓冲阈值(%)
    int maxBufferThreshold;    // 最大缓冲阈值(%)
    
    // 网络配置
    QString userAgent;         // 用户代理字符串
    QString referer;           // 引用页面
    bool followRedirects;      // 是否跟随重定向
    int maxRedirects;          // 最大重定向次数
    
    // 代理配置
    QString proxyHost;         // 代理主机
    int proxyPort;             // 代理端口
    QString proxyUser;         // 代理用户名
    QString proxyPassword;     // 代理密码
    
    // 质量控制
    bool enableQualityControl; // 启用质量控制
    int targetBitrate;         // 目标比特率(kbps)
    int maxLatency;            // 最大延迟(ms)
    
    // 默认配置
    static NetworkConfig defaultConfig();
    
    // 配置验证
    bool isValid() const;
    QString getErrorString() const;
    
    // 配置序列化
    QString toString() const;
    bool fromString(const QString &configString);
};

#endif // NETWORKCONFIG_H 