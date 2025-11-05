#ifndef NETWORKSTREAMUI_H
#define NETWORKSTREAMUI_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>

class NetworkStreamUI : public QDialog
{
    Q_OBJECT

public:
    struct StreamSettings {
        QString url;
        int timeout;
        int bufferSize;
        bool autoReconnect;
        int maxRetries;
    };

    explicit NetworkStreamUI(QWidget *parent = nullptr);
    ~NetworkStreamUI();

    QString getUrl() const;
    StreamSettings getSettings() const;
    void setUrl(const QString &url);
    void setStatus(const QString &status);
    void setProgress(int value);
    void setConnecting(bool connecting);
    
    // 自动关闭功能
    void startAutoCloseTimer(int timeoutMs = 10000);  // 默认10秒
    void stopAutoCloseTimer();

signals:
    void connectRequested(const StreamSettings &settings);

private slots:
    void onConnectClicked();
    bool validateInput();
    void onAutoCloseTimeout();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupUI();
    void setupConnections();

    // UI 组件
    QLineEdit* m_urlEdit;
    QTimer* m_autoCloseTimer;
};

#endif // NETWORKSTREAMUI_H
