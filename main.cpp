#include "Player.h"
#include <QApplication>
#include <QFontDatabase>
#include <QDebug>
#include <QtWidgets/QApplication>
extern "C" {
#pragma comment (lib, "avcodec.lib")
#pragma comment (lib, "avdevice.lib")
#pragma comment (lib, "avfilter.lib")
#pragma comment (lib, "avformat.lib")
#pragma comment (lib, "avutil.lib")
#pragma comment (lib, "swresample.lib")
#pragma comment (lib, "swscale.lib")
#include <libavutil/avutil.h>
}
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
	//使用第三方字库，用来作为UI图片
	QFontDatabase::addApplicationFont(":/Player/res/fontawesome-webfont.ttf");
	//QFontDatabase::addApplicationFont(":/Player/res/fa-solid-900.ttf");
    Player w;
	if (w.Init() == false)
	{
		return -1;
	}
    w.show();
    return a.exec();
}
