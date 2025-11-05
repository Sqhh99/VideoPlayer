#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>  // 新增：用于网络URL输入对话框
#include "NetworkStreamManager.h"  // 新增：网络流管理器
#include "NetworkStreamUI.h"       // 新增：网络流UI组件
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QTime>
#include <QThread>
#include <QMutex>
#include <QDebug>
#include <QShortcut>
#include <QScreen>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QHoverEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QScrollArea>
#include <QUrl>
#include <cmath>
#include <QMoveEvent>

#include "VideoWidget.h"
#include "AudioProcessor.h"
#include "OverlayWidget.h"
#include "NetworkStreamLoader.h"
#include "LoadingWidget.h"

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
}

class VideoPlayer : public QMainWindow
{
    Q_OBJECT

public:
    // 窗口缩放方向枚举
    enum ResizeDirection {
        None = 0,
        Left = 1,
        Right = 2,
        Top = 4,
        Bottom = 8,
        TopLeft = Top | Left,
        TopRight = Top | Right,
        BottomLeft = Bottom | Left,
        BottomRight = Bottom | Right
    };

    VideoPlayer(QWidget *parent = nullptr);
    ~VideoPlayer();
    
    bool openVideo(const QString &filename);  // 修改返回类型为bool
    void openNetworkVideo(const QString &url);  // 重构：使用新的网络流管理器
    void openNetworkStream();                   // 新增：打开网络流对话框
    bool isNetworkUrl(const QString &path);     // 新增：判断是否为网络URL

protected:
    // 鼠标事件处理
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;  // 处理窗口移动，更新覆盖层位置
    bool eventFilter(QObject *obj, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override; // 自定义绘制圆角

private slots:
    void openFile();
    void openNetworkUrl();     // 重构：使用新的网络流对话框
    void playPause();
    void stop();
    void seek(int position);
    void updatePosition();
    void toggleHelpOverlay();  // 切换快捷键帮助覆盖层
    void toggleVideoInfoOverlay();  // 切换视频信息覆盖层
    
    // 网络流相关槽函数
    void onStreamConnected();
    void onStreamDisconnected();
    void onStreamError(const QString &error);
    void onStreamStatusChanged();
    void onNetworkStreamRequested(const NetworkStreamUI::StreamSettings &settings);
    
    // 异步加载相关槽函数
    void onStreamLoadingStarted();
    void onStreamReady(const NetworkStreamLoader::StreamInfo &streamInfo);
    void onStreamLoadingFailed(const QString &error);
    void onStreamLoadingCancelled();

private:
    void setupUI();
    void setupFFmpeg();
    void setupShortcuts();
    void setupHelpOverlay();  // 设置帮助覆盖层
    void setupVideoInfoOverlay();  // 设置视频信息覆盖层
    QString generateVideoInfoText();  // 生成视频信息文本
    QString getCurrentVideoInfoText();  // 获取当前实时的视频信息
    QString truncateFileName(const QString &fileName, int maxLength = 50);  // 智能截断文件名
    void adaptWindowToVideo();
    void closeVideo();
    void playVideo();
    void pauseVideo();
    bool decodeFrame();
    void setupAudio();
    void cleanupAudio();        // 新增：清理音频
    void syncAudioVideo();      // 新增：音视频同步
    void performSeek(int position);  // 新增：执行跳转
    void continueVideoOpening();  // 新增：继续视频打开流程（用于网络视频）
    
    // 窗口缩放辅助方法
    ResizeDirection getResizeDirection(const QPoint &pos);
    void updateCursor(const QPoint &pos);
    QRect calculateNewGeometry(const QPoint &currentPos);
    
    // UI components - 极简设计，只保留视频显示组件
    VideoWidget *m_videoWidget;
    
    // 覆盖层组件
    OverlayWidget *m_helpOverlay;      // 帮助覆盖层
    OverlayWidget *m_videoInfoOverlay; // 视频信息覆盖层
    LoadingWidget *m_loadingWidget;    // GIF加载动画组件
    
    // FFmpeg components - Video
    AVFormatContext *m_formatContext;
    AVCodecContext *m_videoCodecContext;
    AVCodecContext *m_audioCodecContext;
    AVFrame *m_videoFrame;
    AVFrame *m_audioFrame;
    AVPacket *m_packet;
    int m_videoStreamIndex;
    int m_audioStreamIndex;
    
    // 新音频处理器
    AudioProcessor *m_audioProcessor;
    
    // Playback control
    QTimer *m_timer;
    bool m_isPlaying;
    bool m_isPaused;
    bool m_isSeeking;
    int64_t m_duration;
    int64_t m_currentPosition;
    double m_fps;
    float m_volume;
    
    QString m_currentFile;
    
    // 播放稳定性相关
    QTime m_playStartTime;
    bool m_isPlaybackStable;
    int m_frameCount;
    
    // 新增：增强的同步控制
    int64_t m_lastSyncTime;          // 上次同步调整的时间
    int m_syncAdjustmentCount;       // 同步调整次数
    bool m_isNetworkStream;          // 是否为网络流
    
    // 窗口移动相关
    bool m_isDragging;
    QPoint m_dragPosition;
    
    // 窗口缩放相关
    bool m_isResizing;
    ResizeDirection m_resizeDirection;
    QPoint m_resizeStartPos;
    QRect m_resizeStartGeometry;
    
    // 视频比例相关
    double m_aspectRatio;
    QSize m_originalVideoSize;
    
    // 防抖和并发保护
    QTimer *m_seekDebounceTimer;
    QTime m_lastSeekTime;
    QMutex m_seekMutex;
    int m_pendingSeekPosition;
    bool m_hasPendingSeek;
    
    // 网络流管理组件
    NetworkStreamManager *m_streamManager;
    NetworkStreamUI *m_streamUI;
    NetworkStreamLoader *m_streamLoader;
};

#endif // VIDEOPLAYER_H