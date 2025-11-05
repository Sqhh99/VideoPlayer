#ifndef OVERLAYWIDGET_H
#define OVERLAYWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QString>
#include <functional>

class OverlayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OverlayWidget(QWidget *parent = nullptr);
    ~OverlayWidget();
    
    // 设置内容
    void setContent(const QString &content);
    
    // 显示/隐藏控制
    void showOverlay(int x, int y, int width, int height, int autoHideMs = 10000);
    void hideOverlay();
    bool isOverlayVisible() const { return m_isVisible; }
    
    // 更新几何位置（用于resize事件，不重新启动定时器）
    void updateOverlayGeometry(int x, int y, int width, int height);
    
    // 临时隐藏/恢复（用于避免覆盖系统对话框）
    void temporaryHide();
    void restoreFromTemporaryHide();
    

    
    // 实时更新功能
    void enableRealTimeUpdate(bool enable = true);
    void setUpdateCallback(std::function<QString()> callback);
    
    // 样式设置
    void setOverlayStyle(const QString &styleSheet);
    void setMargins(int left, int top, int right, int bottom);

signals:
    void overlayHidden();

private slots:
    void onAutoHideTimeout();
    void onUpdateTimeout();  // 实时更新定时器槽函数

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setupUI();
    
    QLabel *m_contentLabel;

    QVBoxLayout *m_layout;
    QTimer *m_autoHideTimer;
    QTimer *m_updateTimer;   // 实时更新定时器
    bool m_isVisible;
    QWidget *m_parentWidget;  // 保存父窗口引用用于坐标转换
    bool m_isTemporaryHidden; // 临时隐藏状态
    std::function<QString()> m_updateCallback;  // 更新回调函数
};

#endif // OVERLAYWIDGET_H 