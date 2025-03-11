#pragma once

#include <QThread>

class CustomThread : public QThread
{
	Q_OBJECT
public:
	CustomThread();
	~CustomThread();

	/**
	 * @brief	线程运行入口（纯虚函数）
	 */
	virtual void run() = 0;

	/**
	 * @brief	启动线程
	 *
	 * @return	true 成功 false 失败
	 */
	bool StartThread();

	/**
	* @brief	停止线程
	*
	* @return	true 成功 false 失败
	*/
	bool StopThread();

protected:
	bool m_bRunning;    ///< 线程运行标志位};
};
