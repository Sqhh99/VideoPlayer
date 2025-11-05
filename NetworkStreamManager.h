#ifndef NETWORKSTREAMMANAGER_H
#define NETWORKSTREAMMANAGER_H

#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>

extern "C" {
    #include <libavformat/avformat.h>
}

class NetworkConfig;
class StreamProtocolHandler;

class NetworkStreamManager : public QObject
{
    Q_OBJECT

public:
    enum StreamStatus {
        Disconnected,
        Connecting,
        Connected,
        Buffering,
        Error
    };

    enum StreamProtocol {
        Unknown,
        HTTP,
        HTTPS,
        RTMP,
        RTSP,
        UDP,
        TCP
    };

    explicit NetworkStreamManager(QObject *parent = nullptr);
    ~NetworkStreamManager();

    // 连接管理
    bool connectToStream(const QString &url);
    void disconnectStream();
    void reconnect();

    // 状态查询
    bool isConnected() const;
    StreamStatus getStatus() const;
    QString getStatusText() const;
    StreamProtocol getProtocol() const;

    // 配置管理
    void setNetworkConfig(const NetworkConfig &config);
    NetworkConfig getNetworkConfig() const;

    // 流信息
    QString getCurrentUrl() const;
    qint64 getBufferSize() const;
    int getConnectionLatency() const;
    AVFormatContext* getFormatContext() const;

signals:
    void streamConnected();
    void streamDisconnected();
    void streamError(const QString &error);
    void statusChanged();
    void bufferStatusChanged(int percentage);
    void connectionProgress(int percentage);

private slots:
    void handleConnectionTimeout();
    void updateConnectionStatus();
    void handleNetworkReply();

private:
    // 协议检测
    StreamProtocol detectProtocol(const QString &url);
    
    // FFmpeg 上下文管理
    bool setupFFmpegContext();
    void cleanupFFmpegContext();
    
    // 连接管理
    void resetConnection();
    void updateLatency();

    // 成员变量
    StreamStatus m_status;
    StreamProtocol m_protocol;
    QString m_currentUrl;
    AVFormatContext* m_formatContext;
    
    NetworkConfig* m_config;
    StreamProtocolHandler* m_protocolHandler;
    
    QTimer* m_connectionTimer;
    QTimer* m_statusTimer;
    QNetworkAccessManager* m_networkManager;
    QNetworkReply* m_currentReply;
    
    qint64 m_bufferSize;
    int m_connectionLatency;
};

#endif // NETWORKSTREAMMANAGER_H 