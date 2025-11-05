#ifndef NETWORKSTREAMLOADER_H
#define NETWORKSTREAMLOADER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QTimer>
#include <QTime>
#include <QString>

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVCodecContext;
struct AVDictionary;

class NetworkStreamLoader : public QObject
{
    Q_OBJECT

public:
    enum LoadingStatus {
        Idle,
        Connecting,
        LoadingStreamInfo,
        Ready,
        Failed,
        Timeout,
        Cancelled
    };

    struct StreamInfo {
        QString url;
        int videoStreamIndex;
        int audioStreamIndex;
        AVCodecContext* videoCodecContext;
        AVCodecContext* audioCodecContext;
        AVFormatContext* formatContext;
        int64_t duration;
        double fps;
        int width;
        int height;
    };

    explicit NetworkStreamLoader(QObject *parent = nullptr);
    ~NetworkStreamLoader();

    // 主要接口
    void loadStreamAsync(const QString &url, int timeoutMs = 15000);
    void cancelLoading();
    bool isLoading() const;
    LoadingStatus getStatus() const;
    QString getStatusText() const;

signals:
    void loadingStarted();
    void loadingProgress(int percentage, const QString &message);
    void streamReady(const NetworkStreamLoader::StreamInfo &streamInfo);
    void loadingFailed(const QString &error);
    void loadingCancelled();
    void statusChanged(LoadingStatus status);
    void doAsyncWork();  // 内部信号，用于启动工作线程中的工作

public slots:
    void startLoading();

private slots:
    void onTimeoutTimer();
    void onProgressTimer();

private:
    void performAsyncLoading();  // 实际的异步加载工作

private:
    void setStatus(LoadingStatus status);
    void cleanup();
    bool setupNetworkOptions(AVDictionary **options);
    bool openInputStream();
    bool findStreamInfo();
    bool setupCodecs();
    void emitProgress(int percentage, const QString &message);

    // 状态管理
    LoadingStatus m_status;
    mutable QMutex m_statusMutex;
    
    // 网络加载参数
    QString m_url;
    int m_timeoutMs;
    QTime m_startTime;
    
    // FFmpeg 上下文
    AVFormatContext* m_formatContext;
    AVCodecContext* m_videoCodecContext;
    AVCodecContext* m_audioCodecContext;
    int m_videoStreamIndex;
    int m_audioStreamIndex;
    
    // 定时器
    QTimer* m_timeoutTimer;
    QTimer* m_progressTimer;
    
    // 线程安全
    QThread* m_workerThread;
    bool m_shouldCancel;
    mutable QMutex m_cancelMutex;
};

#endif // NETWORKSTREAMLOADER_H 