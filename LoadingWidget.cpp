#include "LoadingWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QApplication>

LoadingWidget::LoadingWidget(QWidget *parent)
    : QWidget(nullptr)  // 独立窗口
    , m_gifLabel(nullptr)
    , m_loadingMovie(nullptr)
    , m_isVisible(false)
    , m_parentWidget(parent)
    , m_cssAnimationTimer(new QTimer(this))
{
    setupUI();
}

LoadingWidget::~LoadingWidget()
{
    if (m_loadingMovie) {
        m_loadingMovie->stop();
        delete m_loadingMovie;
    }
}

void LoadingWidget::setupUI()
{
    // 设置为无边框工具窗口
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);  // 鼠标穿透
    setFixedSize(64, 64);  // 固定大小
    hide();
    
    // 创建GIF标签
    m_gifLabel = new QLabel(this);
    m_gifLabel->setAlignment(Qt::AlignCenter);
    m_gifLabel->setGeometry(0, 0, 64, 64);
    
    // 确保标签能够显示动画
    m_gifLabel->setScaledContents(true);
    m_gifLabel->setAttribute(Qt::WA_TranslucentBackground, false);  // 确保有背景
    m_gifLabel->setStyleSheet("background: transparent;");
    
    // 尝试多种路径加载GIF动画
    QStringList possiblePaths = {
        ":/res/resource/loading.gif",
        ":/resource/loading.gif", 
        "resource/loading.gif",
        "./resource/loading.gif"
    };
    

    for (const QString &path : possiblePaths) {
        m_loadingMovie = new QMovie(path);
        if (m_loadingMovie->isValid()) {
            break;
        } else {
            delete m_loadingMovie;
            m_loadingMovie = nullptr;
        }
    }
    
    if (!m_loadingMovie || !m_loadingMovie->isValid()) {
        if (m_loadingMovie) {
            delete m_loadingMovie;
            m_loadingMovie = nullptr;
        }
        
        // 使用CSS动画作为备用方案
        setupCSSLoadingAnimation();
        return;
    }
    
    // 设置缓存模式和循环次数
    m_loadingMovie->setCacheMode(QMovie::CacheAll);
    m_loadingMovie->setSpeed(100);  // 正常速度
    
    m_loadingMovie->setScaledSize(QSize(48, 48));  // 缩放到合适大小
    
    // 设置到标签
    m_gifLabel->setMovie(m_loadingMovie);
}

void LoadingWidget::showLoading()
{
    if (m_isVisible) return;
    
    m_isVisible = true;
    
    // 定位到右下角
    positionToBottomRight();
    
    if (m_loadingMovie && m_loadingMovie->isValid()) {
        // 确保动画从第一帧开始
        m_loadingMovie->jumpToFrame(0);
        
        // 开始播放动画
        m_loadingMovie->start();
        
        // 强制更新显示
        m_gifLabel->update();
        
    } else {
        // 启动CSS动画
        if (m_cssAnimationTimer) {
            m_cssAnimationTimer->start();
        }
    }
    
    // 显示窗口
    show();
    raise();
    activateWindow();  // 确保窗口激活
    
    // 强制重绘整个窗口
    update();
    repaint();
}

void LoadingWidget::hideLoading()
{
    if (!m_isVisible) return;
    
    m_isVisible = false;
    
    // 停止动画
    if (m_loadingMovie) {
        m_loadingMovie->stop();
    }
    
    // 停止CSS动画
    if (m_cssAnimationTimer) {
        m_cssAnimationTimer->stop();
    }
    
    // 隐藏窗口
    hide();
}

void LoadingWidget::updatePosition()
{
    if (m_isVisible) {
        positionToBottomRight();
    }
}

void LoadingWidget::positionToBottomRight()
{
    if (!m_parentWidget) return;
    
    // 获取父窗口的全局位置和大小
    QPoint parentGlobalPos = m_parentWidget->mapToGlobal(QPoint(0, 0));
    QSize parentSize = m_parentWidget->size();
    
    // 计算右下角位置（留20px边距）
    int x = parentGlobalPos.x() + parentSize.width() - width() - 20;
    int y = parentGlobalPos.y() + parentSize.height() - height() - 20;
    
    // 移动到目标位置
    move(x, y);
}

void LoadingWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
    // 绘制半透明圆形背景
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制圆形背景
    QRect bgRect = rect().adjusted(8, 8, -8, -8);  // 缩小一点
    painter.setBrush(QColor(0, 0, 0, 120));  // 半透明黑色
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(bgRect);
}

void LoadingWidget::setupCSSLoadingAnimation()
{
    // 创建一个旋转的加载指示器
    m_gifLabel->setText("●");
    m_gifLabel->setAlignment(Qt::AlignCenter);
    m_gifLabel->setStyleSheet(
        "QLabel {"
        "    color: #4299e1;"
        "    font-size: 24px;"
        "    font-weight: bold;"
        "    background: transparent;"
        "}"
    );
    
    // 设置定时器来模拟旋转动画
    m_cssAnimationTimer->setInterval(100);  // 每100ms更新一次
    
    connect(m_cssAnimationTimer, &QTimer::timeout, [this]() {
        static int step = 0;
        static QStringList spinnerChars = {"●", "○", "◐", "◑", "◒", "◓"};
        
        m_gifLabel->setText(spinnerChars[step % spinnerChars.size()]);
        step++;
    });
} 