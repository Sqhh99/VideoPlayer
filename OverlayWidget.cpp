#include "OverlayWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

OverlayWidget::OverlayWidget(QWidget *parent)
    : QWidget(nullptr)  // 方案C: 不设置父窗口，成为独立窗口
    , m_contentLabel(nullptr)
    , m_layout(nullptr)
    , m_autoHideTimer(nullptr)
    , m_updateTimer(nullptr)
    , m_isVisible(false)
    , m_parentWidget(parent)  // 保存父窗口引用用于坐标转换
    , m_isTemporaryHidden(false)
    , m_updateCallback(nullptr)
{
    setupUI();
}

OverlayWidget::~OverlayWidget()
{
    if (m_autoHideTimer) {
        m_autoHideTimer->stop();
    }
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void OverlayWidget::setupUI()
{
    // 方案C: 设置为独立窗口，与主窗口保持统一层级
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setObjectName("overlayWidget");  // 设置对象名称用于CSS选择器
    hide();
    
    // 创建内容标签
    m_contentLabel = new QLabel(this);
    m_contentLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_contentLabel->setWordWrap(true);
    m_contentLabel->setTextFormat(Qt::RichText);
    
    // 创建布局
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(16, 15, 16, 15);  // 默认边距
    m_layout->addWidget(m_contentLabel);
    
    // 设置样式 - 只设置文字样式，背景通过paintEvent绘制
    setOverlayStyle(
        "#overlayWidget {"
        "    background: transparent;"  // 透明背景，让paintEvent绘制的背景显示
        "}"
        "QLabel {"
        "    background: transparent;"
        "    border: none;"
        "    color: rgba(255, 255, 255, 230);"
        "    margin: 0px;"
        "    padding: 0px;"
        "    spacing: 0px;"
        "}"
    );
    
    // 创建自动隐藏定时器
    m_autoHideTimer = new QTimer(this);
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout, this, &OverlayWidget::onAutoHideTimeout);
    
    // 创建实时更新定时器
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(1000);  // 每秒更新一次
    connect(m_updateTimer, &QTimer::timeout, this, &OverlayWidget::onUpdateTimeout);
}

void OverlayWidget::setContent(const QString &content)
{
    if (m_contentLabel) {
        m_contentLabel->setText(content);
    }
}

void OverlayWidget::showOverlay(int x, int y, int width, int height, int autoHideMs)
{
    // 方案C: 坐标转换 - 将相对坐标转换为全局坐标
    QPoint globalPos(x, y);
    if (m_parentWidget) {
        globalPos = m_parentWidget->mapToGlobal(QPoint(x, y));
    }
    
    // 设置位置和尺寸
    setGeometry(globalPos.x(), globalPos.y(), width, height);
    
    // 显示覆盖层
    show();
    raise();
    
    // 启动自动隐藏定时器
    if (autoHideMs > 0) {
        m_autoHideTimer->start(autoHideMs);
    }
    
    // 启动实时更新定时器
    if (m_updateCallback) {
        m_updateTimer->start();
    }
    
    m_isVisible = true;
}

void OverlayWidget::hideOverlay()
{
    // 停止定时器
    if (m_autoHideTimer) {
        m_autoHideTimer->stop();
    }
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
    
    // 隐藏覆盖层
    hide();
    m_isVisible = false;
    m_isTemporaryHidden = false;  // 重置临时隐藏状态
    
    // 发送隐藏信号
    emit overlayHidden();
}

void OverlayWidget::updateOverlayGeometry(int x, int y, int width, int height)
{
    // 方案C: 坐标转换 - 将相对坐标转换为全局坐标  
    QPoint globalPos(x, y);
    if (m_parentWidget) {
        globalPos = m_parentWidget->mapToGlobal(QPoint(x, y));
    }
    
    // 只更新几何形状，不影响定时器
    setGeometry(globalPos.x(), globalPos.y(), width, height);
}

void OverlayWidget::temporaryHide()
{
    if (m_isVisible && !m_isTemporaryHidden) {
        m_isTemporaryHidden = true;
        if (m_updateTimer) {
            m_updateTimer->stop();
        }
        hide();
    }
}

void OverlayWidget::restoreFromTemporaryHide()
{
    if (m_isVisible && m_isTemporaryHidden) {
        m_isTemporaryHidden = false;
        show();
        raise();
        if (m_updateCallback && m_updateTimer) {
            m_updateTimer->start();
        }
    }
}

void OverlayWidget::setOverlayStyle(const QString &styleSheet)
{
    setStyleSheet(styleSheet);
}

void OverlayWidget::setMargins(int left, int top, int right, int bottom)
{
    if (m_layout) {
        m_layout->setContentsMargins(left, top, right, bottom);
    }
}

void OverlayWidget::onAutoHideTimeout()
{
    hideOverlay();
}

void OverlayWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制圆角半透明背景
    QPainterPath path;
    path.addRoundedRect(rect(), 8, 8);  // 8px圆角
    
    // 设置半透明深色背景
    QColor backgroundColor(16, 16, 16, 235);  // RGBA: 深灰色，92%不透明度
    painter.fillPath(path, backgroundColor);
    
    // 绘制边框
    QPen borderPen(QColor(255, 255, 255, 20));  // 半透明白色边框
    borderPen.setWidth(1);
    painter.setPen(borderPen);
    painter.drawPath(path);
}

void OverlayWidget::enableRealTimeUpdate(bool enable)
{
    if (enable && m_updateCallback && m_isVisible && !m_isTemporaryHidden) {
        m_updateTimer->start();
    } else {
        m_updateTimer->stop();
    }
}

void OverlayWidget::setUpdateCallback(std::function<QString()> callback)
{
    m_updateCallback = callback;
}

void OverlayWidget::onUpdateTimeout()
{
    if (m_updateCallback && m_isVisible && !m_isTemporaryHidden) {
        QString newContent = m_updateCallback();
        setContent(newContent);
    }
}

