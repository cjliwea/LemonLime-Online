/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QFrame>
#include <QString>
#include <QStringList>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QMouseEvent;
class QPaintEvent;

// 圆角虚线拖放区：把文件夹 / 测试点文件拖进来，或点它一下打开选择框
class DropArea : public QFrame {
	Q_OBJECT

  public:
	explicit DropArea(QWidget *parent = nullptr);

	void setHint(const QString &title, const QString &subtitle);

	// 从拖拽数据里取出本地路径
	static QStringList localPaths(const QMimeData *mime);

  signals:
	void pathsDropped(const QStringList &paths);
	void activated();

  protected:
	void dragEnterEvent(QDragEnterEvent *) override;
	void dragMoveEvent(QDragMoveEvent *) override;
	void dragLeaveEvent(QDragLeaveEvent *) override;
	void dropEvent(QDropEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void paintEvent(QPaintEvent *) override;

  private:
	QString hintTitle_;
	QString hintSubtitle_;
	bool hover_{};
};
