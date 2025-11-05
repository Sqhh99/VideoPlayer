#include <QApplication>
#include "VideoPlayer.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    VideoPlayer player;
    player.show();
    
    // 如果有命令行参数，自动打开视频或网络视频
    if (argc > 1) {
        QString input = QString::fromLocal8Bit(argv[1]);
        if (player.isNetworkUrl(input)) {
            player.openNetworkVideo(input);
        } else {
            player.openVideo(input);
        }
    }
    
    return app.exec();
} 