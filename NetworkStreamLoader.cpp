#include "NetworkStreamLoader.h"
#include <QDebug>
#include <QApplication>
#include <QMutexLocker>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/error.h>
}

NetworkStreamLoader::NetworkStreamLoader(QObject *parent)
    : QObject(parent)
    , m_status(Idle)
    , m_timeoutMs(15000)
    , m_formatContext(nullptr)
    , m_videoCodecContext(nullptr)
    , m_audioCodecContext(nullptr)
    , m_videoStreamIndex(-1)
    , m_audioStreamIndex(-1)
    , m_timeoutTimer(new QTimer(this))
    , m_progressTimer(new QTimer(this))
    , m_workerThread(nullptr)
    , m_shouldCancel(false)
{
    // 设置超时定时器
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &NetworkStreamLoader::onTimeoutTimer);
    
    // 设置进度定时器
    m_progressTimer->setInterval(500);  // 每500ms更新一次进度
    connect(m_progressTimer, &QTimer::timeout, this, &NetworkStreamLoader::onProgressTimer);
}

NetworkStreamLoader::~NetworkStreamLoader()
{
    cancelLoading();
    cleanup();
}

void NetworkStreamLoader::loadStreamAsync(const QString &url, int timeoutMs)
{
    if (isLoading()) {
        qDebug() << "NetworkStreamLoader: Already loading, cancelling previous operation";
        cancelLoading();
    }
    
    m_url = url;
    m_timeoutMs = timeoutMs;
    m_shouldCancel = false;
    
    // Starting async load
    
    // 清理之前的资源
    cleanup();
    
    // 创建工作线程，但不移动this对象
    m_workerThread = new QThread();
    
    // 连接信号 - 使用队列连接确保线程安全
    connect(m_workerThread, &QThread::started, this, &NetworkStreamLoader::startLoading, Qt::QueuedConnection);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QThread::deleteLater);
    
    // 设置状态并启动
    setStatus(Connecting);
    m_startTime = QTime::currentTime();
    
    // 启动定时器（在主线程中）
    m_timeoutTimer->start(m_timeoutMs);
    m_progressTimer->start();
    
    // 启动线程
    m_workerThread->start();
    
    emit loadingStarted();
}

void NetworkStreamLoader::cancelLoading()
{
    QMutexLocker locker(&m_cancelMutex);
    m_shouldCancel = true;
    
    // 停止定时器
    m_timeoutTimer->stop();
    m_progressTimer->stop();
    
    if (m_workerThread && m_workerThread->isRunning()) {
        qDebug() << "NetworkStreamLoader: Cancelling loading operation";
        
        // 等待线程完成
        m_workerThread->quit();
        if (!m_workerThread->wait(3000)) {
            qDebug() << "NetworkStreamLoader: Force terminating worker thread";
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
        
        m_workerThread = nullptr;
        setStatus(Cancelled);
        emit loadingCancelled();
    }
}

bool NetworkStreamLoader::isLoading() const
{
    QMutexLocker locker(&m_statusMutex);
    return m_status == Connecting || m_status == LoadingStreamInfo;
}

NetworkStreamLoader::LoadingStatus NetworkStreamLoader::getStatus() const
{
    QMutexLocker locker(&m_statusMutex);
    return m_status;
}

QString NetworkStreamLoader::getStatusText() const
{
    LoadingStatus status = getStatus();
    switch (status) {
        case Idle: return "等待";
        case Connecting: return "连接中";
        case LoadingStreamInfo: return "加载流信息";
        case Ready: return "就绪";
        case Failed: return "失败";
        case Timeout: return "超时";
        case Cancelled: return "已取消";
        default: return "未知";
    }
}

void NetworkStreamLoader::startLoading()
{
    // Starting loading in worker thread
    
    // 创建一个在工作线程中运行的QObject
    auto worker = new QObject();
    worker->moveToThread(m_workerThread);
    
    // 连接信号，在工作线程中执行实际的加载工作
    connect(this, &NetworkStreamLoader::doAsyncWork, worker, [this, worker]() {
        performAsyncLoading();
        worker->deleteLater();
    }, Qt::QueuedConnection);
    
    // 发送信号开始工作
    emit doAsyncWork();
}

void NetworkStreamLoader::performAsyncLoading()
{
    qDebug() << "NetworkStreamLoader: Performing async loading in worker thread";
    
    try {
        // 检查取消状态
        {
            QMutexLocker locker(&m_cancelMutex);
            if (m_shouldCancel) {
                setStatus(Cancelled);
                emit loadingCancelled();
                return;
            }
        }
        
        emitProgress(10, "正在建立连接...");
        
        // 第一步：打开输入流
        if (!openInputStream()) {
            // 使用QMetaObject::invokeMethod在主线程中停止定时器
            QMetaObject::invokeMethod(this, [this]() {
                m_timeoutTimer->stop();
                m_progressTimer->stop();
            }, Qt::QueuedConnection);
            return;  // 错误已经在函数内部处理
        }
        
        // 检查取消状态
        {
            QMutexLocker locker(&m_cancelMutex);
            if (m_shouldCancel) {
                setStatus(Cancelled);
                emit loadingCancelled();
                return;
            }
        }
        
        emitProgress(40, "正在获取流信息...");
        setStatus(LoadingStreamInfo);
        
        // 第二步：查找流信息
        if (!findStreamInfo()) {
            // 使用QMetaObject::invokeMethod在主线程中停止定时器
            QMetaObject::invokeMethod(this, [this]() {
                m_timeoutTimer->stop();
                m_progressTimer->stop();
            }, Qt::QueuedConnection);
            return;  // 错误已经在函数内部处理
        }
        
        // 检查取消状态
        {
            QMutexLocker locker(&m_cancelMutex);
            if (m_shouldCancel) {
                setStatus(Cancelled);
                emit loadingCancelled();
                return;
            }
        }
        
        emitProgress(70, "正在设置解码器...");
        
        // 第三步：设置解码器
        if (!setupCodecs()) {
            // 使用QMetaObject::invokeMethod在主线程中停止定时器
            QMetaObject::invokeMethod(this, [this]() {
                m_timeoutTimer->stop();
                m_progressTimer->stop();
            }, Qt::QueuedConnection);
            return;  // 错误已经在函数内部处理
        }
        
        emitProgress(100, "连接成功");
        
        // 创建流信息结构
        StreamInfo streamInfo;
        streamInfo.url = m_url;
        streamInfo.videoStreamIndex = m_videoStreamIndex;
        streamInfo.audioStreamIndex = m_audioStreamIndex;
        streamInfo.videoCodecContext = m_videoCodecContext;
        streamInfo.audioCodecContext = m_audioCodecContext;
        streamInfo.formatContext = m_formatContext;
        streamInfo.duration = m_formatContext->duration;
        streamInfo.width = m_videoCodecContext ? m_videoCodecContext->width : 0;
        streamInfo.height = m_videoCodecContext ? m_videoCodecContext->height : 0;
        
        if (m_videoStreamIndex >= 0) {
            AVStream *videoStream = m_formatContext->streams[m_videoStreamIndex];
            streamInfo.fps = av_q2d(videoStream->r_frame_rate);
        } else {
            streamInfo.fps = 0.0;
        }
        
        // 停止所有定时器
        QMetaObject::invokeMethod(this, [this]() {
            m_timeoutTimer->stop();
            m_progressTimer->stop();
        }, Qt::QueuedConnection);
        
        // 设置成功状态
        setStatus(Ready);
        
        // 发送成功信号
        emit streamReady(streamInfo);
        
        qDebug() << "NetworkStreamLoader: Stream loaded successfully";
        return;  // 明确返回，避免后续错误处理
        
    } catch (const std::exception &e) {
        QString error = QString("加载过程中发生异常: %1").arg(e.what());
        qDebug() << "NetworkStreamLoader: Exception:" << error;
        setStatus(Failed);
        emit loadingFailed(error);
    } catch (...) {
        QString error = "加载过程中发生未知异常";
        qDebug() << "NetworkStreamLoader: Unknown exception";
        setStatus(Failed);
        emit loadingFailed(error);
    }
}

void NetworkStreamLoader::onTimeoutTimer()
{
    qDebug() << "NetworkStreamLoader: Timeout occurred";
    
    {
        QMutexLocker locker(&m_cancelMutex);
        m_shouldCancel = true;
    }
    
    setStatus(Timeout);
    emit loadingFailed("连接超时");
    
    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
    }
}

void NetworkStreamLoader::onProgressTimer()
{
    if (!isLoading()) {
        m_progressTimer->stop();
        return;
    }
    
    // 基于时间的简单进度计算
    int elapsed = m_startTime.msecsTo(QTime::currentTime());
    int expectedTotal = m_timeoutMs * 0.8;  // 假设80%的超时时间内完成
    int progress = qMin(90, (elapsed * 90) / expectedTotal);  // 最多到90%，剩下的由实际操作控制
    
    QString message;
    LoadingStatus status = getStatus();
    switch (status) {
        case Connecting:
            message = "正在连接网络流...";
            break;
        case LoadingStreamInfo:
            message = "正在分析视频信息...";
            break;
        default:
            message = "正在加载...";
            break;
    }
    
    emitProgress(progress, message);
}

void NetworkStreamLoader::setStatus(LoadingStatus status)
{
    bool shouldEmit = false;
    {
        QMutexLocker locker(&m_statusMutex);
        if (m_status != status) {
            m_status = status;
            shouldEmit = true;
        }
    }
    
    if (shouldEmit) {
        emit statusChanged(status);
    }
}

void NetworkStreamLoader::cleanup()
{
    // 注意：这些指针的所有权将转移给调用者，这里不释放
    // 只重置本地指针
    m_formatContext = nullptr;
    m_videoCodecContext = nullptr;
    m_audioCodecContext = nullptr;
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
}

bool NetworkStreamLoader::setupNetworkOptions(AVDictionary **options)
{
    av_dict_set(options, "timeout", "10000000", 0);      // 10秒超时
    av_dict_set(options, "buffer_size", "1024000", 0);   // 1MB缓冲
    av_dict_set(options, "max_delay", "5000000", 0);     // 5秒最大延迟
    av_dict_set(options, "user_agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36", 0);
    av_dict_set(options, "reconnect", "1", 0);           // 启用重连
    av_dict_set(options, "reconnect_streamed", "1", 0);  // 流式重连
    av_dict_set(options, "reconnect_delay_max", "5", 0); // 最大重连延迟5秒
    
    return true;
}

bool NetworkStreamLoader::openInputStream()
{
    AVDictionary *options = nullptr;
    setupNetworkOptions(&options);
    
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        av_dict_free(&options);
        setStatus(Failed);
        emit loadingFailed("无法分配格式上下文");
        return false;
    }
    
    QByteArray urlBytes = m_url.toUtf8();
    int ret = avformat_open_input(&m_formatContext, urlBytes.constData(), nullptr, &options);
    av_dict_free(&options);
    
    if (ret != 0) {
        char error_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, error_buf, sizeof(error_buf));
        QString error = QString("无法打开网络流: %1").arg(error_buf);
        
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
        
        setStatus(Failed);
        emit loadingFailed(error);
        return false;
    }
    
    return true;
}

bool NetworkStreamLoader::findStreamInfo()
{
    if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
        setStatus(Failed);
        emit loadingFailed("无法获取流信息");
        return false;
    }
    
    // 查找视频流和音频流
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    
    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        if (m_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex == -1) {
            m_videoStreamIndex = i;
        } else if (m_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex == -1) {
            m_audioStreamIndex = i;
        }
    }
    
    if (m_videoStreamIndex == -1) {
        setStatus(Failed);
        emit loadingFailed("未找到视频流");
        return false;
    }
    
    return true;
}

bool NetworkStreamLoader::setupCodecs()
{
    // 设置视频解码器
    if (m_videoStreamIndex >= 0) {
        AVStream *videoStream = m_formatContext->streams[m_videoStreamIndex];
        const AVCodec *videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
        if (!videoCodec) {
            setStatus(Failed);
            emit loadingFailed("未找到视频解码器");
            return false;
        }
        
        m_videoCodecContext = avcodec_alloc_context3(videoCodec);
        if (avcodec_parameters_to_context(m_videoCodecContext, videoStream->codecpar) < 0) {
            setStatus(Failed);
            emit loadingFailed("无法设置视频解码器参数");
            return false;
        }
        
        if (avcodec_open2(m_videoCodecContext, videoCodec, nullptr) < 0) {
            setStatus(Failed);
            emit loadingFailed("无法打开视频解码器");
            return false;
        }
    }
    
    // 设置音频解码器（如果有音频流）
    if (m_audioStreamIndex >= 0) {
        AVStream *audioStream = m_formatContext->streams[m_audioStreamIndex];
        const AVCodec *audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);
        if (audioCodec) {
            m_audioCodecContext = avcodec_alloc_context3(audioCodec);
            if (avcodec_parameters_to_context(m_audioCodecContext, audioStream->codecpar) >= 0) {
                if (avcodec_open2(m_audioCodecContext, audioCodec, nullptr) < 0) {
                    qDebug() << "NetworkStreamLoader: Cannot open audio decoder, video only";
                    avcodec_free_context(&m_audioCodecContext);
                    m_audioCodecContext = nullptr;
                }
            } else {
                qDebug() << "NetworkStreamLoader: Cannot set audio decoder parameters, video only";
                avcodec_free_context(&m_audioCodecContext);
                m_audioCodecContext = nullptr;
            }
        } else {
            qDebug() << "NetworkStreamLoader: Audio decoder not found, video only";
        }
    }
    
    return true;
}

void NetworkStreamLoader::emitProgress(int percentage, const QString &message)
{
    // 直接发送信号，Qt会自动处理跨线程信号
    emit loadingProgress(percentage, message);
} 