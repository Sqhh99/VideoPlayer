#include "NetworkStreamUI.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QSpacerItem>
#include <QApplication>
#include <QStyle>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QShowEvent>
#include <QMouseEvent>
#include <QDebug>

NetworkStreamUI::NetworkStreamUI(QWidget *parent)
    : QDialog(parent)
    , m_urlEdit(new QLineEdit(this))
    , m_autoCloseTimer(new QTimer(this))
{
    setWindowTitle("网络流");
    setFixedSize(500, 50);
    setModal(false);  // 改为非模态，允许点击外部区域
    
    // 设置为子窗口，跟随父窗口层级，不独立置顶
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    
    // 确保跟随父窗口
    if (parent) {
        setParent(parent);
    }
    
    setupUI();
    setupConnections();
    validateInput();
    
    // 设置自动关闭定时器
    m_autoCloseTimer->setSingleShot(true);
    connect(m_autoCloseTimer, &QTimer::timeout, this, &NetworkStreamUI::onAutoCloseTimeout);
    
    // 安装全局事件过滤器来检测外部点击
    qApp->installEventFilter(this);
}

NetworkStreamUI::~NetworkStreamUI()
{
    // 移除全局事件过滤器
    qApp->removeEventFilter(this);
}

void NetworkStreamUI::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // URL输入框
    m_urlEdit->setPlaceholderText("输入流媒体地址");
    m_urlEdit->setStyleSheet(
        "QLineEdit { "
        "border: 1px solid #4a5568; "
        "border-radius: 6px; "
        "padding: 12px 16px; "
        "font-size: 14px; "
        "font-family: 'Segoe UI', sans-serif; "
        "background: #2d3748; "
        "color: #e2e8f0; "
        "selection-background-color: #4299e1; "
        "} "
        "QLineEdit:focus { "
        "border: 1px solid #4299e1; "
        "background: #2d3748; "
        "outline: none; "
        "} "
        "QLineEdit:hover { "
        "border: 1px solid #718096; "
        "}"
    );
    
    mainLayout->addWidget(m_urlEdit);
    
    // 设置输入框为焦点
    m_urlEdit->setFocus();
    
    // 整体窗口样式
    setStyleSheet(
        "QDialog { "
        "background: #2d3748; "
        "border: 1px solid #4a5568; "
        "border-radius: 6px; "
        "}"
    );
}

void NetworkStreamUI::setupConnections()
{
    // 回车键触发连接
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &NetworkStreamUI::onConnectClicked);
    
    connect(m_urlEdit, &QLineEdit::textChanged, this, &NetworkStreamUI::validateInput);
}

void NetworkStreamUI::setUrl(const QString &url)
{
    m_urlEdit->setText(url);
    validateInput();
}

QString NetworkStreamUI::getUrl() const
{
    return m_urlEdit->text().trimmed();
}

NetworkStreamUI::StreamSettings NetworkStreamUI::getSettings() const
{
    StreamSettings settings;
    settings.url = getUrl();
    settings.timeout = 30;
    settings.bufferSize = 10;
    settings.autoReconnect = true;
    settings.maxRetries = 5;
    return settings;
}

void NetworkStreamUI::setStatus(const QString &status)
{
    // 状态显示功能保留但不显示UI
}

void NetworkStreamUI::setProgress(int value)
{
    // 进度显示功能保留但不显示UI
}

void NetworkStreamUI::setConnecting(bool connecting)
{
    m_urlEdit->setEnabled(!connecting);
    
    if (connecting) {
        m_urlEdit->setPlaceholderText("正在连接...");
    } else {
        m_urlEdit->setPlaceholderText("输入流媒体地址");
    }
}

void NetworkStreamUI::onConnectClicked()
{
    if (validateInput()) {
        // 停止自动关闭定时器
        stopAutoCloseTimer();
        
        // 立即关闭对话框
        accept();
        
        // 发送连接请求信号
        emit connectRequested(getSettings());
    }
}

bool NetworkStreamUI::validateInput()
{
    QString url = m_urlEdit->text().trimmed();
    bool valid = !url.isEmpty() && (
        url.startsWith("rtmp://") ||
        url.startsWith("rtsp://") ||
        url.startsWith("http://") ||
        url.startsWith("https://") ||
        url.startsWith("udp://") ||
        url.startsWith("tcp://") ||
        url.contains("://")
    );
    
    if (!valid && !url.isEmpty()) {
        m_urlEdit->setStyleSheet(
            "QLineEdit { "
            "border: 1px solid #f56565; "
            "border-radius: 6px; "
            "padding: 12px 16px; "
            "font-size: 14px; "
            "font-family: 'Segoe UI', sans-serif; "
            "background: #2d3748; "
            "color: #e2e8f0; "
            "selection-background-color: #4299e1; "
            "} "
            "QLineEdit:focus { "
            "border: 1px solid #f56565; "
            "background: #2d3748; "
            "outline: none; "
            "} "
            "QLineEdit:hover { "
            "border: 1px solid #f56565; "
            "}"
        );
    } else {
        m_urlEdit->setStyleSheet(
            "QLineEdit { "
            "border: 1px solid #4a5568; "
            "border-radius: 6px; "
            "padding: 12px 16px; "
            "font-size: 14px; "
            "font-family: 'Segoe UI', sans-serif; "
            "background: #2d3748; "
            "color: #e2e8f0; "
            "selection-background-color: #4299e1; "
            "} "
            "QLineEdit:focus { "
            "border: 1px solid #4299e1; "
            "background: #2d3748; "
            "outline: none; "
            "} "
            "QLineEdit:hover { "
            "border: 1px solid #718096; "
            "}"
        );
    }
    
    return valid;
}

void NetworkStreamUI::startAutoCloseTimer(int timeoutMs)
{
    m_autoCloseTimer->start(timeoutMs);
    // Auto-close timer started
}

void NetworkStreamUI::stopAutoCloseTimer()
{
    if (m_autoCloseTimer->isActive()) {
        m_autoCloseTimer->stop();
        // Auto-close timer stopped
    }
}

void NetworkStreamUI::onAutoCloseTimeout()
{
    qDebug() << "Auto-close timeout - closing dialog";
    reject();  // 自动关闭，使用reject表示超时
}

void NetworkStreamUI::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        qDebug() << "ESC pressed - closing dialog";
        stopAutoCloseTimer();
        reject();
        return;
    }
    
    // 对于其他按键，重置自动关闭定时器
    if (m_autoCloseTimer->isActive()) {
        startAutoCloseTimer();  // 重新开始计时
    }
    
    QDialog::keyPressEvent(event);
}

void NetworkStreamUI::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event)
    
    // 使用Qt::Popup窗口类型时，失去焦点会自动关闭
    // 这里不需要额外处理，让Qt自动处理
    
    QDialog::focusOutEvent(event);
}

void NetworkStreamUI::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    
    // 居中显示在父窗口中
    if (parentWidget()) {
        QRect parentRect = parentWidget()->geometry();
        QPoint center = parentRect.center();
        move(center.x() - width() / 2, center.y() - height() / 2);
    }
    
    // 对话框显示时启动自动关闭定时器
    startAutoCloseTimer();
    
    // 确保输入框获得焦点
    m_urlEdit->setFocus();
    m_urlEdit->selectAll();  // 选中所有文本，方便覆盖输入
}

bool NetworkStreamUI::eventFilter(QObject *obj, QEvent *event)
{
    // 只在对话框可见时处理事件
    if (!isVisible()) {
        return QDialog::eventFilter(obj, event);
    }
    
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        
        // 检查点击是否在对话框外部
        QPoint globalPos = mouseEvent->globalPosition().toPoint();
        QRect dialogRect = QRect(mapToGlobal(QPoint(0, 0)), size());
        
        if (!dialogRect.contains(globalPos)) {
            qDebug() << "Mouse click outside dialog - closing";
            stopAutoCloseTimer();
            reject();
            return true;  // 事件已处理
        }
    }
    
    return QDialog::eventFilter(obj, event);
}

