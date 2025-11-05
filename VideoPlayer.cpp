#include "VideoPlayer.h"
#include <QDateTime>
#include <QApplication>

VideoPlayer::VideoPlayer(QWidget *parent)
    : QMainWindow(parent)
    , m_videoWidget(nullptr)
    , m_formatContext(nullptr)
    , m_videoCodecContext(nullptr)
    , m_audioCodecContext(nullptr)
    , m_videoFrame(nullptr)
    , m_audioFrame(nullptr)
    , m_packet(nullptr)
    , m_videoStreamIndex(-1)
    , m_audioStreamIndex(-1)
    , m_audioProcessor(nullptr)
    , m_timer(new QTimer(this))
    , m_isPlaying(false)
    , m_isPaused(false)
    , m_isSeeking(false)
    , m_duration(0)
    , m_currentPosition(0)
    , m_fps(25.0)
    , m_volume(0.8f) // 默认音量80%
    , m_isPlaybackStable(false)
    , m_frameCount(0)
    , m_isDragging(false)
    , m_aspectRatio(16.0/9.0)
    , m_dragPosition(QPoint())
    , m_isResizing(false)
    , m_resizeDirection(None)
    , m_seekDebounceTimer(new QTimer(this))
    , m_pendingSeekPosition(0)
    , m_hasPendingSeek(false)
    , m_helpOverlay(nullptr)
    , m_videoInfoOverlay(nullptr)
    , m_loadingWidget(nullptr)
    , m_streamManager(new NetworkStreamManager(this))
    , m_streamUI(new NetworkStreamUI(this))
    , m_streamLoader(new NetworkStreamLoader(this))
    , m_lastSyncTime(0)
    , m_syncAdjustmentCount(0)
    , m_isNetworkStream(false)
{
    setupUI();
    setupFFmpeg();
    setupHelpOverlay();
    setupVideoInfoOverlay();
    
    connect(m_timer, &QTimer::timeout, this, &VideoPlayer::updatePosition);
    
    // 设置防抖定时器 - 更短的延迟，保持响应性
    m_seekDebounceTimer->setSingleShot(true);
    m_seekDebounceTimer->setInterval(50); // 减少到50ms，提高响应性
    connect(m_seekDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_hasPendingSeek) {
            performSeek(m_pendingSeekPosition);
            m_hasPendingSeek = false;
        }
    });
    
    // 连接 VideoWidget 的拖拽信号
    connect(m_videoWidget, &VideoWidget::videoFileDropped, this, &VideoPlayer::openVideo);
    
    // 连接网络流管理器信号
    connect(m_streamManager, &NetworkStreamManager::streamConnected, this, &VideoPlayer::onStreamConnected);
    connect(m_streamManager, &NetworkStreamManager::streamDisconnected, this, &VideoPlayer::onStreamDisconnected);
    connect(m_streamManager, &NetworkStreamManager::streamError, this, &VideoPlayer::onStreamError);
    connect(m_streamManager, &NetworkStreamManager::statusChanged, this, &VideoPlayer::onStreamStatusChanged);
    
    // 连接网络流UI信号
    connect(m_streamUI, &NetworkStreamUI::connectRequested, this, &VideoPlayer::onNetworkStreamRequested);
    
    // 连接异步加载器信号（移除进度更新信号）
    connect(m_streamLoader, &NetworkStreamLoader::loadingStarted, this, &VideoPlayer::onStreamLoadingStarted);
    connect(m_streamLoader, &NetworkStreamLoader::streamReady, this, &VideoPlayer::onStreamReady);
    connect(m_streamLoader, &NetworkStreamLoader::loadingFailed, this, &VideoPlayer::onStreamLoadingFailed);
    connect(m_streamLoader, &NetworkStreamLoader::loadingCancelled, this, &VideoPlayer::onStreamLoadingCancelled);
    
    setWindowTitle("Qt FFmpeg Video Player with Audio");
    resize(900, 700);
}

VideoPlayer::~VideoPlayer()
{
    // 取消正在进行的加载操作
    if (m_streamLoader) {
        m_streamLoader->cancelLoading();
    }
    
    // 断开网络流连接
    if (m_streamManager) {
        m_streamManager->disconnect();
    }
    
    // 关闭视频
    closeVideo();
    
    // 清理网络流组件
    if (m_streamUI) {
        m_streamUI->deleteLater();
        m_streamUI = nullptr;
    }
    
    if (m_streamManager) {
        m_streamManager->deleteLater();
        m_streamManager = nullptr;
    }
    
    if (m_streamLoader) {
        m_streamLoader->deleteLater();
        m_streamLoader = nullptr;
    }
}

void VideoPlayer::setupUI()
{
    setWindowTitle("动漫播放器");
    setMinimumSize(320, 240);
    
    // 极简界面 - 只有视频显示区域和圆角样式
    m_videoWidget = new VideoWidget(this);
    
    // 创建GIF加载动画组件
    m_loadingWidget = new LoadingWidget(this);
    
    // 极简设计 - 直接使用VideoWidget，无圆角，最高性能
    setCentralWidget(m_videoWidget);
    
    // 设置窗口属性 - 去除标题栏
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    
    // 简单黑色背景，无圆角，无复杂样式
    setStyleSheet("QMainWindow { background-color: black; }");
    
    // VideoWidget纯黑背景
    m_videoWidget->setStyleSheet("background-color: black;");
    
    // 启用鼠标跟踪和悬停事件以检测边缘位置
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    m_videoWidget->setMouseTracking(true);
    
    // 安装事件过滤器
    installEventFilter(this);
    
    // UI组件已移除，全部使用快捷键控制
    
    // 设置快捷键
    setupShortcuts();
}

void VideoPlayer::setupFFmpeg()
{
    // FFmpeg不再需要av_register_all()在新版本中
}

void VideoPlayer::setupShortcuts()
{
    // 文件操作快捷键
    QShortcut *openShortcut = new QShortcut(QKeySequence("Ctrl+O"), this);
    connect(openShortcut, &QShortcut::activated, this, &VideoPlayer::openFile);
    
    // 新增：网络视频快捷键
    QShortcut *openUrlShortcut = new QShortcut(QKeySequence("Ctrl+U"), this);
    connect(openUrlShortcut, &QShortcut::activated, this, &VideoPlayer::openNetworkUrl);
    
    QShortcut *quitShortcut = new QShortcut(QKeySequence("Ctrl+Q"), this);
    connect(quitShortcut, &QShortcut::activated, this, &QWidget::close);
    
    // 播放控制快捷键
    QShortcut *playPauseShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playPauseShortcut, &QShortcut::activated, this, &VideoPlayer::playPause);
    
    QShortcut *stopShortcut = new QShortcut(QKeySequence("Ctrl+S"), this);
    connect(stopShortcut, &QShortcut::activated, this, &VideoPlayer::stop);
    
    // 音量控制快捷键
    QShortcut *volumeUpShortcut = new QShortcut(QKeySequence(Qt::Key_Up), this);
    connect(volumeUpShortcut, &QShortcut::activated, this, [this]() {
        m_volume = qMin(1.0f, m_volume + 0.05f);
        if (m_audioProcessor) {
            m_audioProcessor->setVolume(m_volume);
        }
        qDebug() << "Volume up:" << (int)(m_volume * 100) << "%";
    });
    
    QShortcut *volumeDownShortcut = new QShortcut(QKeySequence(Qt::Key_Down), this);
    connect(volumeDownShortcut, &QShortcut::activated, this, [this]() {
        m_volume = qMax(0.0f, m_volume - 0.05f);
        if (m_audioProcessor) {
            m_audioProcessor->setVolume(m_volume);
        }
        qDebug() << "Volume down:" << (int)(m_volume * 100) << "%";
    });
    
    QShortcut *muteShortcut = new QShortcut(QKeySequence("M"), this);
    connect(muteShortcut, &QShortcut::activated, this, [this]() {
        static float lastVolume = m_volume;
        if (m_volume > 0) {
            lastVolume = m_volume;
            m_volume = 0;
        } else {
            m_volume = lastVolume;
        }
        if (m_audioProcessor) {
            m_audioProcessor->setVolume(m_volume);
        }
        qDebug() << "Volume:" << (m_volume > 0 ? "unmuted" : "muted") << (int)(m_volume * 100) << "%";
    });
    
    // 进度控制快捷键
    QShortcut *seekForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    // 保留长按功能，但通过防抖机制控制频率
    connect(seekForwardShortcut, &QShortcut::activated, this, [this]() {
        if (m_formatContext && !m_isSeeking) {
            // 简化稳定性检查 - 只在前5帧内限制，提高响应性
            if (!m_isPlaybackStable && m_frameCount < 5) {
                qDebug() << "Seek ignored - playback not stable yet, frame count:" << m_frameCount;
                return;
            }
            
            int currentPos = m_currentPosition / AV_TIME_BASE;
            int newPos = qMin((int)(m_duration / AV_TIME_BASE), currentPos + 10);
            qDebug() << "Seek forward from" << currentPos << "to" << newPos;
            seek(newPos);
        }
    });
    
    QShortcut *seekBackwardShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    // 保留长按功能，但通过防抖机制控制频率
    connect(seekBackwardShortcut, &QShortcut::activated, this, [this]() {
        if (m_formatContext && !m_isSeeking) {
            // 简化稳定性检查 - 只在前5帧内限制，提高响应性
            if (!m_isPlaybackStable && m_frameCount < 5) {
                qDebug() << "Seek ignored - playback not stable yet, frame count:" << m_frameCount;
                return;
            }
            
            int currentPos = m_currentPosition / AV_TIME_BASE;
            int newPos = qMax(0, currentPos - 10);
            qDebug() << "Seek backward from" << currentPos << "to" << newPos;
            seek(newPos);
        }
    });
    
    // 移除了Shift快捷键，避免系统冲突，简化用户体验
    
    // 快速seek快捷键 - 30秒步进
    QShortcut *seekForwardFastShortcut = new QShortcut(QKeySequence("Ctrl+Right"), this);
    // 恢复长按功能，适合快速浏览视频
    connect(seekForwardFastShortcut, &QShortcut::activated, this, [this]() {
        if (m_formatContext && !m_isSeeking) {
            // 添加播放稳定性检查
            if (!m_isPlaybackStable && m_frameCount < 5) {
                qDebug() << "Fast seek forward ignored - playback not stable yet, frame count:" << m_frameCount;
                return;
            }
            
            int currentPos = m_currentPosition / AV_TIME_BASE;
            int newPos = qMin((int)(m_duration / AV_TIME_BASE), currentPos + 30);
            qDebug() << "Fast seek forward from" << currentPos << "to" << newPos;
            seek(newPos);
        }
    });
    
    QShortcut *seekBackwardFastShortcut = new QShortcut(QKeySequence("Ctrl+Left"), this);
    // 恢复长按功能，适合快速浏览视频
    connect(seekBackwardFastShortcut, &QShortcut::activated, this, [this]() {
        if (m_formatContext && !m_isSeeking) {
            // 添加播放稳定性检查
            if (!m_isPlaybackStable && m_frameCount < 5) {
                qDebug() << "Fast seek backward ignored - playback not stable yet, frame count:" << m_frameCount;
                return;
            }
            
            int currentPos = m_currentPosition / AV_TIME_BASE;
            int newPos = qMax(0, currentPos - 30);
            qDebug() << "Fast seek backward from" << currentPos << "to" << newPos;
            seek(newPos);
        }
    });
    
    // 窗口控制快捷键
    QShortcut *fullscreenShortcut = new QShortcut(QKeySequence("F"), this);
    connect(fullscreenShortcut, &QShortcut::activated, this, [this]() {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    });
    
    QShortcut *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() {
        if (m_loadingWidget && m_loadingWidget->isLoading()) {
            // 如果正在显示加载动画，取消加载
            m_streamLoader->cancelLoading();
        } else if (isFullScreen()) {
            showNormal();
        }
    });
    
    // 最小化快捷键
    QShortcut *minimizeShortcut = new QShortcut(QKeySequence("Ctrl+M"), this);
    connect(minimizeShortcut, &QShortcut::activated, this, &QWidget::showMinimized);
    
    // 最大化/还原快捷键
    QShortcut *maximizeShortcut = new QShortcut(QKeySequence("Ctrl+X"), this);
    connect(maximizeShortcut, &QShortcut::activated, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });
    
    // 关闭窗口快捷键
    QShortcut *closeShortcut = new QShortcut(QKeySequence("Alt+F4"), this);
    connect(closeShortcut, &QShortcut::activated, this, &QWidget::close);
    
    // 显示/隐藏播放信息快捷键
    QShortcut *toggleInfoShortcut = new QShortcut(QKeySequence("I"), this);
    connect(toggleInfoShortcut, &QShortcut::activated, this, [this]() {
        // 临时显示播放信息
        if (!m_formatContext) return;
        
        int currentSec = m_currentPosition / AV_TIME_BASE;
        int totalSec = m_duration / AV_TIME_BASE;
        QString info = QString("播放进度: %1:%2 / %3:%4")
                      .arg(currentSec / 60, 2, 10, QChar('0'))
                      .arg(currentSec % 60, 2, 10, QChar('0'))
                      .arg(totalSec / 60, 2, 10, QChar('0'))
                      .arg(totalSec % 60, 2, 10, QChar('0'));
        
        // 在状态栏临时显示信息
        if (!statusBar()->isVisible()) {
            statusBar()->showMessage(info, 3000); // 显示3秒
            statusBar()->show();
            QTimer::singleShot(3000, this, [this]() {
                statusBar()->hide();
            });
        }
    });
    
    // 快捷键帮助
    QShortcut *helpShortcut = new QShortcut(QKeySequence("H"), this);
    connect(helpShortcut, &QShortcut::activated, this, &VideoPlayer::toggleHelpOverlay);
    
    QShortcut *helpShortcut2 = new QShortcut(QKeySequence("F1"), this);
    connect(helpShortcut2, &QShortcut::activated, this, &VideoPlayer::toggleHelpOverlay);
    
    // 视频信息显示快捷键
    QShortcut *videoInfoShortcut = new QShortcut(QKeySequence("V"), this);
    connect(videoInfoShortcut, &QShortcut::activated, this, &VideoPlayer::toggleVideoInfoOverlay);
}

void VideoPlayer::adaptWindowToVideo()
{
    if (!m_videoCodecContext) return;
    
    int videoWidth = m_videoCodecContext->width;
    int videoHeight = m_videoCodecContext->height;
    
    // 获取屏幕尺寸
    QScreen *screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    int screenWidth = screenGeometry.width();
    int screenHeight = screenGeometry.height();
    
    // 计算合适的窗口尺寸（保持纵横比）
    int windowWidth = videoWidth;
    int windowHeight = videoHeight;
    
    // 如果视频尺寸超过屏幕的80%，则缩放
    double maxWidth = screenWidth * 0.8;
    double maxHeight = screenHeight * 0.8;
    
    if (windowWidth > maxWidth || windowHeight > maxHeight) {
        double scaleX = maxWidth / windowWidth;
        double scaleY = maxHeight / windowHeight;
        double scale = qMin(scaleX, scaleY);
        
        windowWidth = (int)(windowWidth * scale);
        windowHeight = (int)(windowHeight * scale);
    }
    
    // 确保最小尺寸
    windowWidth = qMax(320, windowWidth);
    windowHeight = qMax(240, windowHeight);
    
    // 设置窗口尺寸并居中
    resize(windowWidth, windowHeight);
    
    // 居中显示
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;
    move(x, y);
    
    // Window adapted to video size
}

void VideoPlayer::openFile()
{
    // 临时隐藏覆盖层，避免覆盖文件选择器
    if (m_helpOverlay) {
        m_helpOverlay->temporaryHide();
    }
    if (m_videoInfoOverlay) {
        m_videoInfoOverlay->temporaryHide();
    }
    
    QString filename = QFileDialog::getOpenFileName(this,
        "Select Video File", "", 
        "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv);;All Files (*.*)");
    
    // 恢复覆盖层显示状态
    if (m_helpOverlay) {
        m_helpOverlay->restoreFromTemporaryHide();
    }
    if (m_videoInfoOverlay) {
        m_videoInfoOverlay->restoreFromTemporaryHide();
    }
    
    if (!filename.isEmpty()) {
        openVideo(filename);
    }
}

void VideoPlayer::openNetworkUrl()
{
    // 临时隐藏覆盖层，避免覆盖输入对话框
    if (m_helpOverlay) {
        m_helpOverlay->temporaryHide();
    }
    if (m_videoInfoOverlay) {
        m_videoInfoOverlay->temporaryHide();
    }
    
    // 使用新的网络流UI对话框
    m_streamUI->setStatus("就绪");
    if (m_streamUI->exec() == QDialog::Accepted) {
        // 对话框中的连接请求会通过信号处理
        // 这里不需要额外处理
    }
    
    // 恢复覆盖层显示状态
    if (m_helpOverlay) {
        m_helpOverlay->restoreFromTemporaryHide();
    }
    if (m_videoInfoOverlay) {
        m_videoInfoOverlay->restoreFromTemporaryHide();
    }
}

void VideoPlayer::openNetworkVideo(const QString &url)
{
    // Starting async network video loading
    
    // 关闭当前视频
    closeVideo();
    
    // 设置网络流标志
    m_isNetworkStream = true;
    
    // 如果已经在加载，取消之前的加载
    if (m_streamLoader->isLoading()) {
        m_streamLoader->cancelLoading();
    }
    
    // 设置当前文件URL
    m_currentFile = url;
    
    // 开始异步加载
    m_streamLoader->loadStreamAsync(url, 15000);  // 15秒超时
}

bool VideoPlayer::isNetworkUrl(const QString &path)
{
    return path.startsWith("http://", Qt::CaseInsensitive) || 
           path.startsWith("https://", Qt::CaseInsensitive) ||
           path.startsWith("rtmp://", Qt::CaseInsensitive) ||
           path.startsWith("rtsp://", Qt::CaseInsensitive);
}

bool VideoPlayer::openVideo(const QString &filename)
{
    closeVideo();  // 使用正确的方法名
    
    // 检测是否为网络流
    m_isNetworkStream = filename.startsWith("http://") || filename.startsWith("https://") || 
                       filename.startsWith("rtmp://") || filename.startsWith("rtsp://");
    
    if (m_isNetworkStream) {
        qDebug() << "Opening network stream:" << filename;
    } else {
        qDebug() << "Opening local file:" << filename;
    }
    
    QByteArray ba = filename.toUtf8();
    
    closeVideo();
    
    m_currentFile = filename;
    
    // 打开视频文件 - 使用UTF-8编码支持中文路径
    if (avformat_open_input(&m_formatContext, ba.constData(), nullptr, nullptr) != 0) {
        QMessageBox::critical(this, "Error", QString("Cannot open video file: %1").arg(filename));
        return false;
    }
    
    // 继续视频打开流程
    continueVideoOpening();
    
    return true;
}

void VideoPlayer::continueVideoOpening()
{
    // 获取流信息
    if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
        QMessageBox::critical(this, "Error", "Cannot get stream info");
        closeVideo();
        return;
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
        QMessageBox::critical(this, "Error", "No video stream found");
        closeVideo();
        return;
    }
    
    // 设置视频解码器
    AVStream *videoStream = m_formatContext->streams[m_videoStreamIndex];
    const AVCodec *videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!videoCodec) {
        QMessageBox::critical(this, "Error", "Video decoder not found");
        closeVideo();
        return;
    }
    
    m_videoCodecContext = avcodec_alloc_context3(videoCodec);
    if (avcodec_parameters_to_context(m_videoCodecContext, videoStream->codecpar) < 0) {
        QMessageBox::critical(this, "Error", "Cannot set video decoder parameters");
        closeVideo();
        return;
    }
    
    if (avcodec_open2(m_videoCodecContext, videoCodec, nullptr) < 0) {
        QMessageBox::critical(this, "Error", "Cannot open video decoder");
        closeVideo();
        return;
    }
    
    // 设置音频解码器（如果有音频流）
    if (m_audioStreamIndex != -1) {
        AVStream *audioStream = m_formatContext->streams[m_audioStreamIndex];
        const AVCodec *audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);
        if (audioCodec) {
            m_audioCodecContext = avcodec_alloc_context3(audioCodec);
            if (avcodec_parameters_to_context(m_audioCodecContext, audioStream->codecpar) >= 0) {
                if (avcodec_open2(m_audioCodecContext, audioCodec, nullptr) >= 0) {
                    setupAudio();
                } else {
                    qDebug() << "Cannot open audio decoder, playing video only";
                    avcodec_free_context(&m_audioCodecContext);
                    m_audioCodecContext = nullptr;
                }
            } else {
                qDebug() << "Cannot set audio decoder parameters, playing video only";
                avcodec_free_context(&m_audioCodecContext);
                m_audioCodecContext = nullptr;
            }
        } else {
            qDebug() << "Audio decoder not found, playing video only";
        }
    }
    
    // 分配帧和包
    m_videoFrame = av_frame_alloc();
    m_audioFrame = av_frame_alloc();
    m_packet = av_packet_alloc();
    
    // 获取视频信息
    m_duration = m_formatContext->duration;
    m_fps = av_q2d(videoStream->r_frame_rate);
    
    // 保存视频原始尺寸和宽高比
    m_originalVideoSize = QSize(m_videoCodecContext->width, m_videoCodecContext->height);
    m_aspectRatio = (double)m_videoCodecContext->width / m_videoCodecContext->height;
    
    // UI组件已移除，无需更新UI状态
    
    // 自动适配窗口大小到视频尺寸
    adaptWindowToVideo();
    
    // 自动开始播放
    playPause();
    
    QString displayName = isNetworkUrl(m_currentFile) ? "网络视频" : m_currentFile;
    qDebug() << "Video opened successfully:" << displayName;
    qDebug() << "Video size:" << m_videoCodecContext->width << "x" << m_videoCodecContext->height;
    qDebug() << "FPS:" << m_fps;
    qDebug() << "Duration:" << (m_duration / AV_TIME_BASE) << "seconds";
}

void VideoPlayer::setupAudio()
{
    if (!m_audioCodecContext) return;
    
    // 创建新的音频处理器
    m_audioProcessor = new AudioProcessor(this);
    
    // 连接音频处理器信号 - 使用队列连接避免递归
    connect(m_audioProcessor, &AudioProcessor::audioTimeChanged,
            this, [this](int64_t timestamp) {
        // 更新音频时钟用于同步
        syncAudioVideo();
    }, Qt::QueuedConnection);
    
    connect(m_audioProcessor, &AudioProcessor::bufferStatusChanged,
            this, [this](int bufferLevel, int maxBuffer) {
        // 移除缓冲状态日志，减少输出噪音
    }, Qt::QueuedConnection);
    
    connect(m_audioProcessor, &AudioProcessor::audioError,
            this, [this](const QString& error) {
        qDebug() << "Audio error:" << error;
        // 可选：显示错误消息给用户
    }, Qt::QueuedConnection);
    
    // 初始化音频处理器
    if (!m_audioProcessor->initialize(m_audioCodecContext)) {
        qDebug() << "Failed to initialize audio processor";
        delete m_audioProcessor;
        m_audioProcessor = nullptr;
        return;
    }
    
    // 设置音频流信息用于精确时间计算
    if (m_audioStreamIndex >= 0) {
        m_audioProcessor->setAudioStreamInfo(m_formatContext->streams[m_audioStreamIndex]);
    }
    
    // 设置音量
    m_audioProcessor->setVolume(m_volume);
    
    qDebug() << "Audio system initialized successfully";
}

void VideoPlayer::cleanupAudio()
{
    if (m_audioProcessor) {
        m_audioProcessor->cleanup();
        delete m_audioProcessor;
        m_audioProcessor = nullptr;
    }
}

void VideoPlayer::closeVideo()
{
    if (m_isPlaying) {
        m_timer->stop();
        m_isPlaying = false;
        m_isPaused = false;
    }
    
    cleanupAudio();
    
    if (m_videoFrame) {
        av_frame_free(&m_videoFrame);
        m_videoFrame = nullptr;
    }
    
    if (m_audioFrame) {
        av_frame_free(&m_audioFrame);
        m_audioFrame = nullptr;
    }
    
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    
    if (m_videoCodecContext) {
        avcodec_free_context(&m_videoCodecContext);
        m_videoCodecContext = nullptr;
    }
    
    if (m_audioCodecContext) {
        avcodec_free_context(&m_audioCodecContext);
        m_audioCodecContext = nullptr;
    }
    
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }
    
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_currentPosition = 0;
    m_duration = 0;
    
    // 清理视频显示
    m_videoWidget->clearFrame();
}

void VideoPlayer::playPause()
{
    if (!m_formatContext) return;
    
    if (m_isPlaying) {
        pauseVideo();
    } else {
        playVideo();
    }
}

void VideoPlayer::playVideo()
{
    if (!m_formatContext) return;
    
    m_isPlaying = true;
    
    // 重置播放稳定性状态
    m_isPlaybackStable = false;
    m_frameCount = 0;
    m_playStartTime = QTime::currentTime();
    
    // 重置同步状态
    m_lastSyncTime = 0;
    m_syncAdjustmentCount = 0;
    
    // 启动音频处理器 - 区分首次播放和从暂停恢复
    if (m_audioProcessor) {
        if (m_isPaused) {
            // 从暂停状态恢复
            m_audioProcessor->resume();
        } else {
            // 首次播放或重新播放
            m_audioProcessor->start();
        }
    }
    
    m_isPaused = false;
    
    // 启动高频定时器 - 追求最佳视觉体验
    int interval = qMax(8, (int)(1000.0 / m_fps)); // 最小8ms，支持120fps+
    m_timer->start(interval);
}

void VideoPlayer::pauseVideo()
{
    m_isPlaying = false;
    m_isPaused = true;
    m_timer->stop();
    
    // 暂停音频处理器
    if (m_audioProcessor) {
        m_audioProcessor->pause();
    }
}

void VideoPlayer::stop()
{
    if (!m_formatContext) return;
    
    m_timer->stop();
    m_isPlaying = false;
    m_isPaused = false;
    
    // 重置播放稳定性状态
    m_isPlaybackStable = false;
    m_frameCount = 0;
    
    // 停止音频处理器
    if (m_audioProcessor) {
        m_audioProcessor->stop();
    }
    
    // 重置到开头
    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    m_currentPosition = 0;
    
    m_videoWidget->clearFrame();
}

void VideoPlayer::seek(int position)
{
    if (!m_formatContext) return;
    
    QMutexLocker locker(&m_seekMutex);
    
    // 检查是否有正在进行的seek操作
    if (m_isSeeking && !m_seekDebounceTimer->isActive()) {
        qDebug() << "Seek ignored - another seek operation in progress";
        return;
    }
    
    // 简化的防抖逻辑 - 只防止真正的快速连击
    QTime currentTime = QTime::currentTime();
    
    if (m_lastSeekTime.isValid()) {
        int timeDiff = m_lastSeekTime.msecsTo(currentTime);
        
        // 只对极快的连击（<20ms）使用防抖，这种情况通常是意外的
        if (timeDiff < 20) {
            m_pendingSeekPosition = position;
            m_hasPendingSeek = true;
            
            // 重启防抖定时器
            m_seekDebounceTimer->stop();
            m_seekDebounceTimer->start();
            
            qDebug() << "Seek debounced - position:" << position << "timeDiff:" << timeDiff << "ms";
            return;
        }
    }
    
    // 如果没有频繁操作，直接执行seek
    m_lastSeekTime = currentTime;
    performSeek(position);
}

void VideoPlayer::performSeek(int position)
{
    if (!m_formatContext) return;
    
    // 强制保护 - 如果已经在seek中，忽略后续请求
    if (m_isSeeking) {
        qDebug() << "PerformSeek ignored - already seeking";
        return;
    }
    
    // 边界检查 - 确保position在有效范围内
    int maxPosition = m_duration / AV_TIME_BASE;
    if (position < 0) position = 0;
    if (position > maxPosition) position = maxPosition;
    
    qDebug() << "PerformSeek start - Position:" << position 
             << "Current:" << (m_currentPosition / AV_TIME_BASE)
             << "Max:" << maxPosition;
    
    // 立即设置seek状态，防止任何干扰
    m_isSeeking = true;
    
    // 暂时停止定时器，避免冲突
    bool wasPlaying = m_isPlaying;
    if (m_isPlaying) {
        m_timer->stop();
    }
    
    int64_t seekTarget = (int64_t)position * AV_TIME_BASE;
    
    // 执行seek操作
    bool seekSuccess = false;
    
    // 对于小步长的seek（15秒以内），尝试更精确的策略
    int currentPos = m_currentPosition / AV_TIME_BASE;
    int seekDistance = abs(position - currentPos);
    
    if (seekDistance <= 15) {
        // 小距离：尝试精确seek
        seekSuccess = (av_seek_frame(m_formatContext, -1, seekTarget, 0) >= 0);
    } else {
        // 大距离：使用关键帧seek
        seekSuccess = (av_seek_frame(m_formatContext, -1, seekTarget, AVSEEK_FLAG_BACKWARD) >= 0);
    }
    
    if (seekSuccess) {
        // 清理解码器缓冲区，避免显示旧帧
        if (m_videoCodecContext) {
            avcodec_flush_buffers(m_videoCodecContext);
        }
        if (m_audioCodecContext) {
            avcodec_flush_buffers(m_audioCodecContext);
        }
        
        // 通知音频处理器进行seek
        if (m_audioProcessor) {
            m_audioProcessor->seek(seekTarget);
            qDebug() << "Audio processor seek completed";
        }
        
        // 简化的帧查找逻辑
        int attempts = 0;
        bool foundFrame = false;
        
        while (attempts < 10 && !foundFrame) {
            if (av_read_frame(m_formatContext, m_packet) >= 0) {
                if (m_packet->stream_index == m_videoStreamIndex) {
                    int ret = avcodec_send_packet(m_videoCodecContext, m_packet);
                    if (ret >= 0) {
                        ret = avcodec_receive_frame(m_videoCodecContext, m_videoFrame);
                        if (ret == 0) {
                            // 显示视频帧
                            m_videoWidget->displayFrame(m_videoFrame, m_videoCodecContext->width, m_videoCodecContext->height);
                            
                            // 更新位置
                            if (m_videoFrame->pts != AV_NOPTS_VALUE) {
                                AVStream *stream = m_formatContext->streams[m_videoStreamIndex];
                                m_currentPosition = av_rescale_q(m_videoFrame->pts, stream->time_base, AV_TIME_BASE_Q);
                            } else {
                                m_currentPosition = seekTarget;
                            }
                            foundFrame = true;
                        }
                    }
                }
                av_packet_unref(m_packet);
            } else {
                break; // 文件结束
            }
            attempts++;
        }
        
        // 如果没找到帧，使用目标位置
        if (!foundFrame) {
            m_currentPosition = seekTarget;
        }
        
        // UI组件已移除，无需更新UI
        
        qDebug() << "Seek completed - Target:" << position << "s, Actual:" << (m_currentPosition / AV_TIME_BASE) << "s, Distance:" << seekDistance << "s";
    } else {
        qDebug() << "Seek failed for position:" << position;
    }
    
    // 恢复播放状态
    if (wasPlaying) {
        m_timer->start(1000.0 / m_fps);
    }
    
    // 清除seek状态 - 确保状态重置
    m_isSeeking = false;
    
    qDebug() << "PerformSeek completed - Final position:" << (m_currentPosition / AV_TIME_BASE) << "s";
}

void VideoPlayer::updatePosition()
{
    if (!m_isPlaying || !m_formatContext || m_isSeeking) return;
    
    // 只需要解码帧，UI组件已移除
    decodeFrame();
}

bool VideoPlayer::decodeFrame()
{
    if (!m_formatContext || !m_videoCodecContext) return false;
    
    bool videoFrameDecoded = false;
    
    while (av_read_frame(m_formatContext, m_packet) >= 0) {
        if (m_packet->stream_index == m_videoStreamIndex) {
            int ret = avcodec_send_packet(m_videoCodecContext, m_packet);
            if (ret < 0) {
                av_packet_unref(m_packet);
                continue;
            }
            
            ret = avcodec_receive_frame(m_videoCodecContext, m_videoFrame);
            if (ret == 0) {
                // 显示视频帧
                m_videoWidget->displayFrame(m_videoFrame, m_videoCodecContext->width, m_videoCodecContext->height);
                
                // 更新当前位置
                if (m_videoFrame->pts != AV_NOPTS_VALUE) {
                    AVStream *stream = m_formatContext->streams[m_videoStreamIndex];
                    m_currentPosition = av_rescale_q(m_videoFrame->pts, stream->time_base, AV_TIME_BASE_Q);
                }
                
                // 跟踪播放稳定性 - 快速稳定，提高响应性
                m_frameCount++;
                if (!m_isPlaybackStable && m_frameCount >= 5) {
                    m_isPlaybackStable = true;
                    // 移除播放稳定性日志，减少输出
                }
                
                videoFrameDecoded = true;
                // 不要在这里return，继续处理可能的音频包
            }
        } else if (m_packet->stream_index == m_audioStreamIndex && m_audioCodecContext && m_isPlaying) {
            // 发送音频包到音频处理器
            if (m_audioProcessor) {
                m_audioProcessor->processAudioPacket(m_packet);
            }
        }
        av_packet_unref(m_packet);
        
        // 如果已经解码了视频帧，可以返回让界面更新
        if (videoFrameDecoded) {
            return true;
        }
    }
    
    // 到达文件末尾
    stop();
    return false;
}

void VideoPlayer::syncAudioVideo()
{
    // 实现音视频同步逻辑
    if (!m_audioProcessor || !m_isPlaying) {
        return;
    }
    
    // 获取精确的音频时间
    int64_t audioTime = m_audioProcessor->getAccurateAudioTime();
    
    // 计算音视频时间差
    int64_t timeDiff = m_currentPosition - audioTime;
    
    // 根据流类型调整同步参数
    int64_t syncThreshold, maxSyncAdjustment, minSyncAdjustment;
    
    if (m_isNetworkStream) {
        // 网络流使用更宽松的阈值，考虑网络抖动
        syncThreshold = 60000;       // 60ms同步阈值
        maxSyncAdjustment = 300000;  // 最大调整量300ms
        minSyncAdjustment = 15000;   // 最小调整量15ms
    } else {
        // 本地文件使用更严格的阈值
        syncThreshold = 40000;       // 40ms同步阈值
        maxSyncAdjustment = 200000;  // 最大调整量200ms
        minSyncAdjustment = 10000;   // 最小调整量10ms
    }
    
    // 定期输出同步状态（每100次调用输出一次，方便查看）
    static int syncCallCount = 0;
    syncCallCount++;
    bool shouldLogStatus = (syncCallCount % 100 == 1);
    
    // 添加基本调试信息，确认函数被调用
    if (shouldLogStatus) {
        qDebug() << "[DEBUG] Sync called V:" << (m_currentPosition / 1000) << "ms A:" << (audioTime / 1000) << "ms Delta:" << (timeDiff / 1000) << "ms";
    }
    
    if (abs(timeDiff) > syncThreshold && abs(timeDiff) < maxSyncAdjustment) {
        // 防止过于频繁的调整
        int64_t currentTime = QDateTime::currentMSecsSinceEpoch();
        if (currentTime - m_lastSyncTime < 100) { // 100ms内不重复调整
            return;
        }
        
        // 使用自适应同步策略
        int64_t adjustment;
        
        if (abs(timeDiff) > 150000) { // 超过150ms，快速调整
            adjustment = timeDiff * 0.8; // 调整80%的差异
        } else if (abs(timeDiff) > 100000) { // 100-150ms，中等调整
            adjustment = timeDiff * 0.6; // 调整60%的差异
        } else if (abs(timeDiff) > 60000) { // 60-100ms，温和调整
            adjustment = timeDiff * 0.4; // 调整40%的差异
        } else { // 40-60ms，轻微调整
            adjustment = timeDiff * 0.3; // 调整30%的差异
        }
        
        // 确保调整量不会太小
        if (abs(adjustment) < minSyncAdjustment) {
            adjustment = (timeDiff > 0) ? minSyncAdjustment : -minSyncAdjustment;
        }
        
        m_audioProcessor->setMasterClock(m_currentPosition - adjustment);
        
        // 记录同步调整
        m_lastSyncTime = currentTime;
        m_syncAdjustmentCount++;
        
        // 输出同步调整日志
        if (abs(timeDiff) > (m_isNetworkStream ? 50000 : 40000)) {
            qDebug() << "[SYNC]" << (m_isNetworkStream ? "NET" : "LOCAL") 
                     << "V:" << (m_currentPosition / 1000) << "ms"
                     << "A:" << (audioTime / 1000) << "ms"
                     << "Delta:" << (timeDiff / 1000) << "ms"
                     << "Adj:" << (adjustment / 1000) << "ms";
        }
        
    } else if (shouldLogStatus && abs(timeDiff) > syncThreshold) {
        // 只在有问题时输出状态
        QString status = (abs(timeDiff) >= maxSyncAdjustment) ? "FAR" : "MONITOR";
        
        qDebug() << "[A/V]" << status << (m_isNetworkStream ? "NET" : "LOCAL")
                 << "V:" << (m_currentPosition / 1000) << "ms"
                 << "A:" << (audioTime / 1000) << "ms" 
                 << "Delta:" << (timeDiff / 1000) << "ms";
    }
}

void VideoPlayer::setupHelpOverlay()
{
    // 创建帮助覆盖层
    m_helpOverlay = new OverlayWidget(this);
    
    // 预设置帮助文本内容
    QString helpText = 
        "<div style='font-family: \"Microsoft YaHei UI\", \"Segoe UI\", sans-serif; font-size: 10pt; line-height: 1.4; color: rgba(255,255,255,0.9); margin: 0; padding: 0; border: 0; outline: 0;'>"
        
        // 播放控制
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>播放/暂停：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>Space</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>停止播放：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>Ctrl+S</span>"
        "</div>"
        
        // 进度控制
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>快进/快退：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>← →</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>大幅跳跃：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>Ctrl+← →</span>"
        "</div>"
        
        // 音量控制
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>音量调节：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>↑ ↓</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>静音切换：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>M</span>"
        "</div>"
        
        // 文件操作
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>打开文件：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>Ctrl+O</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>网络视频：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>Ctrl+U</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>退出程序：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>Ctrl+Q</span>"
        "</div>"
        
        // 窗口控制
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>全屏切换：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>F</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>退出全屏：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>Esc</span>"
        "</div>"
        
        // 其他功能
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>播放信息：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>I</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>显示帮助：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>H</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>视频信息：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>V</span>"
        "</div>"
        
        "<div style='margin-bottom: 0px; line-height: 1.2;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 9pt; min-width: 100px; display: inline-block;'>拖拽窗口：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 9pt;'>鼠标</span>"
        "</div>"
        
        "</div>";
    
    // 设置帮助内容
    m_helpOverlay->setContent(helpText);
}

void VideoPlayer::toggleHelpOverlay()
{
    if (m_helpOverlay->isOverlayVisible()) {
        // 隐藏帮助
        m_helpOverlay->hideOverlay();
    } else {
        // 显示帮助
        int w = width(), h = height();
        int ow = 240;  // 固定宽度
        int oh = 300;  // 高度以容纳所有快捷键
        
        // 位置计算 - 显示在右侧
        int x = w - ow - 30;  // 右边距
        int y = (h - oh) / 2 - 20;  // 稍微向上偏移
        
        // 边界检查
        x = qMax(10, qMin(x, w - ow - 10));
        y = qMax(10, qMin(y, h - oh - 10));
        
        // 显示覆盖层，10秒后自动隐藏
        m_helpOverlay->showOverlay(x, y, ow, oh, 10000);
    }
}

void VideoPlayer::setupVideoInfoOverlay()
{
    // 创建视频信息覆盖层
    m_videoInfoOverlay = new OverlayWidget(this);
}

void VideoPlayer::toggleVideoInfoOverlay()
{
    if (m_videoInfoOverlay->isOverlayVisible()) {
        // 清除更新回调并隐藏视频信息
        m_videoInfoOverlay->setUpdateCallback(nullptr);
        m_videoInfoOverlay->hideOverlay();
    } else {
        // 显示视频信息
        QString infoText;
        if (!m_formatContext) {
            // 如果没有视频加载，显示提示信息
            infoText = 
                "<div style='font-family: \"Microsoft YaHei UI\", \"Segoe UI\", sans-serif; font-size: 10pt; line-height: 1.4; color: rgba(255,255,255,0.9); text-align: center;'>"
                "<span style='color: rgba(255,255,255,0.7); font-size: 11pt;'>暂未加载视频文件</span><br/>"
                "<span style='color: rgba(255,255,255,0.5); font-size: 9pt;'>请按 Ctrl+O 打开视频</span>"
                "</div>";
        } else {
            // 生成视频信息文本
            infoText = generateVideoInfoText();
        }
        
        // 设置内容
        m_videoInfoOverlay->setContent(infoText);
        
        // 设置实时更新回调
        m_videoInfoOverlay->setUpdateCallback([this]() -> QString {
            return getCurrentVideoInfoText();
        });
        
        // 计算位置和尺寸
        int w = width(), h = height();
        int ow = 280;  // 视频信息框稍宽一些
        
        // 动态计算高度，基于是否有视频加载
        int oh = m_formatContext ? 320 : 120;  // 有视频时较高，无视频时较矮
        
        // 位置计算 - 显示在左侧，与帮助框区分
        int x = 30;  // 左边距
        int y = (h - oh) / 2 - 20;  // 垂直居中，稍微向上偏移
        
        // 边界检查
        x = qMax(10, qMin(x, w - ow - 10));
        y = qMax(10, qMin(y, h - oh - 10));
        
        // 显示覆盖层，8秒后自动隐藏
        m_videoInfoOverlay->showOverlay(x, y, ow, oh, 8000);
    }
}

QString VideoPlayer::generateVideoInfoText()
{
    if (!m_formatContext) return "";
    
    QString infoText = 
        "<div style='font-family: \"Microsoft YaHei UI\", \"Segoe UI\", sans-serif; font-size: 9pt; line-height: 1.4; color: rgba(255,255,255,0.9); margin: 0; padding: 0;'>";
    
    // 文件信息
    QString displayName;
    if (isNetworkUrl(m_currentFile)) {
        // 网络URL使用智能截断
        displayName = truncateFileName(m_currentFile, 45);
    } else {
        // 本地文件只显示文件名
        QFileInfo fileInfo(m_currentFile);
        displayName = truncateFileName(fileInfo.fileName(), 45);
    }
    
    infoText += QString(
        "<div style='margin-bottom: 4px;'>"
        "<span style='color: rgba(100,149,237,0.9); font-size: 10pt; font-weight: bold;'>文件信息</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>文件名：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 8pt; word-break: break-all;'>%1</span>"
        "</div>"
    ).arg(displayName);
    
    // 视频流信息
    if (m_videoCodecContext) {
        infoText += QString(
            "<div style='margin-bottom: 4px; margin-top: 8px;'>"
            "<span style='color: rgba(100,149,237,0.9); font-size: 10pt; font-weight: bold;'>视频流</span>"
            "</div>"
            
            "<div style='margin-bottom: 3px;'>"
            "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>编码格式：</span>"
            "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%1</span>"
            "</div>"
            
            "<div style='margin-bottom: 3px;'>"
            "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>分辨率：</span>"
            "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%2×%3</span>"
            "</div>"
            
            "<div style='margin-bottom: 3px;'>"
            "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>帧率：</span>"
            "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%4 FPS</span>"
            "</div>"
            
            "<div style='margin-bottom: 3px;'>"
            "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>宽高比：</span>"
            "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%5:1</span>"
            "</div>"
        ).arg(avcodec_get_name(m_videoCodecContext->codec_id))
         .arg(m_videoCodecContext->width)
         .arg(m_videoCodecContext->height)
         .arg(m_fps, 0, 'f', 2)  // 格式化为2位小数
         .arg(m_aspectRatio, 0, 'f', 2);  // 格式化为2位小数
    }
    
    // 音频流信息
    if (m_audioCodecContext) {
        infoText += QString(
            "<div style='margin-bottom: 4px; margin-top: 8px;'>"
            "<span style='color: rgba(100,149,237,0.9); font-size: 10pt; font-weight: bold;'>音频流</span>"
            "</div>"
            
            "<div style='margin-bottom: 3px;'>"
            "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>编码格式：</span>"
            "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%1</span>"
            "</div>"
            
            "<div style='margin-bottom: 3px;'>"
            "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>采样率：</span>"
            "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%2 Hz</span>"
            "</div>"
            
            "<div style='margin-bottom: 3px;'>"
            "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>声道数：</span>"
            "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%3</span>"
            "</div>"
        ).arg(avcodec_get_name(m_audioCodecContext->codec_id))
         .arg(m_audioCodecContext->sample_rate)
         .arg(m_audioCodecContext->ch_layout.nb_channels);
    }
    
    // 播放信息
    int currentSec = m_currentPosition / AV_TIME_BASE;
    int totalSec = m_duration / AV_TIME_BASE;
    infoText += QString(
        "<div style='margin-bottom: 4px; margin-top: 8px;'>"
        "<span style='color: rgba(100,149,237,0.9); font-size: 10pt; font-weight: bold;'>播放状态</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>时长：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%1:%2</span>"
        "</div>"
        
        "<div style='margin-bottom: 3px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>进度：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%3:%4 / %1:%2</span>"
        "</div>"
        
        "<div style='margin-bottom: 0px;'>"
        "<span style='color: rgba(255,255,255,0.7); font-size: 8pt; min-width: 60px; display: inline-block;'>音量：</span>"
        "<span style='color: rgba(255,255,255,0.9); font-size: 8pt;'>%5%</span>"
        "</div>"
    ).arg(totalSec / 60, 2, 10, QChar('0'))
     .arg(totalSec % 60, 2, 10, QChar('0'))
     .arg(currentSec / 60, 2, 10, QChar('0'))
     .arg(currentSec % 60, 2, 10, QChar('0'))
     .arg((int)(m_volume * 100));
    
    infoText += "</div>";
    return infoText;
}

QString VideoPlayer::getCurrentVideoInfoText()
{
    // 这个方法返回实时更新的视频信息，主要是播放状态部分
    return generateVideoInfoText();  // 重用现有的生成逻辑
}

QString VideoPlayer::truncateFileName(const QString &fileName, int maxLength)
{
    if (fileName.length() <= maxLength) {
        return fileName;
    }
    
    // 对于网络URL，特殊处理
    if (fileName.startsWith("http://") || fileName.startsWith("https://")) {
        // 尝试提取有意义的部分
        QUrl url(fileName);
        QString host = url.host();
        QString path = url.path();
        
        // 如果主机名 + 路径仍然太长，进行智能截断
        QString simplified = host + path;
        if (simplified.length() <= maxLength) {
            return simplified;
        }
        
        // 保留域名和文件名部分
        QStringList pathParts = path.split('/', Qt::SkipEmptyParts);
        if (!pathParts.isEmpty()) {
            QString lastPart = pathParts.last();
            QString result = host + "/.../" + lastPart;
            
            if (result.length() <= maxLength) {
                return result;
            }
            
            // 如果还是太长，截断最后部分
            int availableLength = maxLength - host.length() - 7; // 7 = "/.../" + "..."
            if (availableLength > 0) {
                return host + "/.../" + lastPart.left(availableLength) + "...";
            }
        }
        
        // 最后的备用方案：简单截断
        return fileName.left(maxLength - 3) + "...";
    }
    
    // 对于普通文件名，保留开头和结尾
    if (maxLength > 10) {
        int frontLength = (maxLength - 3) * 2 / 3;  // 前面占2/3
        int backLength = (maxLength - 3) - frontLength;  // 后面占1/3
        
        return fileName.left(frontLength) + "..." + fileName.right(backLength);
    }
    
    // 如果maxLength太小，简单截断
    return fileName.left(maxLength - 3) + "...";
}

// 移除了applySoftLimiter - 保持原始音频纯净度，避免引入噪音和失真

// 鼠标事件处理 - 实现窗口拖拽和缩放
void VideoPlayer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        m_resizeDirection = getResizeDirection(pos);
        
        if (m_resizeDirection != None) {
            // 开始缩放
            m_isResizing = true;
            m_resizeStartPos = event->globalPosition().toPoint();
            m_resizeStartGeometry = geometry();
        } else {
            // 开始拖拽
            m_isDragging = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
        event->accept();
    }
}

void VideoPlayer::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        if (m_isResizing) {
            // 执行缩放 - 节流减少频繁更新，提高性能
            static QTime lastUpdate;
            if (!lastUpdate.isValid() || lastUpdate.msecsTo(QTime::currentTime()) > 20) { // 减少到50fps，提高性能
                QRect newGeometry = calculateNewGeometry(event->globalPosition().toPoint());
                setGeometry(newGeometry);
                lastUpdate = QTime::currentTime();
            }
        } else if (m_isDragging) {
            // 执行拖拽 - 添加节流优化
            static QTime lastDragUpdate;
            if (!lastDragUpdate.isValid() || lastDragUpdate.msecsTo(QTime::currentTime()) > 10) { // 100fps拖拽
                move(event->globalPosition().toPoint() - m_dragPosition);
                lastDragUpdate = QTime::currentTime();
            }
        }
        event->accept();
    } else {
        // 减少光标更新频率
        static QTime lastCursorUpdate;
        if (!lastCursorUpdate.isValid() || lastCursorUpdate.msecsTo(QTime::currentTime()) > 50) {
            updateCursor(event->pos());
            lastCursorUpdate = QTime::currentTime();
        }
    }
}

void VideoPlayer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        m_isResizing = false;
        m_resizeDirection = None;
        setCursor(Qt::ArrowCursor);
        event->accept();
    }
}

// 窗口缩放事件 - 保持视频宽高比
void VideoPlayer::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    
    // 调整帮助覆盖层位置
    if (m_helpOverlay && m_helpOverlay->isOverlayVisible()) {
        int windowWidth = width();
        int windowHeight = height();
        
        // 使用固定的紧凑尺寸
        int overlayWidth = 240;
        int overlayHeight = 300;
        
        // 智能位置选择
        int x = windowWidth - overlayWidth - 30;  // 右边距
        int y = (windowHeight - overlayHeight) / 2 - 20;  // 垂直位置
        
        // 确保不超出窗口边界
        x = qMax(10, qMin(x, windowWidth - overlayWidth - 10));
        y = qMax(10, qMin(y, windowHeight - overlayHeight - 10));
        
        m_helpOverlay->updateOverlayGeometry(x, y, overlayWidth, overlayHeight);
    }
    
    // 调整视频信息覆盖层位置
    if (m_videoInfoOverlay && m_videoInfoOverlay->isOverlayVisible()) {
        int windowWidth = width();
        int windowHeight = height();
        
        // 动态计算尺寸
        int overlayWidth = 280;
        int overlayHeight = m_formatContext ? 320 : 120;
        
        // 位置计算 - 显示在左侧
        int x = 30;  // 左边距
        int y = (windowHeight - overlayHeight) / 2 - 20;  // 垂直居中
        
        // 确保不超出窗口边界
        x = qMax(10, qMin(x, windowWidth - overlayWidth - 10));
        y = qMax(10, qMin(y, windowHeight - overlayHeight - 10));
        
        m_videoInfoOverlay->updateOverlayGeometry(x, y, overlayWidth, overlayHeight);
    }
    
    // 调整GIF加载动画位置
    if (m_loadingWidget && m_loadingWidget->isLoading()) {
        m_loadingWidget->updatePosition();
    }
    
    // 如果正在手动缩放，不执行自动比例调整，避免冲突
    if (m_isResizing) {
        return;
    }
    
    // 如果有视频加载，确保窗口保持正确的宽高比
    if (m_originalVideoSize.isValid() && !isMaximized() && !isFullScreen()) {
        QSize newSize = event->size();
        QSize originalSize = event->oldSize();
        
        // 避免在程序启动时的初始resize事件中触发
        if (!originalSize.isValid()) {
            return;
        }
        
        // 计算基于宽度的高度
        int newHeight = (int)(newSize.width() / m_aspectRatio);
        
        // 如果计算出的高度超出当前高度，则基于高度计算宽度
        if (newHeight > newSize.height()) {
            int newWidth = (int)(newSize.height() * m_aspectRatio);
            newSize = QSize(newWidth, newSize.height());
        } else {
            newSize = QSize(newSize.width(), newHeight);
        }
        
        // 只有当尺寸真的需要调整时才resize，避免无限循环
        if (newSize != event->size() && abs(newSize.width() - event->size().width()) > 1) {
            QTimer::singleShot(0, this, [this, newSize]() {
                if (!m_isResizing) {  // 再次检查，确保不在手动缩放中
                    resize(newSize);
                }
            });
        }
    }
}

void VideoPlayer::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    
    // 窗口移动时，更新覆盖层位置，使其跟随窗口移动
    if (m_helpOverlay && m_helpOverlay->isOverlayVisible()) {
        int windowWidth = width();
        int windowHeight = height();
        
        // 使用固定的紧凑尺寸
        int overlayWidth = 240;
        int overlayHeight = 300;
        
        // 智能位置选择
        int x = windowWidth - overlayWidth - 30;  // 右边距
        int y = (windowHeight - overlayHeight) / 2 - 20;  // 垂直位置
        
        // 确保不超出窗口边界
        x = qMax(10, qMin(x, windowWidth - overlayWidth - 10));
        y = qMax(10, qMin(y, windowHeight - overlayHeight - 10));
        
        m_helpOverlay->updateOverlayGeometry(x, y, overlayWidth, overlayHeight);
    }
    
    // 同时更新视频信息覆盖层位置
    if (m_videoInfoOverlay && m_videoInfoOverlay->isOverlayVisible()) {
        int windowWidth = width();
        int windowHeight = height();
        
        // 动态计算尺寸
        int overlayWidth = 280;
        int overlayHeight = m_formatContext ? 320 : 120;
        
        // 位置计算 - 显示在左侧
        int x = 30;  // 左边距
        int y = (windowHeight - overlayHeight) / 2 - 20;  // 垂直居中
        
        // 确保不超出窗口边界
        x = qMax(10, qMin(x, windowWidth - overlayWidth - 10));
        y = qMax(10, qMin(y, windowHeight - overlayHeight - 10));
        
        m_videoInfoOverlay->updateOverlayGeometry(x, y, overlayWidth, overlayHeight);
    }
    
    // 同时更新GIF加载动画位置
    if (m_loadingWidget && m_loadingWidget->isLoading()) {
        m_loadingWidget->updatePosition();
    }
}

bool VideoPlayer::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == this && event->type() == QEvent::HoverMove) {
        QHoverEvent *hoverEvent = static_cast<QHoverEvent*>(event);
        updateCursor(hoverEvent->position().toPoint());
    }
    return QMainWindow::eventFilter(obj, event);
}

void VideoPlayer::paintEvent(QPaintEvent *event)
{
    // 极简绘制，最高性能
    QMainWindow::paintEvent(event);
}

// 检测鼠标位置对应的缩放方向
VideoPlayer::ResizeDirection VideoPlayer::getResizeDirection(const QPoint &pos)
{
    const int borderWidth = 8; // 边缘检测宽度
    const QRect rect = this->rect();
    
    ResizeDirection direction = None;
    
    if (pos.x() <= borderWidth) {
        direction = static_cast<ResizeDirection>(direction | Left);
    } else if (pos.x() >= rect.width() - borderWidth) {
        direction = static_cast<ResizeDirection>(direction | Right);
    }
    
    if (pos.y() <= borderWidth) {
        direction = static_cast<ResizeDirection>(direction | Top);
    } else if (pos.y() >= rect.height() - borderWidth) {
        direction = static_cast<ResizeDirection>(direction | Bottom);
    }
    
    return direction;
}

// 根据鼠标位置更新光标样式
void VideoPlayer::updateCursor(const QPoint &pos)
{
    if (m_isResizing || m_isDragging) return;
    
    ResizeDirection direction = getResizeDirection(pos);
    
    switch (direction) {
        case Left:
        case Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case Top:
        case Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case TopLeft:
        case BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case TopRight:
        case BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        default:
            setCursor(Qt::ArrowCursor);
            break;
    }
}

// 计算新的窗口几何形状（保持宽高比）
QRect VideoPlayer::calculateNewGeometry(const QPoint &currentPos)
{
    QPoint delta = currentPos - m_resizeStartPos;
    QRect newGeometry = m_resizeStartGeometry;
    
    // 根据缩放方向调整窗口大小
    if (m_resizeDirection & Left) {
        newGeometry.setLeft(m_resizeStartGeometry.left() + delta.x());
    }
    if (m_resizeDirection & Right) {
        newGeometry.setRight(m_resizeStartGeometry.right() + delta.x());
    }
    if (m_resizeDirection & Top) {
        newGeometry.setTop(m_resizeStartGeometry.top() + delta.y());
    }
    if (m_resizeDirection & Bottom) {
        newGeometry.setBottom(m_resizeStartGeometry.bottom() + delta.y());
    }
    
    // 确保最小尺寸
    const int minWidth = 320;
    const int minHeight = 240;
    
    if (newGeometry.width() < minWidth) {
        if (m_resizeDirection & Left) {
            newGeometry.setLeft(newGeometry.right() - minWidth);
        } else {
            newGeometry.setWidth(minWidth);
        }
    }
    
    if (newGeometry.height() < minHeight) {
        if (m_resizeDirection & Top) {
            newGeometry.setTop(newGeometry.bottom() - minHeight);
        } else {
            newGeometry.setHeight(minHeight);
        }
    }
    
    // 如果有视频加载且不是最大化/全屏状态，保持宽高比
    if (m_originalVideoSize.isValid() && !isMaximized() && !isFullScreen()) {
        // 根据主要缩放方向决定如何保持比例
        if ((m_resizeDirection & (Left | Right)) && !(m_resizeDirection & (Top | Bottom))) {
            // 只调整宽度，根据宽度计算高度
            int newHeight = qRound(newGeometry.width() / m_aspectRatio);
            if (m_resizeDirection & Top) {
                newGeometry.setTop(newGeometry.bottom() - newHeight);
            } else {
                newGeometry.setHeight(newHeight);
            }
        } else if ((m_resizeDirection & (Top | Bottom)) && !(m_resizeDirection & (Left | Right))) {
            // 只调整高度，根据高度计算宽度
            int newWidth = qRound(newGeometry.height() * m_aspectRatio);
            if (m_resizeDirection & Left) {
                newGeometry.setLeft(newGeometry.right() - newWidth);
            } else {
                newGeometry.setWidth(newWidth);
            }
        } else if (m_resizeDirection & (TopLeft | TopRight | BottomLeft | BottomRight)) {
            // 对角缩放，保持原始比例 - 使用更稳定的算法
            QPoint delta = currentPos - m_resizeStartPos;
            
            // 根据拖拽距离的主要方向决定缩放基准
            if (abs(delta.x()) > abs(delta.y())) {
                // 主要是水平移动，以宽度为基准
                int newHeight = qRound(newGeometry.width() / m_aspectRatio);
                if (m_resizeDirection & Top) {
                    newGeometry.setTop(newGeometry.bottom() - newHeight);
                } else {
                    newGeometry.setHeight(newHeight);
                }
            } else {
                // 主要是垂直移动，以高度为基准
                int newWidth = qRound(newGeometry.height() * m_aspectRatio);
                if (m_resizeDirection & Left) {
                    newGeometry.setLeft(newGeometry.right() - newWidth);
                } else {
                    newGeometry.setWidth(newWidth);
                }
            }
        }
    }
    
    return newGeometry;
}

// 网络流相关方法实现
void VideoPlayer::onNetworkStreamRequested(const NetworkStreamUI::StreamSettings &settings)
{
    qDebug() << "Network stream requested:" << settings.url;
    
    // 对话框已经在onConnectClicked中关闭了，这里直接开始异步加载
    openNetworkVideo(settings.url);
}

void VideoPlayer::onStreamConnected()
{
    m_streamUI->setConnecting(false);
    m_streamUI->setStatus("连接成功");
    
    // 获取当前URL并开始播放
    QString url = m_streamManager->getCurrentUrl();
    if (!url.isEmpty()) {
        // 关闭UI对话框
        m_streamUI->accept();
        
        // 开始播放网络视频
        openNetworkVideo(url);
    }
}

void VideoPlayer::onStreamDisconnected()
{
    m_streamUI->setConnecting(false);
    m_streamUI->setStatus("连接已断开");
    
    // 可以在这里处理断连后的逻辑
    if (m_isPlaying) {
        pauseVideo();
    }
}

void VideoPlayer::onStreamError(const QString &error)
{
    m_streamUI->setConnecting(false);
    m_streamUI->setStatus(QString("错误: %1").arg(error));
    
    // 显示错误消息
    QMessageBox::warning(this, "网络流错误", error);
}

void VideoPlayer::onStreamStatusChanged()
{
    // 更新状态显示
    QString status = QString("网络流状态已更新");
    if (m_streamUI->isVisible()) {
        m_streamUI->setStatus(status);
    }
}

void VideoPlayer::onStreamLoadingStarted()
{
    // 显示右下角GIF加载动画
    m_loadingWidget->showLoading();
}

void VideoPlayer::onStreamReady(const NetworkStreamLoader::StreamInfo &streamInfo)
{
    qDebug() << "Stream ready, setting up video player";
    
    // 隐藏GIF加载动画
    m_loadingWidget->hideLoading();
    
    // 设置FFmpeg上下文
    m_formatContext = streamInfo.formatContext;
    m_videoStreamIndex = streamInfo.videoStreamIndex;
    m_audioStreamIndex = streamInfo.audioStreamIndex;
    m_videoCodecContext = streamInfo.videoCodecContext;
    m_audioCodecContext = streamInfo.audioCodecContext;
    
    // 分配帧和包
    m_videoFrame = av_frame_alloc();
    m_audioFrame = av_frame_alloc();
    m_packet = av_packet_alloc();
    
    // 获取视频信息
    m_duration = streamInfo.duration;
    m_fps = streamInfo.fps;
    
    // 保存视频原始尺寸和宽高比
    m_originalVideoSize = QSize(streamInfo.width, streamInfo.height);
    m_aspectRatio = (double)streamInfo.width / streamInfo.height;
    
    // 设置音频（如果有音频流）
    if (m_audioCodecContext) {
        setupAudio();
    }
    
    // 自动适配窗口大小到视频尺寸
    adaptWindowToVideo();
    
    // 自动开始播放
    playPause();
    
    qDebug() << "Network video opened successfully";
    qDebug() << "Video size:" << streamInfo.width << "x" << streamInfo.height;
    qDebug() << "FPS:" << streamInfo.fps;
    qDebug() << "Duration:" << (streamInfo.duration / AV_TIME_BASE) << "seconds";
}

void VideoPlayer::onStreamLoadingFailed(const QString &error)
{
    qDebug() << "Stream loading failed:" << error;
    
    // 隐藏GIF加载动画
    m_loadingWidget->hideLoading();
    
    // 只有在真正失败时才显示错误消息（检查是否已经有视频在播放）
    if (!m_formatContext || !m_isPlaying) {
        QMessageBox::critical(this, "网络流加载失败", 
            QString("无法加载网络视频流:\n%1").arg(error));
    } else {
        qDebug() << "Ignoring error signal as stream is already playing successfully";
    }
}

void VideoPlayer::onStreamLoadingCancelled()
{
    qDebug() << "Stream loading cancelled";
    
    // 隐藏GIF加载动画
    m_loadingWidget->hideLoading();
}