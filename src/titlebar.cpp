/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "titlebar.h"
//
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

TitleBar::TitleBar(QWidget *window, bool withMinMax, QWidget *parent)
    : QWidget(parent), window_(window), withMinMax_(withMinMax) {
	setFixedHeight(34);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto *lay = new QHBoxLayout(this);
	lay->setContentsMargins(10, 0, 10, 0);
	lay->setSpacing(8);

	// 左侧：窗口图标 + 窗口标题
	iconLabel_ = new QLabel(this);
	iconLabel_->setFixedSize(18, 18);

	if (! window_->windowIcon().isNull())
		iconLabel_->setPixmap(window_->windowIcon().pixmap(16, 16));

	lay->addWidget(iconLabel_);
	titleLabel_ = new QLabel(window_->windowTitle(), this);
	titleLabel_->setObjectName(QStringLiteral("TitleBarText"));
	lay->addWidget(titleLabel_);
	lay->addStretch();

	// 右侧：macOS 风格圆点（黄最小化 / 绿最大化 / 红关闭）
	if (withMinMax_) {
		minBtn_ = makeDot(QStringLiteral("TbMin"), QStringLiteral("–"), QStringLiteral("#FBBF24"),
		                  QStringLiteral("#D69E0B"));
		maxBtn_ = makeDot(QStringLiteral("TbMax"), QStringLiteral("+"), QStringLiteral("#34D399"),
		                  QStringLiteral("#0EA47A"));
		lay->addWidget(minBtn_);
		lay->addWidget(maxBtn_);
	}

	closeBtn_ = makeDot(QStringLiteral("TbClose"), QStringLiteral("×"), QStringLiteral("#F87171"),
	                    QStringLiteral("#DC2626"));
	lay->addWidget(closeBtn_);

	if (minBtn_)
		connect(minBtn_, &QPushButton::clicked, window_, &QWidget::showMinimized);

	if (maxBtn_) {
		connect(maxBtn_, &QPushButton::clicked, this, [this]() {
			// 最大化 / 还原切换
			if (window_->isMaximized())
				window_->showNormal();
			else
				window_->showMaximized();
		});
	}

	connect(closeBtn_, &QPushButton::clicked, window_, &QWidget::close);

	// 同步窗口标题 / 图标的变化
	window_->installEventFilter(this);
}

// 生成圆点按钮的内联样式表（免疫外部 QSS 级联，保证永远是正圆）
static QString dotStyleSheet(const QString &color, const QString &pressed, bool hover) {
	return QStringLiteral(
	           "QPushButton{background:%1;border:none;border-radius:7px;padding:0;margin:0;"
	           "color:%2;font-size:9px;font-weight:700;}"
	           "QPushButton:pressed{background:%3;}")
	    .arg(color, hover ? QStringLiteral("rgba(0,0,0,0.55)") : QStringLiteral("transparent"), pressed);
}

// 创建一个 14px 圆形窗控按钮：常态只显示纯色圆点，悬停圆点组时浮现符号
QPushButton *TitleBar::makeDot(const QString &objectName, const QString &symbol, const QString &color,
                               const QString &pressed) {
	auto *btn = new QPushButton(symbol, this);
	btn->setObjectName(objectName);
	btn->setFixedSize(14, 14);
	btn->setFocusPolicy(Qt::NoFocus);
	btn->setProperty("dotColor", color);
	btn->setProperty("dotPressed", pressed);
	btn->setStyleSheet(dotStyleSheet(color, pressed, false));
	return btn;
}

// 悬停圆点组时浮现符号：直接重写各圆点的内联样式表
void TitleBar::setHover(bool on) {
	if (hover_ == on)
		return;

	hover_ = on;
	const auto btns = findChildren<QPushButton *>();

	for (auto *b : btns)
		b->setStyleSheet(
		    dotStyleSheet(b->property("dotColor").toString(), b->property("dotPressed").toString(), on));
}

bool TitleBar::event(QEvent *e) {
	// 用 event() 处理 Enter/Leave，兼容 Qt5/Qt6 的 enterEvent 签名差异
	if (e->type() == QEvent::Enter)
		setHover(true);
	else if (e->type() == QEvent::Leave)
		setHover(false);

	return QWidget::event(e);
}

bool TitleBar::eventFilter(QObject *obj, QEvent *e) {
	if (obj == window_) {
		if (e->type() == QEvent::WindowTitleChange)
			titleLabel_->setText(window_->windowTitle());
		else if (e->type() == QEvent::WindowIconChange && ! window_->windowIcon().isNull())
			iconLabel_->setPixmap(window_->windowIcon().pixmap(16, 16));
	}

	return QWidget::eventFilter(obj, e);
}

void TitleBar::mousePressEvent(QMouseEvent *e) {
	// 左键按下且窗口非最大化时记录拖动起点（最大化时拖动直接忽略）
	if (e->button() == Qt::LeftButton && ! window_->isMaximized()) {
		dragging_ = true;
		dragOffset_ = e->globalPosition().toPoint() - window_->frameGeometry().topLeft();
	}

	QWidget::mousePressEvent(e);
}

void TitleBar::mouseMoveEvent(QMouseEvent *e) {
	if (dragging_ && (e->buttons() & Qt::LeftButton)) {
		if (window_->isMaximized())
			return;

		window_->move(e->globalPosition().toPoint() - dragOffset_);
	}

	QWidget::mouseMoveEvent(e);
}

void TitleBar::mouseReleaseEvent(QMouseEvent *e) {
	dragging_ = false;
	QWidget::mouseReleaseEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *e) {
	// 双击标题栏切换最大化 / 还原
	if (e->button() == Qt::LeftButton && withMinMax_) {
		if (window_->isMaximized())
			window_->showNormal();
		else
			window_->showMaximized();
	}

	QWidget::mouseDoubleClickEvent(e);
}

void installTitleBar(QWidget *w, bool withMinMax) {
	if (! w)
		return;

	// 保留窗口类型位（Dialog / Window），仅去掉原生边框
	auto f = w->windowFlags();
	w->setWindowFlags((f & Qt::WindowType_Mask) | Qt::FramelessWindowHint);
	auto *tb = new TitleBar(w, withMinMax, w);
	tb->setObjectName(QStringLiteral("MainTitleBar"));

	if (auto *mw = qobject_cast<QMainWindow *>(w)) {
		// 主窗口：menuWidget 会替换菜单栏区域，所以用一个容器装「标题栏 + 原菜单栏」，
		// 保证原菜单栏保留在标题栏下方
		auto *container = new QWidget(mw);
		auto *v = new QVBoxLayout(container);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(0);
		v->addWidget(tb);
		auto *mb = mw->menuBar();
		mb->setParent(container);
		v->addWidget(mb);
		mw->setMenuWidget(container);
		return;
	}

	// 对话框：插到顶层布局最上方
	if (auto *vbox = qobject_cast<QVBoxLayout *>(w->layout())) {
		vbox->insertWidget(0, tb);
	} else if (w->layout()) {
		// 其他布局（如 QGridLayout）：用 QLayout::setMenuBar 兜底放到布局上方
		w->layout()->setMenuBar(tb);
	}
}
