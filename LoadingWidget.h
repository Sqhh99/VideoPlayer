#ifndef LOADINGWIDGET_H
#define LOADINGWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QMovie>
#include <QTimer>

class LoadingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingWidget(QWidget *parent = nullptr);
    ~LoadingWidget();
    
    void showLoading();
    void hideLoading();
    bool isLoading() const { return m_isVisible; }
    
    void updatePosition();  // 更新到右下角位置

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setupUI();
    void positionToBottomRight();
    void setupCSSLoadingAnimation();  // CSS动画备用方案
    
    QLabel *m_gifLabel;
    QMovie *m_loadingMovie;
    bool m_isVisible;
    QWidget *m_parentWidget;
    QTimer *m_cssAnimationTimer;  // CSS动画定时器
};

#endif // LOADINGWIDGET_H 