#include "StreamProtocolHandler.h"
#include "NetworkConfig.h"
#include <QNetworkRequest>
#include <QNetworkReply>

// StreamProtocolHandler 基类实现
StreamProtocolHandler::StreamProtocolHandler(QObject *parent)
    : QObject(parent)
    , m_isConnected(false)
    , m_protocolType(UNKNOWN_PROTOCOL)
{
}

StreamProtocolHandler::ProtocolType StreamProtocolHandler::detectProtocol(const QString &url)
{
    QUrl qurl(url);
    QString scheme = qurl.scheme().toLower();
    
    if (scheme == "http") {
        return HTTP_PROTOCOL;
    } else if (scheme == "https") {
        return HTTPS_PROTOCOL;
    } else if (scheme == "rtmp") {
        return RTMP_PROTOCOL;
    } else if (scheme == "rtsp") {
        return RTSP_PROTOCOL;
    } else if (scheme == "udp") {
        return UDP_PROTOCOL;
    } else if (scheme == "tcp") {
        return TCP_PROTOCOL;
    }
    
    return UNKNOWN_PROTOCOL;
}

QString StreamProtocolHandler::protocolToString(ProtocolType protocol)
{
    switch (protocol) {
    case HTTP_PROTOCOL: return "HTTP";
    case HTTPS_PROTOCOL: return "HTTPS";
    case RTMP_PROTOCOL: return "RTMP";
    case RTSP_PROTOCOL: return "RTSP";
    case UDP_PROTOCOL: return "UDP";
    case TCP_PROTOCOL: return "TCP";
    default: return "Unknown";
    }
}

StreamProtocolHandler* StreamProtocolHandler::createHandler(ProtocolType protocol, QObject *parent)
{
    switch (protocol) {
    case HTTP_PROTOCOL:
    case HTTPS_PROTOCOL:
        return new HttpStreamHandler(parent);
    case RTMP_PROTOCOL:
        return new RtmpStreamHandler(parent);
    case RTSP_PROTOCOL:
        return new RtspStreamHandler(parent);
    case UDP_PROTOCOL:
    case TCP_PROTOCOL:
        // 可以在这里添加UDP/TCP处理器
        return nullptr;
    default:
        return nullptr;
    }
}

bool StreamProtocolHandler::validateUrl(const QString &url) const
{
    QUrl qurl(url);
    return qurl.isValid() && !qurl.scheme().isEmpty();
}

QString StreamProtocolHandler::normalizeUrl(const QString &url) const
{
    QUrl qurl(url);
    return qurl.toString();
}

int StreamProtocolHandler::getDefaultPort() const
{
    return 80; // 默认端口
}

void StreamProtocolHandler::setCommonOptions(AVDictionary** options, const NetworkConfig &config)
{
    if (!options) return;
    
    // 设置超时
    av_dict_set_int(options, "timeout", config.connectionTimeout * 1000, 0);
    
    // 设置用户代理
    if (!config.userAgent.isEmpty()) {
        av_dict_set(options, "user_agent", config.userAgent.toUtf8().constData(), 0);
    }
    
    // 设置引用页面
    if (!config.referer.isEmpty()) {
        av_dict_set(options, "referer", config.referer.toUtf8().constData(), 0);
    }
    
    // 设置重定向
    if (config.followRedirects) {
        av_dict_set_int(options, "followlocation", 1, 0);
        av_dict_set_int(options, "maxredirs", config.maxRedirects, 0);
    }
    
    // 设置缓冲区大小
    av_dict_set_int(options, "buffer_size", config.bufferSize, 0);
}

bool StreamProtocolHandler::isSecureProtocol() const
{
    return false; // 基类默认不安全
}

// HttpStreamHandler 实现
HttpStreamHandler::HttpStreamHandler(QObject *parent)
    : StreamProtocolHandler(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
{
    m_protocolType = HTTP_PROTOCOL;
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &HttpStreamHandler::handleNetworkReply);
}

HttpStreamHandler::~HttpStreamHandler()
{
    disconnectFromStream();
}

bool HttpStreamHandler::connectToStream(const QString &url, const NetworkConfig &config)
{
    if (!validateUrl(url)) {
        emit connectionError("Invalid URL");
        return false;
    }
    
    disconnectFromStream(); // 断开现有连接
    
    m_currentUrl = normalizeUrl(url);
    
    QNetworkRequest request(m_currentUrl);
    
    // 设置请求头
    if (!config.userAgent.isEmpty()) {
        request.setRawHeader("User-Agent", config.userAgent.toUtf8());
    }
    
    if (!config.referer.isEmpty()) {
        request.setRawHeader("Referer", config.referer.toUtf8());
    }
    
    // 发起连接
    m_currentReply = m_networkManager->get(request);
    
    if (!m_currentReply) {
        emit connectionError("Failed to create network request");
        return false;
    }
    
    return true;
}

void HttpStreamHandler::disconnectFromStream()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    
    m_isConnected = false;
    m_currentUrl.clear();
}

bool HttpStreamHandler::isConnected() const
{
    return m_isConnected && m_currentReply;
}

bool HttpStreamHandler::isSecureProtocol() const
{
    return m_protocolType == HTTPS_PROTOCOL;
}

void HttpStreamHandler::handleNetworkReply()
{
    if (!m_currentReply) {
        return;
    }
    
    if (m_currentReply->error() == QNetworkReply::NoError) {
        m_isConnected = true;
        emit connectionEstablished();
    } else {
        emit connectionError(m_currentReply->errorString());
    }
    
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

// RtmpStreamHandler 实现
RtmpStreamHandler::RtmpStreamHandler(QObject *parent)
    : StreamProtocolHandler(parent)
    , m_formatContext(nullptr)
{
    m_protocolType = RTMP_PROTOCOL;
}

RtmpStreamHandler::~RtmpStreamHandler()
{
    disconnectFromStream();
}

bool RtmpStreamHandler::connectToStream(const QString &url, const NetworkConfig &config)
{
    if (!validateUrl(url)) {
        emit connectionError("Invalid RTMP URL");
        return false;
    }
    
    disconnectFromStream();
    
    m_currentUrl = normalizeUrl(url);
    
    // 分配格式上下文
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        emit connectionError("Failed to allocate format context");
        return false;
    }
    
    // 设置选项
    AVDictionary* options = nullptr;
    setCommonOptions(&options, config);
    
    // 尝试打开RTMP流
    int ret = avformat_open_input(&m_formatContext, m_currentUrl.toUtf8().constData(), nullptr, &options);
    
    av_dict_free(&options);
    
    if (ret < 0) {
        emit connectionError("Failed to open RTMP stream");
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
        return false;
    }
    
    m_isConnected = true;
    emit connectionEstablished();
    return true;
}

void RtmpStreamHandler::disconnectFromStream()
{
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }
    
    m_isConnected = false;
    m_currentUrl.clear();
}

bool RtmpStreamHandler::isConnected() const
{
    return m_isConnected && m_formatContext;
}

int RtmpStreamHandler::getDefaultPort() const
{
    return 1935; // RTMP默认端口
}

// RtspStreamHandler 实现
RtspStreamHandler::RtspStreamHandler(QObject *parent)
    : StreamProtocolHandler(parent)
    , m_formatContext(nullptr)
{
    m_protocolType = RTSP_PROTOCOL;
}

RtspStreamHandler::~RtspStreamHandler()
{
    disconnectFromStream();
}

bool RtspStreamHandler::connectToStream(const QString &url, const NetworkConfig &config)
{
    if (!validateUrl(url)) {
        emit connectionError("Invalid RTSP URL");
        return false;
    }
    
    disconnectFromStream();
    
    m_currentUrl = normalizeUrl(url);
    
    // 分配格式上下文
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        emit connectionError("Failed to allocate format context");
        return false;
    }
    
    // 设置选项
    AVDictionary* options = nullptr;
    setCommonOptions(&options, config);
    
    // 尝试打开RTSP流
    int ret = avformat_open_input(&m_formatContext, m_currentUrl.toUtf8().constData(), nullptr, &options);
    
    av_dict_free(&options);
    
    if (ret < 0) {
        emit connectionError("Failed to open RTSP stream");
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
        return false;
    }
    
    m_isConnected = true;
    emit connectionEstablished();
    return true;
}

void RtspStreamHandler::disconnectFromStream()
{
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }
    
    m_isConnected = false;
    m_currentUrl.clear();
}

bool RtspStreamHandler::isConnected() const
{
    return m_isConnected && m_formatContext;
}

int RtspStreamHandler::getDefaultPort() const
{
    return 554; // RTSP默认端口
} 