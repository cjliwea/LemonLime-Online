/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QPoint>
#include <QWidget>

class QLabel;
class QPushButton;

// 自绘标题栏（macOS 红绿灯窗控），配合 Qt::FramelessWindowHint 使用
class TitleBar : public QWidget {
	Q_OBJECT

  public:
	explicit TitleBar(QWidget *window, bool withMinMax, QWidget *parent = nullptr);

  protected:
	bool event(QEvent *) override;
	bool eventFilter(QObject *, QEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;

  private:
	QWidget *window_{};   // 被控制的顶层窗口
	bool withMinMax_{};   // 是否带最小化 / 最大化按钮
	bool dragging_{};     // 是否正在拖动窗口
	bool hover_{};        // 鼠标是否悬停在标题栏上
	QPoint dragOffset_;   // 拖动偏移（按下点相对窗口左上角）
	QLabel *iconLabel_{};
	QLabel *titleLabel_{};
	QPushButton *minBtn_{};
	QPushButton *maxBtn_{};
	QPushButton *closeBtn_{};

	QPushButton *makeDot(const QString &, const QString &, const QString &, const QString &); // 创建圆形窗控按钮
	void setHover(bool); // 切换悬停状态并重写圆点内联样式
};

// 便捷函数：把窗口 w 改为无边框并装上自绘标题栏
// 主窗口（QMainWindow）装到 menuWidget 位置；对话框插入顶层布局最上方
void installTitleBar(QWidget *w, bool withMinMax);
