#ifndef AUDIOPROCESSOR_H
#define AUDIOPROCESSOR_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QTimer>
#include <QTime>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QDebug>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QElapsedTimer> // Added for QElapsedTimer

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswresample/swresample.h>
    #include <libavutil/time.h>
}

// 音频包结构
struct AudioPacket {
    uint8_t* data;
    int size;
    int64_t pts;  // 显示时间戳
    int duration; // 包持续时间(ms)
    
    AudioPacket() : data(nullptr), size(0), pts(AV_NOPTS_VALUE), duration(0) {}
    ~AudioPacket() { 
        if (data) {
            av_free(data);
            data = nullptr;
        }
    }
};

class AudioProcessor : public QObject
{
    Q_OBJECT

public:
    explicit AudioProcessor(QObject *parent = nullptr);
    ~AudioProcessor();

    // 初始化和清理
    bool initialize(AVCodecContext* audioCodecContext);
    void cleanup();
    
    // 播放控制
    void start();
    void pause();
    void resume();  // 新增：从暂停状态恢复
    void stop();
    void seek(int64_t timestamp);
    
    // 音频数据处理
    void processAudioPacket(AVPacket* packet);
    
    // 音量控制
    void setVolume(float volume);
    float getVolume() const { return m_volume; }
    
    // 同步控制
    int64_t getCurrentAudioTime() const;
    void setMasterClock(int64_t timestamp);
    bool isPlaying() const { return m_isPlaying; }
    
    // 新增：更精确的时间计算
    int64_t getAccurateAudioTime() const;
    int64_t getAudioDeviceLatency() const;
    void updateAudioClock(int64_t pts, int sampleCount);
    void setAudioStreamInfo(AVStream* audioStream);  // 新增：设置音频流信息
    
    // 状态查询
    bool isInitialized() const { return m_initialized; }
    QString getStatusInfo() const;

signals:
    void audioTimeChanged(int64_t timestamp);
    void bufferStatusChanged(int bufferLevel, int maxBuffer);
    void audioError(const QString& error);

public slots:
    void processAudioQueue();

private slots:
    void checkBufferStatus();
    void handleAudioStateChanged();

private:
    // 音频设备管理
    bool setupAudioDevice();
    void cleanupAudioDevice();
    bool restartAudioDevice();
    
    // 重采样管理
    bool setupResampler();
    void cleanupResampler();
    int resampleAudioFrame(AVFrame* frame, uint8_t** outputBuffer);
    
    // 缓冲区管理
    void manageDynamicBuffer();
    void clearAudioQueue();
    int getOptimalBufferSize() const;
    
    // 同步控制
    void adjustPlaybackTiming();
    int64_t calculateAudioDelay() const;
    bool shouldDropFrame(int64_t framePts) const;
    void adjustDeviceLatency();  // 新增：动态调整设备延迟
    
    // 错误处理
    void handleAudioDeviceError();
    void attemptRecovery();

private:
    // 音频上下文
    AVCodecContext* m_audioCodecContext;
    SwrContext* m_swrContext;
    AVFrame* m_audioFrame;
    AVStream* m_audioStream;  // 新增：音频流信息
    
    // Qt音频设备
    QAudioFormat m_audioFormat;
    QAudioSink* m_audioSink;
    QIODevice* m_audioDevice;
    QAudioDevice m_outputDevice;
    
    // 音频缓冲队列
    QQueue<AudioPacket*> m_audioQueue;
    mutable QMutex m_queueMutex;
    QWaitCondition m_bufferCondition;
    
    // 播放状态
    bool m_initialized;
    bool m_isPlaying;
    bool m_isPaused;
    bool m_isSeeking;
    float m_volume;
    
    // 同步控制
    int64_t m_masterClock;
    int64_t m_audioBasePts;
    QTime m_playbackStartTime;
    QTime m_audioStartTime;
    
    // 新增：精确时间同步
    int64_t m_lastAudioPts;          // 最后一个音频帧的PTS
    int64_t m_audioClockBase;        // 音频时钟基准
    QElapsedTimer m_audioTimer;      // 高精度计时器
    int64_t m_deviceLatency;         // 音频设备延迟
    int64_t m_accumulatedSamples;    // 累积采样数
    double m_sampleDuration;         // 每个采样的时长(微秒)
    
    // 缓冲区管理
    int m_maxQueueSize;
    int m_minQueueSize;
    int m_optimalBufferSize;
    QTimer* m_bufferCheckTimer;
    
    // 性能监控
    int m_droppedFrames;
    int m_processedFrames;
    QTime m_lastStatsTime;
    
    // 错误恢复
    int m_errorCount;
    bool m_recoveryInProgress;
    QTimer* m_recoveryTimer;
    
    // 线程保护
    mutable QMutex m_stateMutex;
    mutable QMutex m_timeMutex;
    
    // 音频格式信息
    int m_sampleRate;
    int m_channels;
    int m_bytesPerSample;
    AVSampleFormat m_inputSampleFormat;
    
    // 质量控制
    bool m_enableQualityControl;
    int m_maxLatency; // 最大延迟(ms)
    int m_targetLatency; // 目标延迟(ms)
};

#endif // AUDIOPROCESSOR_H 