#ifndef STREAMPROTOCOLHANDLER_H
#define STREAMPROTOCOLHANDLER_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QNetworkAccessManager>

extern "C" {
    #include <libavformat/avformat.h>
}

class NetworkConfig;

// 基础协议处理器
class StreamProtocolHandler : public QObject
{
    Q_OBJECT

public:
    enum ProtocolType {
        UNKNOWN_PROTOCOL = 0,
        HTTP_PROTOCOL,
        HTTPS_PROTOCOL,
        RTMP_PROTOCOL,
        RTSP_PROTOCOL,
        UDP_PROTOCOL,
        TCP_PROTOCOL
    };

    explicit StreamProtocolHandler(QObject *parent = nullptr);
    virtual ~StreamProtocolHandler() = default;

    // 协议检测
    static ProtocolType detectProtocol(const QString &url);
    static QString protocolToString(ProtocolType protocol);

    // 工厂方法
    static StreamProtocolHandler* createHandler(ProtocolType protocol, QObject *parent = nullptr);

    // 纯虚函数 - 子类必须实现
    virtual bool connectToStream(const QString &url, const NetworkConfig &config) = 0;
    virtual void disconnectFromStream() = 0;
    virtual bool isConnected() const = 0;

    // 通用方法
    virtual bool validateUrl(const QString &url) const;
    virtual QString normalizeUrl(const QString &url) const;
    virtual int getDefaultPort() const;
    virtual void setCommonOptions(AVDictionary** options, const NetworkConfig &config);
    virtual bool isSecureProtocol() const;

signals:
    void connectionProgress(int percentage);
    void connectionError(const QString &error);
    void connectionEstablished();
    void connectionLost();

protected:
    QString m_currentUrl;
    bool m_isConnected;
    ProtocolType m_protocolType;
};

// HTTP/HTTPS 协议处理器
class HttpStreamHandler : public StreamProtocolHandler
{
    Q_OBJECT

public:
    explicit HttpStreamHandler(QObject *parent = nullptr);
    ~HttpStreamHandler() override;

    bool connectToStream(const QString &url, const NetworkConfig &config) override;
    void disconnectFromStream() override;
    bool isConnected() const override;
    bool isSecureProtocol() const override;

private slots:
    void handleNetworkReply();

private:
    QNetworkAccessManager* m_networkManager;
    QNetworkReply* m_currentReply;
};

// RTMP 协议处理器
class RtmpStreamHandler : public StreamProtocolHandler
{
    Q_OBJECT

public:
    explicit RtmpStreamHandler(QObject *parent = nullptr);
    ~RtmpStreamHandler() override;

    bool connectToStream(const QString &url, const NetworkConfig &config) override;
    void disconnectFromStream() override;
    bool isConnected() const override;
    int getDefaultPort() const override;

private:
    AVFormatContext* m_formatContext;
};

// RTSP 协议处理器
class RtspStreamHandler : public StreamProtocolHandler
{
    Q_OBJECT

public:
    explicit RtspStreamHandler(QObject *parent = nullptr);
    ~RtspStreamHandler() override;

    bool connectToStream(const QString &url, const NetworkConfig &config) override;
    void disconnectFromStream() override;
    bool isConnected() const override;
    int getDefaultPort() const override;

private:
    AVFormatContext* m_formatContext;
};

#endif // STREAMPROTOCOLHANDLER_H 