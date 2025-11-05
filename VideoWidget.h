#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMutex>
#include <QImage>
#include <QTime>
#include <QDragEnterEvent>
#include <QDropEvent>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
}

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

    void displayFrame(AVFrame* frame, int width, int height);
    void clearFrame();

signals:
    void videoFileDropped(const QString &filePath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QImage m_image;
    QImage m_cachedImage; // 缓存QImage避免频繁分配
    QMutex m_mutex;
    SwsContext* m_swsContext;
    uint8_t* m_rgbBuffer;
    AVFrame* m_rgbFrame;
    int m_videoWidth;
    int m_videoHeight;
    QTime m_lastUpdateTime; // 重绘节流
    
    void setupSwsContext(int width, int height);
    void cleanupSwsContext();
};

#endif // VIDEOWIDGET_H