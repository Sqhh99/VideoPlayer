#include "VideoWidget.h"
#include <QResizeEvent>
#include <QDebug>
#include <cstring> // for memcpy
#include <QDragEnterEvent>
#include <QMimeData>
#include <QFileInfo>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
    , m_swsContext(nullptr)
    , m_rgbBuffer(nullptr)
    , m_rgbFrame(nullptr)
    , m_videoWidth(0)
    , m_videoHeight(0)
{
    setMinimumSize(320, 240);
    
    // 启用拖拽功能
    setAcceptDrops(true);
    
    // 性能优化设置
    setStyleSheet("background-color: black;");
    
    // 启用硬件加速和渲染优化
    setAttribute(Qt::WA_PaintOnScreen, false);  // 使用双缓冲
    setAttribute(Qt::WA_OpaquePaintEvent, true); // 不透明绘制，提高性能
    setAttribute(Qt::WA_NoSystemBackground, true); // 禁用系统背景
}

VideoWidget::~VideoWidget()
{
    cleanupSwsContext();
}

void VideoWidget::setupSwsContext(int width, int height)
{
    cleanupSwsContext();
    
    m_videoWidth = width;
    m_videoHeight = height;
    
    // 创建RGB帧
    m_rgbFrame = av_frame_alloc();
    if (!m_rgbFrame) {
        qDebug() << "Failed to allocate RGB frame";
        return;
    }
    
    // 分配RGB缓冲区
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
    m_rgbBuffer = (uint8_t*)av_malloc(numBytes);
    if (!m_rgbBuffer) {
        qDebug() << "Failed to allocate RGB buffer";
        av_frame_free(&m_rgbFrame);
        return;
    }
    
    // 设置RGB帧数据指针
    av_image_fill_arrays(m_rgbFrame->data, m_rgbFrame->linesize, m_rgbBuffer,
                        AV_PIX_FMT_RGB24, width, height, 1);
    
    // 创建高性能缩放上下文 - 使用最快算法
    m_swsContext = sws_getContext(width, height, AV_PIX_FMT_YUV420P,
                                 width, height, AV_PIX_FMT_RGB24,
                                 SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!m_swsContext) {
        qDebug() << "Failed to create SWS context";
        cleanupSwsContext();
    }
}

void VideoWidget::cleanupSwsContext()
{
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    
    if (m_rgbBuffer) {
        av_free(m_rgbBuffer);
        m_rgbBuffer = nullptr;
    }
    
    if (m_rgbFrame) {
        av_frame_free(&m_rgbFrame);
        m_rgbFrame = nullptr;
    }
    
    m_videoWidth = 0;
    m_videoHeight = 0;
}

void VideoWidget::displayFrame(AVFrame* frame, int width, int height)
{
    if (!frame) return;
    
    QMutexLocker locker(&m_mutex);
    
    // 如果尺寸改变，重新设置上下文
    if (width != m_videoWidth || height != m_videoHeight) {
        setupSwsContext(width, height);
    }
    
    if (!m_swsContext || !m_rgbFrame) return;
    
    // 转换为RGB格式
    sws_scale(m_swsContext, frame->data, frame->linesize, 0, height,
              m_rgbFrame->data, m_rgbFrame->linesize);
    
    // 高效的QImage创建 - 复用缓存避免频繁分配
    if (m_cachedImage.size() != QSize(width, height) || m_cachedImage.format() != QImage::Format_RGB888) {
        m_cachedImage = QImage(width, height, QImage::Format_RGB888);
    }
    
    // 直接内存拷贝，最高性能
    memcpy(m_cachedImage.bits(), m_rgbFrame->data[0], width * height * 3);
    m_image = m_cachedImage;
    
    // 高帧率模式 - 提升到60fps获得流畅体验
    QTime currentTime = QTime::currentTime();
    if (!m_lastUpdateTime.isValid() || m_lastUpdateTime.msecsTo(currentTime) > 16) {
        update();
        m_lastUpdateTime = currentTime;
    }
}

void VideoWidget::clearFrame()
{
    QMutexLocker locker(&m_mutex);
    m_image = QImage();
    update();
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_image.isNull()) {
        // 快速绘制提示文字
        QPainter painter(this);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No Video");
        return;
    }
    
    // 高性能绘制设置
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false); // 禁用平滑变换提速
    
    // 优化的缩放计算 - 减少浮点运算
    const QSize widgetSize = size();
    const QSize imageSize = m_image.size();
    
    const int widgetW = widgetSize.width();
    const int widgetH = widgetSize.height();
    const int imageW = imageSize.width();
    const int imageH = imageSize.height();
    
    // 使用整数运算提高性能
    int scaledWidth, scaledHeight;
    if (widgetW * imageH > widgetH * imageW) {
        // 以高度为基准
        scaledHeight = widgetH;
        scaledWidth = (imageW * widgetH) / imageH;
    } else {
        // 以宽度为基准
        scaledWidth = widgetW;
        scaledHeight = (imageH * widgetW) / imageW;
    }
    
    // 居中显示
    const int x = (widgetW - scaledWidth) / 2;
    const int y = (widgetH - scaledHeight) / 2;
    
    // 高效绘制
    painter.drawImage(QRect(x, y, scaledWidth, scaledHeight), m_image);
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    
    // 只有在尺寸显著改变时才重绘，提高拖拽性能
    QSize oldSize = event->oldSize();
    QSize newSize = event->size();
    if (oldSize.isValid() && 
        abs(oldSize.width() - newSize.width()) < 10 && 
        abs(oldSize.height() - newSize.height()) < 10) {
        return; // 跳过小变化
    }
    
    // 快速响应resize - 提高窗口操作流畅度
    QTime currentTime = QTime::currentTime();
    if (m_lastUpdateTime.isValid() && m_lastUpdateTime.msecsTo(currentTime) < 33) {
        return; // 限制在30fps以平衡性能和响应性
    }
    
    update();
    m_lastUpdateTime = currentTime;
}

void VideoWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        QList<QUrl> urlList = event->mimeData()->urls();
        if (!urlList.isEmpty()) {
            QString filePath = urlList.first().toLocalFile();
            QString suffix = QFileInfo(filePath).suffix().toLower();
            
            // 检查是否为支持的视频格式
            QStringList supportedFormats = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp", "ts", "m2ts"};
            if (supportedFormats.contains(suffix)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void VideoWidget::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        if (!urlList.isEmpty()) {
            QString filePath = urlList.first().toLocalFile();
            emit videoFileDropped(filePath);
        }
    }
}
