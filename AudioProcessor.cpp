#include "AudioProcessor.h"
#include <QApplication>
#include <cstring>

AudioProcessor::AudioProcessor(QObject *parent)
    : QObject(parent)
    , m_audioCodecContext(nullptr)
    , m_swrContext(nullptr)
    , m_audioFrame(nullptr)
    , m_audioStream(nullptr)
    , m_audioSink(nullptr)
    , m_audioDevice(nullptr)
    , m_initialized(false)
    , m_isPlaying(false)
    , m_isPaused(false)
    , m_isSeeking(false)
    , m_volume(0.8f)
    , m_masterClock(0)
    , m_audioBasePts(AV_NOPTS_VALUE)
    , m_maxQueueSize(60)
    , m_minQueueSize(8)
    , m_optimalBufferSize(4096)
    , m_bufferCheckTimer(nullptr)
    , m_droppedFrames(0)
    , m_processedFrames(0)
    , m_errorCount(0)
    , m_recoveryInProgress(false)
    , m_recoveryTimer(nullptr)
    , m_sampleRate(44100)
    , m_channels(2)
    , m_bytesPerSample(2)
    , m_inputSampleFormat(AV_SAMPLE_FMT_NONE)
    , m_enableQualityControl(true)
    , m_maxLatency(200)
    , m_targetLatency(100)
{
    m_audioFrame = av_frame_alloc();
    
    // 新增：初始化精确时间同步变量
    m_lastAudioPts = AV_NOPTS_VALUE;
    m_audioClockBase = 0;
    m_deviceLatency = 0;
    m_accumulatedSamples = 0;
    m_sampleDuration = 0.0;
    
    // 初始化定时器
    m_bufferCheckTimer = new QTimer(this);
    m_bufferCheckTimer->setInterval(100);
    connect(m_bufferCheckTimer, &QTimer::timeout, this, &AudioProcessor::checkBufferStatus);
    
    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setSingleShot(true);
    m_recoveryTimer->setInterval(1000);
    connect(m_recoveryTimer, &QTimer::timeout, this, &AudioProcessor::attemptRecovery);
    
    qDebug() << "AudioProcessor created - simplified version";
}

AudioProcessor::~AudioProcessor()
{
    cleanup();
    
    if (m_audioFrame) {
        av_frame_free(&m_audioFrame);
    }
}

bool AudioProcessor::initialize(AVCodecContext* audioCodecContext)
{
    if (!audioCodecContext) {
        qDebug() << "Invalid audio codec context";
        return false;
    }
    
    cleanup();
    
    m_audioCodecContext = audioCodecContext;
    
    // 设置音频格式
    m_audioFormat.setSampleRate(audioCodecContext->sample_rate);
    m_audioFormat.setChannelCount(qMin(audioCodecContext->ch_layout.nb_channels, 2));
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    
    qDebug() << "Audio format - Rate:" << m_audioFormat.sampleRate() 
             << "Channels:" << m_audioFormat.channelCount();
    
    // 获取默认输出设备
    m_outputDevice = QMediaDevices::defaultAudioOutput();
    if (m_outputDevice.isNull()) {
        emit audioError("No audio output device found");
        return false;
    }
    
    // 检查格式支持
    if (!m_outputDevice.isFormatSupported(m_audioFormat)) {
        qDebug() << "Trying alternative sample rate...";
        m_audioFormat.setSampleRate(44100);
        if (!m_outputDevice.isFormatSupported(m_audioFormat)) {
            emit audioError("Audio format not supported");
            return false;
        }
    }
    
    // 设置重采样器
    if (!setupResampler()) {
        emit audioError("Failed to setup resampler");
        return false;
    }
    
    // 新增：计算采样时长和设备延迟
    m_sampleDuration = 1000000.0 / m_audioFormat.sampleRate(); // 微秒/采样
    
    // 估算音频设备延迟（基于缓冲区大小）
    int bufferFrames = m_audioFormat.sampleRate() * m_targetLatency / 1000; // 目标延迟对应的帧数
    m_deviceLatency = (int64_t)(bufferFrames * m_sampleDuration); // 转换为微秒
    
    qDebug() << "Audio timing setup - Sample duration:" << m_sampleDuration 
             << "us, Device latency:" << (m_deviceLatency / 1000) << "ms";
    
    // 设置音频设备
    if (!setupAudioDevice()) {
        emit audioError("Failed to setup audio device");
        return false;
    }
    
    m_initialized = true;
    qDebug() << "AudioProcessor initialized successfully";
    
    return true;
}

void AudioProcessor::cleanup()
{
    stop();
    cleanupAudioDevice();
    cleanupResampler();
    
    m_audioCodecContext = nullptr;
    m_initialized = false;
    m_masterClock = 0;
    m_audioBasePts = AV_NOPTS_VALUE;
    
    qDebug() << "AudioProcessor cleaned up";
}

bool AudioProcessor::setupResampler()
{
    cleanupResampler();
    
    m_swrContext = swr_alloc();
    if (!m_swrContext) {
        qDebug() << "Failed to allocate SwrContext";
        return false;
    }
    
    AVChannelLayout out_ch_layout;
    if (m_audioFormat.channelCount() == 1) {
        out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
    } else {
        out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    }
    
    int ret = swr_alloc_set_opts2(&m_swrContext,
                                  &out_ch_layout,
                                  AV_SAMPLE_FMT_S16,
                                  m_audioFormat.sampleRate(),
                                  &m_audioCodecContext->ch_layout,
                                  m_audioCodecContext->sample_fmt,
                                  m_audioCodecContext->sample_rate,
                                  0, nullptr);
    
    av_channel_layout_uninit(&out_ch_layout);
    
    if (ret < 0) {
        qDebug() << "Failed to set resampler options";
        swr_free(&m_swrContext);
        return false;
    }
    
    // 使用标准重采样设置，避免复杂配置导致的问题
    // av_opt_set_int(m_swrContext, "resampler", SWR_ENGINE_SOXR, 0);
    // av_opt_set_double(m_swrContext, "cutoff", 0.98, 0);
    // av_opt_set_int(m_swrContext, "dither_method", SWR_DITHER_TRIANGULAR, 0);
    
    if (swr_init(m_swrContext) < 0) {
        qDebug() << "Failed to initialize SwrContext";
        swr_free(&m_swrContext);
        return false;
    }
    
    qDebug() << "Audio resampler setup successfully";
    return true;
}

void AudioProcessor::cleanupResampler()
{
    if (m_swrContext) {
        swr_free(&m_swrContext);
        m_swrContext = nullptr;
    }
}

bool AudioProcessor::setupAudioDevice()
{
    cleanupAudioDevice();
    
    m_audioSink = new QAudioSink(m_outputDevice, m_audioFormat, this);
    m_audioSink->setVolume(m_volume);
    
    qDebug() << "Audio device setup completed";
    return true;
}

void AudioProcessor::cleanupAudioDevice()
{
    if (m_audioDevice) {
        m_audioDevice = nullptr;
    }
    
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
    }
}

void AudioProcessor::start()
{
    if (!m_initialized || m_isPlaying) {
        return;
    }
    
    m_isPlaying = true;
    m_isPaused = false;
    
    if (m_audioSink) {
        m_audioDevice = m_audioSink->start();
        if (!m_audioDevice) {
            qDebug() << "Failed to start audio device";
            m_isPlaying = false;
            return;
        }
    }
    
    // 启动缓冲区监控
    m_bufferCheckTimer->start();
    
    // 新增：启动高精度计时器
    m_audioTimer.start();
    m_audioClockBase = 0;
    m_accumulatedSamples = 0;
    
    // Audio playback started
}

void AudioProcessor::pause()
{
    if (!m_isPlaying || m_isPaused) {
        return;
    }
    
    QMutexLocker locker(&m_stateMutex);
    
    m_isPaused = true;
    
    if (m_audioSink && m_audioDevice) {
        m_audioSink->suspend();
        qDebug() << "Audio device suspended";
    }
    
    m_bufferCheckTimer->stop();
    
    qDebug() << "Audio playback paused";
}

void AudioProcessor::resume()
{
    if (!m_initialized || !m_isPaused) {
        return;
    }
    
    QMutexLocker locker(&m_stateMutex);
    
    m_isPaused = false;
    
    if (m_audioSink && m_audioDevice) {
        // 从暂停状态恢复
        m_audioSink->resume();
        qDebug() << "Audio device resumed from pause";
    } else if (m_audioSink) {
        // 如果设备丢失，重新启动
        m_audioDevice = m_audioSink->start();
        if (!m_audioDevice) {
            qDebug() << "Failed to restart audio device after pause";
            m_isPaused = true;
            return;
        }
        qDebug() << "Audio device restarted after pause";
    }
    
    // 重新调整音频时间基准
    m_audioStartTime = QTime::currentTime();
    
    // 重新启动缓冲区监控
    m_bufferCheckTimer->start();
    
    qDebug() << "Audio playback resumed";
}

void AudioProcessor::stop()
{
    m_isPlaying = false;
    m_isPaused = false;
    
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioDevice = nullptr;
    }
    
    // 清理音频队列
    clearAudioQueue();
    
    m_audioBasePts = AV_NOPTS_VALUE;
    m_masterClock = 0;
    
    // 停止定时器
    if (m_bufferCheckTimer) {
        m_bufferCheckTimer->stop();
    }
    if (m_recoveryTimer) {
        m_recoveryTimer->stop();
    }
    
    qDebug() << "Audio playback stopped";
}

void AudioProcessor::seek(int64_t timestamp)
{
    QMutexLocker locker(&m_stateMutex);
    
    m_isSeeking = true;
    clearAudioQueue();
    
    m_masterClock = timestamp;
    m_audioBasePts = AV_NOPTS_VALUE;
    m_audioStartTime = QTime::currentTime();
    
    if (m_isPlaying && m_audioSink) {
        restartAudioDevice();
    }
    
    m_isSeeking = false;
    
    qDebug() << "Audio seek to:" << timestamp;
}

void AudioProcessor::processAudioPacket(AVPacket* packet)
{
    if (!m_initialized || !m_audioCodecContext || !packet || !m_audioDevice) {
        return;
    }
    
    int ret = avcodec_send_packet(m_audioCodecContext, packet);
    if (ret < 0) {
        return;
    }
    
    while (ret >= 0) {
        ret = avcodec_receive_frame(m_audioCodecContext, m_audioFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }
        
        // 重采样音频帧
        uint8_t* outputBuffer = nullptr;
        int outputSize = resampleAudioFrame(m_audioFrame, &outputBuffer);
        
        if (outputSize > 0 && outputBuffer) {
            // 直接写入音频设备
            qint64 written = m_audioDevice->write(
                reinterpret_cast<const char*>(outputBuffer), outputSize);
            
            if (written > 0 && m_audioFrame->pts != AV_NOPTS_VALUE) {
                if (m_audioBasePts == AV_NOPTS_VALUE) {
                    qDebug() << "[AUDIO] First audio PTS:" << m_audioFrame->pts;
                }
                
                // 持续更新音频PTS以反映当前播放位置
                m_audioBasePts = m_audioFrame->pts;
                
                // 新增：更新精确的音频时钟
                updateAudioClock(m_audioFrame->pts, m_audioFrame->nb_samples);
                
                emit audioTimeChanged(m_audioFrame->pts);
            } else if (written > 0) {
                static int noPtsCount = 0;
                if (++noPtsCount <= 3) {
                    qDebug() << "[WARN] Audio frame without PTS, count:" << noPtsCount;
                }
            }
            
            av_free(outputBuffer);
            m_processedFrames++;
        }
    }
}

void AudioProcessor::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    
    if (m_audioSink) {
        m_audioSink->setVolume(m_volume);
    }
}

int64_t AudioProcessor::getCurrentAudioTime() const
{
    if (m_audioBasePts == AV_NOPTS_VALUE || !m_isPlaying) {
        return m_masterClock;
    }
    
    return m_audioBasePts;
}

void AudioProcessor::setMasterClock(int64_t timestamp)
{
    m_masterClock = timestamp;
}

// 新增：更精确的音频时间计算
int64_t AudioProcessor::getAccurateAudioTime() const
{
    if (!m_isPlaying) {
        return m_masterClock;
    }
    
    // 优先使用最新的音频PTS，转换为微秒
    if (m_audioBasePts != AV_NOPTS_VALUE) {
        if (m_audioStream) {
            // 使用正确的音频流时间基准
            int64_t audioTimeUs = av_rescale_q(m_audioBasePts, 
                                              m_audioStream->time_base, 
                                              AV_TIME_BASE_Q);
            
            // 使用音频流时间基准进行转换
            return audioTimeUs;
        } else {
            // 回退方案：假设标准音频时间基准
            int64_t audioTimeUs = av_rescale_q(m_audioBasePts, 
                                              {1, m_audioFormat.sampleRate()}, 
                                              AV_TIME_BASE_Q);
            // 使用回退方案：标准音频时间基准
            return audioTimeUs;
        }
    } else {
                // 没有有效的音频PTS，使用备用时钟
    }
    
    // 如果有精确的音频时钟基准，使用它
    if (m_audioClockBase > 0 && m_audioTimer.isValid()) {
        // 计算从播放开始到现在的实际时间
        int64_t elapsedTime = m_audioTimer.elapsed() * 1000; // 转换为微秒
        
        // 加上基准时间，减去设备延迟
        int64_t actualAudioTime = m_audioClockBase + elapsedTime - m_deviceLatency;
        
        // 确保时间不会是负数
        return qMax(0LL, actualAudioTime);
    }
    
    // 最后回退到主时钟
    return qMax(0LL, m_masterClock);
}

int64_t AudioProcessor::getAudioDeviceLatency() const
{
    return m_deviceLatency;
}

void AudioProcessor::updateAudioClock(int64_t pts, int sampleCount)
{
    if (pts != AV_NOPTS_VALUE) {
        m_lastAudioPts = pts;
        
        // 如果是第一帧或者时间基准需要重置
        if (m_audioClockBase == 0 || abs(pts - (m_audioClockBase + m_audioTimer.elapsed() * 1000)) > 100000) {
            m_audioClockBase = pts;
            m_audioTimer.restart();
            m_accumulatedSamples = 0;
        }
        
        // 累积采样数用于精确计算
        m_accumulatedSamples += sampleCount;
        
        // 动态调整设备延迟估算
        adjustDeviceLatency();
    }
}

void AudioProcessor::adjustDeviceLatency()
{
    if (!m_audioSink) return;
    
    // 基于音频设备的实际缓冲状态调整延迟估算
    int bufferSize = m_audioSink->bufferSize();
    int byteRate = m_audioFormat.sampleRate() * m_audioFormat.channelCount() * 2; // 16-bit samples
    
    if (byteRate > 0) {
        // 计算缓冲区对应的时间延迟
        int64_t calculatedLatency = (int64_t)(bufferSize * 1000000.0 / byteRate);
        
        // 平滑调整延迟值，避免突变
        m_deviceLatency = (m_deviceLatency * 3 + calculatedLatency) / 4;
    }
}

void AudioProcessor::setAudioStreamInfo(AVStream* audioStream)
{
    m_audioStream = audioStream;
}

QString AudioProcessor::getStatusInfo() const
{
    return QString("Audio Status - Playing: %1, Processed: %2, Dropped: %3")
        .arg(m_isPlaying ? "Yes" : "No")
        .arg(m_processedFrames)
        .arg(m_droppedFrames);
}

int AudioProcessor::resampleAudioFrame(AVFrame* frame, uint8_t** outputBuffer)
{
    if (!frame || !m_swrContext) {
        return 0;
    }
    
    int outSamples = swr_get_out_samples(m_swrContext, frame->nb_samples);
    if (outSamples <= 0) {
        return 0;
    }
    
    int outBufferSize = av_samples_get_buffer_size(
        nullptr, m_audioFormat.channelCount(), outSamples, AV_SAMPLE_FMT_S16, 0);
    
    *outputBuffer = static_cast<uint8_t*>(av_malloc(outBufferSize));
    if (!*outputBuffer) {
        return 0;
    }
    
    int convertedSamples = swr_convert(
        m_swrContext, outputBuffer, outSamples,
        const_cast<const uint8_t**>(frame->data), frame->nb_samples);
    
    if (convertedSamples < 0) {
        av_free(*outputBuffer);
        *outputBuffer = nullptr;
        return 0;
    }
    
    return av_samples_get_buffer_size(
        nullptr, m_audioFormat.channelCount(), convertedSamples, AV_SAMPLE_FMT_S16, 0);
}

void AudioProcessor::clearAudioQueue()
{
    QMutexLocker locker(&m_queueMutex);
    
    while (!m_audioQueue.isEmpty()) {
        AudioPacket* packet = m_audioQueue.dequeue();
        delete packet;
    }
}

void AudioProcessor::processAudioQueue()
{
    // 简化版本：不使用复杂的队列处理
    // 直接在processAudioPacket中处理
}

void AudioProcessor::checkBufferStatus()
{
    if (!m_isPlaying || m_isPaused) {
        return;
    }
    
    // 使用音频设备的实际状态而不是空的队列
    if (m_audioSink) {
        int bufferLevel = 0;
        QAudio::State state = m_audioSink->state();
        
        // 基于音频设备状态判断缓冲区情况
        if (state == QAudio::ActiveState) {
            // 音频设备正常工作，估算缓冲区级别
            bufferLevel = qMax(1, m_processedFrames % 10); // 简单的估算
        } else if (state == QAudio::IdleState) {
            bufferLevel = 0; // 设备空闲，可能缓冲区不足
        }
        
        emit bufferStatusChanged(bufferLevel, 10); // 使用更合理的最大值
        
        // 大幅减少日志输出 - 只在严重问题时输出
        static int underrunCount = 0;
        static int lastLogTime = 0;
        int currentTime = QTime::currentTime().msecsSinceStartOfDay();
        
        if (bufferLevel == 0 && state == QAudio::IdleState) {
            underrunCount++;
            // 每5秒最多输出一次，且只在连续问题时输出
            if (underrunCount > 50 && (currentTime - lastLogTime) > 5000) {
                qDebug() << "[WARN] Audio buffer issues detected";
                lastLogTime = currentTime;
                underrunCount = 0;
            }
        } else {
            underrunCount = 0; // 重置计数器
        }
    }
}

void AudioProcessor::handleAudioStateChanged()
{
    if (m_audioSink) {
        QAudio::State state = m_audioSink->state();
        if (state == QAudio::IdleState || state == QAudio::StoppedState) {
            qDebug() << "Audio device state changed to:" << state;
        }
    }
}

bool AudioProcessor::restartAudioDevice()
{
    if (!m_audioSink) {
        return false;
    }
    
    qDebug() << "Restarting audio device...";
    
    m_audioSink->stop();
    m_audioDevice = nullptr;
    
    if (m_isPlaying) {
        m_audioDevice = m_audioSink->start();
        if (!m_audioDevice) {
            qDebug() << "Failed to restart audio device";
            return false;
        }
    }
    
    return true;
}

void AudioProcessor::manageDynamicBuffer()
{
    // 简化版本：不进行动态缓冲区管理
}

int AudioProcessor::getOptimalBufferSize() const
{
    return m_optimalBufferSize;
}

void AudioProcessor::adjustPlaybackTiming()
{
    // 简化版本：不进行播放时序调整
}

int64_t AudioProcessor::calculateAudioDelay() const
{
    return 0; // 简化版本：返回0延迟
}

bool AudioProcessor::shouldDropFrame(int64_t framePts) const
{
    // 简化版本：不丢帧
    return false;
}

void AudioProcessor::handleAudioDeviceError()
{
    qDebug() << "Audio device error detected";
    m_errorCount++;
    
    if (m_errorCount > 3 && !m_recoveryInProgress) {
        attemptRecovery();
    }
}

void AudioProcessor::attemptRecovery()
{
    if (m_recoveryInProgress) {
        return;
    }
    
    m_recoveryInProgress = true;
    qDebug() << "Attempting audio recovery...";
    
    bool wasPlaying = m_isPlaying;
    
    // 停止播放
    stop();
    
    // 重新设置音频设备
    if (setupAudioDevice()) {
        if (wasPlaying) {
            start();
        }
        m_errorCount = 0;
        qDebug() << "Audio recovery successful";
    } else {
        qDebug() << "Audio recovery failed";
        emit audioError("Audio recovery failed");
    }
    
    m_recoveryInProgress = false;
}


