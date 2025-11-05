#include "NetworkStreamManager.h"
#include "NetworkConfig.h"
#include "StreamProtocolHandler.h"
#include <QDebug>
#include <QTime>
#include <QNetworkRequest>

NetworkStreamManager::NetworkStreamManager(QObject *parent)
    : QObject(parent)
    , m_status(Disconnected)
    , m_protocol(Unknown)
    , m_formatContext(nullptr)
    , m_config(new NetworkConfig(NetworkConfig::defaultConfig()))
    , m_protocolHandler(nullptr)
    , m_connectionTimer(new QTimer(this))
    , m_statusTimer(new QTimer(this))
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_bufferSize(0)
    , m_connectionLatency(0)
{
    // 设置连接超时定时器
    m_connectionTimer->setSingleShot(true);
    connect(m_connectionTimer, &QTimer::timeout, this, &NetworkStreamManager::handleConnectionTimeout);
    
    // 设置状态更新定时器
    m_statusTimer->setInterval(1000); // 每秒更新一次状态
    connect(m_statusTimer, &QTimer::timeout, this, &NetworkStreamManager::updateConnectionStatus);
    
    // 网络管理器信号连接
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &NetworkStreamManager::handleNetworkReply);
}

NetworkStreamManager::~NetworkStreamManager()
{
    disconnectStream();
    delete m_config;
    if (m_protocolHandler) {
        delete m_protocolHandler;
    }
}

bool NetworkStreamManager::connectToStream(const QString &url)
{
    if (url.isEmpty()) {
        emit streamError("Empty URL provided");
        return false;
    }
    
    // 断开现有连接
    disconnectStream();
    
    m_currentUrl = url;
    m_status = Connecting;
    emit statusChanged();
    
    // 检测协议类型
    m_protocol = detectProtocol(url);
    
    // 创建协议处理器
    StreamProtocolHandler::ProtocolType protocolType =
        static_cast<StreamProtocolHandler::ProtocolType>(m_protocol);
    m_protocolHandler = StreamProtocolHandler::createHandler(protocolType, this);
    
    if (!m_protocolHandler) {
        emit streamError("Unsupported protocol");
        return false;
    }
    
    // 连接协议处理器信号
    connect(m_protocolHandler, &StreamProtocolHandler::connectionProgress,
            this, &NetworkStreamManager::connectionProgress);
    
    // 连接错误信号
    connect(m_protocolHandler, &StreamProtocolHandler::connectionError,
            this, &NetworkStreamManager::streamError);
    
    // 开始连接
    m_connectionTimer->start(m_config->connectionTimeout);
    m_statusTimer->start();
    
    // 这里可以添加实际的连接逻辑
    // 为了演示，我们模拟一个成功的连接
    QTimer::singleShot(1000, this, [this]() {
        m_status = Connected;
        emit streamConnected();
        emit statusChanged();
    });
    
    return true;
}

void NetworkStreamManager::disconnectStream()
{
    if (m_status == Disconnected) {
        return;
    }
    
    m_connectionTimer->stop();
    m_statusTimer->stop();
    
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    
    if (m_protocolHandler) {
        m_protocolHandler->disconnectFromStream();
        delete m_protocolHandler;
        m_protocolHandler = nullptr;
    }
    
    cleanupFFmpegContext();
    
    m_status = Disconnected;
    m_currentUrl.clear();
    m_bufferSize = 0;
    m_connectionLatency = 0;
    
    emit streamDisconnected();
    emit statusChanged();
}

void NetworkStreamManager::reconnect()
{
    if (!m_currentUrl.isEmpty()) {
        connectToStream(m_currentUrl);
    }
}

bool NetworkStreamManager::isConnected() const
{
    return m_status == Connected;
}

NetworkStreamManager::StreamStatus NetworkStreamManager::getStatus() const
{
    return m_status;
}

QString NetworkStreamManager::getStatusText() const
{
    switch (m_status) {
    case Disconnected: return "未连接";
    case Connecting: return "连接中...";
    case Connected: return "已连接";
    case Buffering: return "缓冲中...";
    case Error: return "连接错误";
    default: return "未知状态";
    }
}

NetworkStreamManager::StreamProtocol NetworkStreamManager::getProtocol() const
{
    return m_protocol;
}

void NetworkStreamManager::setNetworkConfig(const NetworkConfig &config)
{
    *m_config = config;
}

NetworkConfig NetworkStreamManager::getNetworkConfig() const
{
    return *m_config;
}

QString NetworkStreamManager::getCurrentUrl() const
{
    return m_currentUrl;
}

qint64 NetworkStreamManager::getBufferSize() const
{
    return m_bufferSize;
}

int NetworkStreamManager::getConnectionLatency() const
{
    return m_connectionLatency;
}

AVFormatContext* NetworkStreamManager::getFormatContext() const
{
    return m_formatContext;
}

void NetworkStreamManager::handleConnectionTimeout()
{
    m_status = Error;
    emit streamError("Connection timeout");
    emit statusChanged();
}

void NetworkStreamManager::updateConnectionStatus()
{
    // 更新连接状态和缓冲区信息
    if (m_status == Connected) {
        // 模拟缓冲区状态更新
        static int bufferLevel = 0;
        bufferLevel = (bufferLevel + 10) % 100;
        emit bufferStatusChanged(bufferLevel);
    }
}

void NetworkStreamManager::handleNetworkReply()
{
    if (!m_currentReply) {
        return;
    }
    
    if (m_currentReply->error() != QNetworkReply::NoError) {
        emit streamError(m_currentReply->errorString());
    }
    
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

NetworkStreamManager::StreamProtocol NetworkStreamManager::detectProtocol(const QString &url)
{
    StreamProtocolHandler::ProtocolType type = StreamProtocolHandler::detectProtocol(url);
    
    switch (type) {
    case StreamProtocolHandler::HTTP_PROTOCOL: return HTTP;
    case StreamProtocolHandler::HTTPS_PROTOCOL: return HTTPS;
    case StreamProtocolHandler::RTMP_PROTOCOL: return RTMP;
    case StreamProtocolHandler::RTSP_PROTOCOL: return RTSP;
    case StreamProtocolHandler::UDP_PROTOCOL: return UDP;
    case StreamProtocolHandler::TCP_PROTOCOL: return TCP;
    default: return Unknown;
    }
}

bool NetworkStreamManager::setupFFmpegContext()
{
    // FFmpeg 上下文设置
    m_formatContext = avformat_alloc_context();
    return m_formatContext != nullptr;
}

void NetworkStreamManager::cleanupFFmpegContext()
{
    if (m_formatContext) {
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
    }
}

void NetworkStreamManager::resetConnection()
{
    disconnectStream();
    // 可以在这里添加重置逻辑
}

void NetworkStreamManager::updateLatency()
{
    // 更新连接延迟
    static QTime lastUpdate = QTime::currentTime();
    QTime now = QTime::currentTime();
    m_connectionLatency = lastUpdate.msecsTo(now);
    lastUpdate = now;
} 