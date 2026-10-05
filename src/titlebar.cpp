/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "titlebar.h"
//
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibrary>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace {

// Win11 起支持给无边框窗口加系统级圆角：
//   DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE = 33, &DWMWCP_ROUND = 2, sizeof(int))
// 用 QLibrary 动态解析，这样低版本 Windows 上只是不生效，不会链接失败。
void applyRoundedCorners(QWidget *window) {
	using SetWindowAttributeFn = long (*)(void *, unsigned long, const void *, unsigned long);

	static QLibrary dwmapi(QStringLiteral("dwmapi"));
	static SetWindowAttributeFn setWindowAttribute = nullptr;
	static bool resolved = false;

	if (! resolved) {
		resolved = true;

		if (dwmapi.load())
			setWindowAttribute =
			    reinterpret_cast<SetWindowAttributeFn>(dwmapi.resolve("DwmSetWindowAttribute"));
	}

	if (! setWindowAttribute || ! window)
		return;

	const int preference = 2; // DWMWCP_ROUND
	setWindowAttribute(reinterpret_cast<void *>(window->winId()), 33, &preference, sizeof(preference));
}

// 窗口是否处于最大化：无边框窗口下 Qt 与系统状态偶尔不同步，
// 这里把三种来源取「或」，避免“按了还原却没反应”。
bool windowIsMaximized(QWidget *window) {
	if (! window)
		return false;

	if (window->isMaximized())
		return true;

	if (window->windowState() & Qt::WindowMaximized)
		return true;

	if (auto *handle = window->windowHandle())
		return (handle->windowState() & Qt::WindowMaximized);

	return false;
}

// 取窗口所在屏幕的可用区域（排除任务栏）
QRect availableGeometryFor(QWidget *window) {
	if (! window)
		return {};

	if (auto *handle = window->windowHandle()) {
		if (auto *screen = handle->screen())
			return screen->availableGeometry();
	}

	if (auto *screen = window->screen())
		return screen->availableGeometry();

	if (auto *screen = QGuiApplication::primaryScreen())
		return screen->availableGeometry();

	return {};
}

// 还原尺寸不能顶满屏幕：如果窗口本身就是「整屏大小的普通窗口」，
// 最大化 / 还原在视觉上完全没有区别，按钮看起来就像失效了。
// 这里把还原尺寸限制在屏幕可用区域的 86% 以内，保证一定是一个中等窗口。
void clampToMediumSize(QWidget *window, QRect &geometry) {
	const QRect available = availableGeometryFor(window);

	if (! available.isValid())
		return;

	const int limitWidth = qMax(800, int(available.width() * 0.86));
	const int limitHeight = qMax(560, int(available.height() * 0.86));
	const int width = qMin(geometry.width(), limitWidth);
	const int height = qMin(geometry.height(), limitHeight);

	if (width != geometry.width() || height != geometry.height()) {
		geometry.setSize(QSize(width, height));
		geometry.moveCenter(available.center());
	} else if (! available.intersects(geometry)) {
		geometry.moveCenter(available.center());
	}
}

// 无边框窗口默认失去了用鼠标拖边框缩放的能力，这里补上：
// 应用级事件过滤器监听边框附近的移动 / 按下，用 Qt 几何计算改变窗口大小。
class WindowBorderResizer : public QObject {
  public:
	explicit WindowBorderResizer(QWidget *window) : QObject(window), window_(window) {
		if (auto *app = QCoreApplication::instance())
			app->installEventFilter(this);
	}

  protected:
	bool eventFilter(QObject *watched, QEvent *event) override {
		const QEvent::Type type = event->type();

		// 应用级过滤器会收到整个程序的所有事件，先按类型快速挡掉
		if (type != QEvent::MouseMove && type != QEvent::MouseButtonPress &&
		    type != QEvent::MouseButtonRelease)
			return false;

		if (! window_ || ! window_->isVisible())
			return false;

		auto *widget = qobject_cast<QWidget *>(watched);

		if (! widget || widget->window() != window_)
			return false;

		switch (type) {
			case QEvent::MouseMove: {
				auto *mouseEvent = static_cast<QMouseEvent *>(event);

				if (resizing_) {
					// 收不到 release（例如鼠标被别的窗口抢走）时自动收尾，避免卡在缩放状态
					if (! (mouseEvent->buttons() & Qt::LeftButton))
						resizing_ = false;
					else {
						applyResize(mouseEvent->globalPosition().toPoint());
						return true;
					}
				}

				updateCursor(mouseEvent->globalPosition().toPoint());
				break;
			}

			case QEvent::MouseButtonPress: {
				auto *mouseEvent = static_cast<QMouseEvent *>(event);

				if (mouseEvent->button() != Qt::LeftButton)
					break;

				const Qt::Edges edges = edgesAt(mouseEvent->globalPosition().toPoint());

				if (edges == Qt::Edges())
					break;

				resizing_ = true;
				edges_ = edges;
				startGeometry_ = window_->geometry();
				startPosition_ = mouseEvent->globalPosition().toPoint();
				return true; // 吃掉这次按下，别让贴边的控件被误触发
			}

			case QEvent::MouseButtonRelease:
				if (resizing_) {
					resizing_ = false;
					return true;
				}

				break;

			default:
				break;
		}

		return false;
	}

  private:
	QWidget *window_{};
	bool resizing_{};
	Qt::Edges edges_{};
	QRect startGeometry_;
	QPoint startPosition_;
	Qt::Edges cursorEdges_{};
	static const int borderWidth_ = 6; // 边框感应宽度（像素）

	// 光标落在窗口四条边的哪几条边上（角落会同时命中两条）
	Qt::Edges edgesAt(const QPoint &globalPosition) const {
		if (! window_ || windowIsMaximized(window_) || window_->isFullScreen())
			return {};

		const QRect rect = window_->frameGeometry();
		Qt::Edges edges;

		if (globalPosition.x() >= rect.left() && globalPosition.x() <= rect.right()) {
			if (globalPosition.y() >= rect.top() && globalPosition.y() <= rect.top() + borderWidth_)
				edges |= Qt::TopEdge;

			if (globalPosition.y() <= rect.bottom() &&
			    globalPosition.y() >= rect.bottom() - borderWidth_)
				edges |= Qt::BottomEdge;
		}

		if (globalPosition.y() >= rect.top() && globalPosition.y() <= rect.bottom()) {
			if (globalPosition.x() >= rect.left() && globalPosition.x() <= rect.left() + borderWidth_)
				edges |= Qt::LeftEdge;

			if (globalPosition.x() <= rect.right() &&
			    globalPosition.x() >= rect.right() - borderWidth_)
				edges |= Qt::RightEdge;
		}

		return edges;
	}

	static QCursor cursorFor(Qt::Edges edges) {
		const bool left = edges.testFlag(Qt::LeftEdge);
		const bool right = edges.testFlag(Qt::RightEdge);
		const bool top = edges.testFlag(Qt::TopEdge);
		const bool bottom = edges.testFlag(Qt::BottomEdge);

		if ((left && top) || (right && bottom))
			return QCursor(Qt::SizeFDiagCursor);

		if ((right && top) || (left && bottom))
			return QCursor(Qt::SizeBDiagCursor);

		if (left || right)
			return QCursor(Qt::SizeHorCursor);

		if (top || bottom)
			return QCursor(Qt::SizeVerCursor);

		return QCursor(Qt::ArrowCursor);
	}

	// 光标样式只改顶层窗口，不动子控件的：子控件自己设过光标就仍然用自己的，
	// 也不会因为这里 unsetCursor 把输入框的 I 形光标弄丢。
	void updateCursor(const QPoint &globalPosition) {
		if (! window_ || resizing_)
			return;

		const Qt::Edges edges = edgesAt(globalPosition);

		if (edges == cursorEdges_)
			return;

		cursorEdges_ = edges;

		if (edges == Qt::Edges())
			window_->unsetCursor();
		else
			window_->setCursor(cursorFor(edges));
	}

	void applyResize(const QPoint &globalPosition) {
		if (! window_)
			return;

		QRect geometry = startGeometry_;
		const QPoint delta = globalPosition - startPosition_;
		const int minWidth = qMax(480, window_->minimumSize().width());
		const int minHeight = qMax(320, window_->minimumSize().height());

		if (edges_.testFlag(Qt::LeftEdge)) {
			const int left = geometry.left() + delta.x();

			if (geometry.right() - left + 1 >= minWidth)
				geometry.setLeft(left);
		}

		if (edges_.testFlag(Qt::RightEdge)) {
			const int right = geometry.right() + delta.x();

			if (right - geometry.left() + 1 >= minWidth)
				geometry.setRight(right);
		}

		if (edges_.testFlag(Qt::TopEdge)) {
			const int top = geometry.top() + delta.y();

			if (geometry.bottom() - top + 1 >= minHeight)
				geometry.setTop(top);
		}

		if (edges_.testFlag(Qt::BottomEdge)) {
			const int bottom = geometry.bottom() + delta.y();

			if (bottom - geometry.top() + 1 >= minHeight)
				geometry.setBottom(bottom);
		}

		window_->setGeometry(geometry);
	}
};

} // namespace

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

	if (minBtn_) {
		minBtn_->setToolTip(tr("Minimize"));
		connect(minBtn_, &QPushButton::clicked, window_, &QWidget::showMinimized);
	}

	if (maxBtn_) {
		maxBtn_->setToolTip(tr("Maximize"));
		connect(maxBtn_, &QPushButton::clicked, this, &TitleBar::toggleMaximize);
	}

	closeBtn_->setToolTip(tr("Close"));
	connect(closeBtn_, &QPushButton::clicked, window_, &QWidget::close);

	// 同步窗口标题 / 图标 / 最大化状态的变化
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
		else if (e->type() == QEvent::Show)
			applyRoundedCorners(window_);
		else if (e->type() == QEvent::WindowStateChange)
			syncMaxState();
	}

	return QWidget::eventFilter(obj, e);
}

// 同步绿点提示：最大化时告诉用户这个按钮现在是「还原」
void TitleBar::syncMaxState() {
	if (! maxBtn_)
		return;

	const bool maximized = windowIsMaximized(window_) || window_->isFullScreen();
	maxBtn_->setToolTip(maximized ? tr("Restore Down") : tr("Maximize"));
}

// 回到中等窗口：先把窗口从最大化 / 全屏状态还原，再套用「不超过屏幕 86%」的尺寸
bool TitleBar::restoreToMedium(bool settleLater) {
	if (! window_)
		return false;

	QRect geometry = normalGeometry_;

	if (! geometry.isValid() || geometry.width() <= 0 || geometry.height() <= 0)
		geometry = window_->normalGeometry();

	if (! geometry.isValid() || geometry.width() <= 0 || geometry.height() <= 0)
		geometry = window_->geometry();

	// 全屏窗口没有 normalGeometry，直接用屏幕可用区域作基准
	if (window_->isFullScreen())
		geometry = availableGeometryFor(window_);

	window_->showNormal();
	clampToMediumSize(window_, geometry);
	window_->setGeometry(geometry);

	// 平台窗口管理器可能在 showNormal() 之后才落定尺寸，延迟再校正一次。
	// 从标题栏拖动还原时不能这么做：那会把用户刚拖到的位置顶回居中位置。
	if (settleLater) {
		QPointer<QWidget> guard(window_);
		QTimer::singleShot(0, window_, [guard, geometry]() {
			if (guard && ! windowIsMaximized(guard))
				guard->setGeometry(geometry);
		});
	}

	return true;
}

void TitleBar::toggleMaximize() {
	if (! window_)
		return;

	if (window_->isFullScreen()) {
		window_->showNormal();
		syncMaxState();
		return;
	}

	if (windowIsMaximized(window_))
		restoreToMedium();
	else {
		normalGeometry_ = window_->geometry();
		window_->showMaximized();
	}

	syncMaxState();
}

void TitleBar::mousePressEvent(QMouseEvent *e) {
	// 只处理左键；最大化状态下拖动 = 先还原成中等窗口，再把窗口贴到光标下
	if (e->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(e);
		return;
	}

	const QPoint globalPosition = e->globalPosition().toPoint();

	if (windowIsMaximized(window_)) {
		const qreal ratio = width() > 0 ? qreal(e->position().x()) / qreal(width()) : 0.5;
		restoreToMedium(false); // 拖动还原：不要再延迟校正位置，用户手动接管
		const QSize restored = window_->size();
		window_->move(globalPosition.x() - int(ratio * restored.width()),
		              globalPosition.y() - int(e->position().y()));
	}

	dragging_ = true;
	dragOffset_ = globalPosition - window_->frameGeometry().topLeft();
	QWidget::mousePressEvent(e);
}

void TitleBar::mouseMoveEvent(QMouseEvent *e) {
	if (dragging_ && (e->buttons() & Qt::LeftButton)) {
		if (windowIsMaximized(window_))
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
		dragging_ = false;
		toggleMaximize();
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
	} else if (auto *vbox = qobject_cast<QVBoxLayout *>(w->layout())) {
		// 对话框：插到顶层布局最上方
		vbox->insertWidget(0, tb);
	} else if (w->layout()) {
		// 其他布局（如 QGridLayout）：用 QLayout::setMenuBar 兜底放到布局上方
		w->layout()->setMenuBar(tb);
	}

	// 无边框窗口丢掉的原生缩放能力，用自绘的边框拖拽补回来
	new WindowBorderResizer(w);

	applyRoundedCorners(w);
}
