/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droparea.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUrl>

DropArea::DropArea(QWidget *parent) : QFrame(parent) {
	setAcceptDrops(true);
	setMinimumHeight(132);
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void DropArea::setHint(const QString &title, const QString &subtitle) {
	hintTitle_ = title;
	hintSubtitle_ = subtitle;
	update();
}

auto DropArea::localPaths(const QMimeData *mime) -> QStringList {
	QStringList paths;

	if (! mime || ! mime->hasUrls())
		return paths;

	const QList<QUrl> urls = mime->urls();

	for (const QUrl &url : urls) {
		const QString local = url.toLocalFile();

		if (! local.isEmpty())
			paths.append(local);
	}

	return paths;
}

void DropArea::dragEnterEvent(QDragEnterEvent *event) {
	if (localPaths(event->mimeData()).isEmpty()) {
		event->ignore();
		return;
	}

	hover_ = true;
	update();
	event->acceptProposedAction();
}

void DropArea::dragMoveEvent(QDragMoveEvent *event) { event->acceptProposedAction(); }

void DropArea::dragLeaveEvent(QDragLeaveEvent *event) {
	hover_ = false;
	update();
	event->accept();
}

void DropArea::dropEvent(QDropEvent *event) {
	hover_ = false;
	update();

	const QStringList paths = localPaths(event->mimeData());

	if (paths.isEmpty())
		return;

	event->acceptProposedAction();
	emit pathsDropped(paths);
}

void DropArea::mousePressEvent(QMouseEvent *event) {
	QFrame::mousePressEvent(event);

	if (event->button() == Qt::LeftButton)
		emit activated();
}

void DropArea::paintEvent(QPaintEvent *event) {
	QFrame::paintEvent(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QRectF box = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);

	QPen pen(hover_ ? QColor(0x84, 0xCC, 0x16) : QColor(0xCB, 0xD5, 0xE1));
	pen.setWidthF(2.0);
	pen.setStyle(Qt::DashLine);
	painter.setPen(pen);
	painter.setBrush(hover_ ? QColor(0xF7, 0xFE, 0xE7) : QColor(0xFC, 0xFD, 0xFE));
	painter.drawRoundedRect(box, 12.0, 12.0);

	const QRectF textRect = box.adjusted(18.0, 16.0, -18.0, -16.0);

	painter.setPen(QColor(0x1E, 0x29, 0x3B));
	QFont titleFont = font();
	titleFont.setBold(true);
	titleFont.setPointSizeF(font().pointSizeF() + 1.5);
	painter.setFont(titleFont);

	QRectF titleRect = textRect;
	titleRect.setHeight(textRect.height() * 0.5);
	painter.drawText(titleRect, int(Qt::AlignCenter) | int(Qt::TextWordWrap), hintTitle_);

	painter.setFont(font());
	painter.setPen(QColor(0x64, 0x74, 0x8B));

	QRectF subtitleRect = textRect;
	subtitleRect.setTop(titleRect.bottom());
	painter.drawText(subtitleRect, int(Qt::AlignHCenter) | int(Qt::AlignTop) | int(Qt::TextWordWrap),
	                 hintSubtitle_);
}
