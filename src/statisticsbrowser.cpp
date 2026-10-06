/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "statisticsbrowser.h"
#include "ui_statisticsbrowser.h"
//
#include "base/LemonType.hpp"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/task.h"
//
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLinearGradient>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>

namespace {

	// ---------------------------------------------------------------- 配色（对齐预览版）
	const QColor kLime200(0xD9, 0xF9, 0x9D);
	const QColor kLime300(0xBE, 0xF2, 0x64);
	const QColor kLime400(0xA3, 0xE6, 0x35);
	const QColor kLime500(0x84, 0xCC, 0x16);
	const QColor kLime600(0x65, 0xA3, 0x0D);
	const QColor kLime950(0x1A, 0x2E, 0x05);
	const QColor kInk(0x0F, 0x17, 0x2A);
	const QColor kMuted(0x64, 0x74, 0x8B);
	const QColor kFaint(0x94, 0xA3, 0xB8);
	const QColor kBorder(0xE5, 0xE7, 0xEB);
	const QColor kBorderSoft(0xF1, 0xF5, 0xF9);
	const QColor kAmber(0xD9, 0x77, 0x06);
	const QColor kDanger(0xE2, 0x4B, 0x4A);

	const char *kDashboardQss =
	    "QWidget#StatRoot { background: #F8FAFC; }"
	    "QFrame#Card { background: #FFFFFF; border: 1px solid #E5E7EB; border-radius: 10px; }"
	    "QFrame#KpiCard { background: #FFFFFF; border: 1px solid #E5E7EB; border-radius: 12px; }"
	    "QFrame#RingCell { background: #F8FAFC; border: 1px solid #F1F5F9; border-radius: 10px; }"
	    "QFrame#PpStat { background: #F8FAFC; border: 0; border-radius: 8px; }"
	    "QLabel#PageTitle { font-size: 17px; font-weight: bold; color: #0F172A; }"
	    "QLabel#PageSub { font-size: 11px; color: #94A3B8; }"
	    "QLabel#PanelTitle { font-size: 13px; font-weight: bold; color: #0F172A; }"
	    "QLabel#PanelSub { font-size: 11px; color: #94A3B8; }"
	    "QLabel#SecTitle { font-size: 13px; font-weight: bold; color: #0F172A; }"
	    "QLabel#KL { font-size: 12px; color: #64748B; }"
	    "QLabel#KV { font-size: 25px; font-weight: bold; color: #1A2E05; }"
	    "QLabel#KS { font-size: 11px; color: #94A3B8; }"
	    "QLabel#PpBadge { font-size: 11px; font-weight: bold; color: #1A2E05; background: #ECFCCB;"
	    "  border: 1px solid #BEF264; border-radius: 6px; padding: 2px 8px; }"
	    "QLabel#PpName { font-size: 13px; font-weight: bold; color: #0F172A; }"
	    "QLabel#PpSrc { font-size: 11px; color: #94A3B8; font-family: Consolas, monospace; }"
	    "QLabel#PpLabel { font-size: 10px; color: #94A3B8; }"
	    "QLabel#PpValue { font-size: 15px; font-weight: bold; color: #0F172A; }"
	    "QLabel#PpValueOk { font-size: 15px; font-weight: bold; color: #65A30D; }"
	    "QLabel#PpValueBad { font-size: 15px; font-weight: bold; color: #E24B4A; }"
	    "QLabel#RingTitle { font-size: 12px; font-weight: bold; color: #0F172A; }"
	    "QLabel#RingSub { font-size: 11px; color: #94A3B8; }"
	    "QPushButton#SBtn { border: 1px solid #E5E7EB; background: #FFFFFF; border-radius: 6px;"
	    "  padding: 3px 10px; font-size: 12px; color: #475569; }"
	    "QPushButton#SBtn:hover { border-color: #84CC16; color: #3F6212; }"
	    "QTableWidget#Rank { border: 0; background: #FFFFFF; }"
	    "QTableWidget#Rank::item { border-bottom: 1px solid #F1F5F9; }"
	    "QTableWidget#Rank::item:hover { background: #FAFDF2; }"
	    "QHeaderView::section { background: #F8FAFC; color: #64748B; border: 0;"
	    "  border-bottom: 1px solid #E5E7EB; padding: 6px 8px; font-size: 12px; }"
	    "QTextBrowser#Detail { border: 0; background: #FFFFFF; }"
	    "QScrollArea#StatScroll { border: 0; background: transparent; }";

	// 清空布局（含嵌套布局），供每次刷新时重建动态内容
	void clearLayout(QLayout *layout) {
		while (auto *item = layout->takeAt(0)) {
			if (auto *w = item->widget())
				w->deleteLater();
			else if (auto *child = item->layout())
				clearLayout(child);
			delete item;
		}
	}

	// ---------------------------------------------------------------- KPI 图标（手绘，项目未链接 Qt6::Svg）
	enum KpiIcon { IconUsers = 0, IconBars, IconTrophy, IconUpload, IconCheck };

	void drawKpiIcon(QPainter &p, const QRectF &box, int kind) {
		p.save();
		p.setPen(QPen(kLime300, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		const qreal l = box.left(), t = box.top(), w = box.width(), h = box.height();
		switch (kind) {
			case IconUsers:
				p.drawEllipse(QRectF(l + w * 0.33, t, w * 0.34, h * 0.36));
				p.drawArc(QRectF(l + w * 0.13, t + h * 0.46, w * 0.74, h * 0.66), 0, 180 * 16);
				break;
			case IconBars:
				p.drawLine(QPointF(l + w * 0.16, t + h * 0.88), QPointF(l + w * 0.16, t + h * 0.30));
				p.drawLine(QPointF(l + w * 0.50, t + h * 0.90), QPointF(l + w * 0.50, t + h * 0.06));
				p.drawLine(QPointF(l + w * 0.84, t + h * 0.82), QPointF(l + w * 0.84, t + h * 0.48));
				break;
			case IconTrophy: {
				QPainterPath cup;
				cup.moveTo(l + w * 0.29, t + h * 0.10);
				cup.lineTo(l + w * 0.71, t + h * 0.10);
				cup.lineTo(l + w * 0.65, t + h * 0.56);
				cup.lineTo(l + w * 0.35, t + h * 0.56);
				cup.closeSubpath();
				p.drawPath(cup);
				p.drawLine(QPointF(l + w * 0.50, t + h * 0.56), QPointF(l + w * 0.50, t + h * 0.80));
				p.drawLine(QPointF(l + w * 0.30, t + h * 0.90), QPointF(l + w * 0.70, t + h * 0.90));
				p.drawArc(QRectF(l + w * 0.04, t + h * 0.12, w * 0.30, h * 0.34), 90 * 16, 180 * 16);
				p.drawArc(QRectF(l + w * 0.66, t + h * 0.12, w * 0.30, h * 0.34), -90 * 16, 180 * 16);
				break;
			}
			case IconUpload:
				p.drawLine(QPointF(l + w * 0.50, t + h * 0.74), QPointF(l + w * 0.50, t + h * 0.08));
				p.drawLine(QPointF(l + w * 0.28, t + h * 0.32), QPointF(l + w * 0.50, t + h * 0.08));
				p.drawLine(QPointF(l + w * 0.72, t + h * 0.32), QPointF(l + w * 0.50, t + h * 0.08));
				p.drawLine(QPointF(l + w * 0.12, t + h * 0.74), QPointF(l + w * 0.12, t + h * 0.92));
				p.drawLine(QPointF(l + w * 0.88, t + h * 0.74), QPointF(l + w * 0.88, t + h * 0.92));
				p.drawLine(QPointF(l + w * 0.12, t + h * 0.92), QPointF(l + w * 0.88, t + h * 0.92));
				break;
			default:
				p.drawEllipse(QRectF(l + w * 0.04, t + h * 0.04, w * 0.92, h * 0.92));
				p.drawLine(QPointF(l + w * 0.27, t + h * 0.52), QPointF(l + w * 0.45, t + h * 0.71));
				p.drawLine(QPointF(l + w * 0.45, t + h * 0.71), QPointF(l + w * 0.74, t + h * 0.31));
				break;
		}
		p.restore();
	}

	// ---------------------------------------------------------------- 卡片容器
	class StatPanel : public QFrame {
	  public:
		explicit StatPanel(const QString &title, const QString &sub = QString(), QWidget *parent = nullptr)
		    : QFrame(parent) {
			setObjectName(QStringLiteral("Card"));
			auto *v = new QVBoxLayout(this);
			v->setContentsMargins(13, 12, 14, 13);
			v->setSpacing(9);
			head_ = new QHBoxLayout();
			head_->setSpacing(8);
			subs_ = new QLabel(sub, this);
			subs_->setObjectName(QStringLiteral("PanelSub"));
			subs_->setVisible(! sub.isEmpty());
			if (! title.isEmpty()) {
				auto *t = new QLabel(title, this);
				t->setObjectName(QStringLiteral("PanelTitle"));
				head_->addWidget(t);
			}
			head_->addWidget(subs_);
			head_->addStretch(1);
			if (! title.isEmpty() || ! sub.isEmpty())
				v->addLayout(head_);
			body_ = new QVBoxLayout();
			body_->setSpacing(8);
			v->addLayout(body_, 1);
		}
		QHBoxLayout *head() const { return head_; }
		QVBoxLayout *body() const { return body_; }
		QLabel *subLabel() const { return subs_; }
		void setSub(const QString &text) {
			subs_->setText(text);
			subs_->setVisible(! text.isEmpty());
		}

	  private:
		QHBoxLayout *head_{};
		QVBoxLayout *body_{};
		QLabel *subs_{};
	};

	// ---------------------------------------------------------------- KPI 卡片（左侧渐变条 + 右上角图标）
} // namespace

class StatKpiCard : public QFrame {
  public:
	StatKpiCard(int icon, const QString &label, QWidget *parent = nullptr) : QFrame(parent), icon_(icon) {
		setObjectName(QStringLiteral("KpiCard"));
		setMinimumHeight(84);
		auto *v = new QVBoxLayout(this);
		v->setContentsMargins(17, 12, 30, 12);
		v->setSpacing(2);
		value_ = new QLabel(QStringLiteral("0"), this);
		value_->setObjectName(QStringLiteral("KV"));
		sub_ = new QLabel(this);
		sub_->setObjectName(QStringLiteral("KS"));
		auto *l = new QLabel(label, this);
		l->setObjectName(QStringLiteral("KL"));
		v->addWidget(l);
		v->addWidget(value_);
		v->addWidget(sub_);
	}
	void setValue(const QString &text) { value_->setText(text); }
	void setSub(const QString &text) { sub_->setText(text); }

  protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		QLinearGradient g(0, 0, 0, height());
		g.setColorAt(0, kLime400);
		g.setColorAt(1, kLime600);
		QPainterPath bar;
		bar.addRoundedRect(QRectF(0.5, 0.5, 12, height() - 1), 11, 11);
		p.save();
		p.setClipRect(QRectF(0, 0, 3, height()));
		p.setPen(Qt::NoPen);
		p.setBrush(g);
		p.drawPath(bar);
		p.restore();
		drawKpiIcon(p, QRectF(width() - 32, 13, 18, 18), icon_);
	}

  private:
	int icon_{};
	QLabel *value_{};
	QLabel *sub_{};
};

// ---------------------------------------------------------------- 柱状图（成绩分布）
class StatBarChart : public QWidget {
  public:
	explicit StatBarChart(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(228); }
	void setData(const QStringList &labels, const QList<double> &values, double maxValue) {
		labels_ = labels;
		values_ = values;
		maxValue_ = maxValue > 0 ? maxValue : 1;
		update();
	}
	void setEmptyText(const QString &text) { empty_ = text; }

  protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		if (values_.isEmpty()) {
			p.setPen(kFaint);
			p.drawText(rect(), Qt::AlignCenter, empty_);
			return;
		}
		QFont f = font();
		f.setPointSizeF(8.5);
		p.setFont(f);
		const QRectF r = QRectF(rect()).adjusted(38, 8, -10, -24);
		for (int i = 0; i <= 4; i++) {
			const qreal y = r.bottom() - r.height() * i / 4.0;
			p.setPen(i == 0 ? kBorder : kBorderSoft);
			p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
			p.setPen(kFaint);
			p.drawText(QRectF(0, y - 8, 34, 16), Qt::AlignRight | Qt::AlignVCenter,
			           QString::number(qRound(maxValue_ * i / 4.0)));
		}
		const int n = values_.size();
		const qreal slot = r.width() / n;
		const qreal barW = qMin<qreal>(46.0, slot * 0.55);
		const QFontMetrics fm(f);
		for (int i = 0; i < n; i++) {
			const double ratio = values_[i] / maxValue_;
			const qreal h = qMax<qreal>(0.0, r.height() * ratio);
			QColor c = kDanger;
			if (ratio >= 0.75)
				c = kLime600;
			else if (ratio >= 0.5)
				c = kLime500;
			else if (ratio >= 0.25)
				c = QColor(0xF5, 0x9E, 0x0B);
			p.setPen(Qt::NoPen);
			p.setBrush(c);
			p.drawRoundedRect(QRectF(r.left() + slot * i + (slot - barW) / 2.0, r.bottom() - h, barW, h), 6,
			                  6);
			p.setPen(kMuted);
			p.drawText(QRectF(r.left() + slot * i, r.bottom() + 4, slot, 16), Qt::AlignHCenter | Qt::AlignTop,
			           fm.elidedText(labels_.value(i), Qt::ElideRight, int(slot) - 2));
		}
	}

  private:
	QStringList labels_;
	QList<double> values_;
	double maxValue_{1};
	QString empty_ = QStringLiteral("暂无数据");
};

// ---------------------------------------------------------------- 折线图（提交时间线）
class StatLineChart : public QWidget {
  public:
	explicit StatLineChart(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(228); }
	void setEmptyText(const QString &text) { empty_ = text; }
	void setData(const QStringList &labels, const QList<double> &values) {
		labels_ = labels;
		values_ = values;
		update();
	}
	void clearData() {
		labels_.clear();
		values_.clear();
		update();
	}

  protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		if (values_.size() < 2) {
			p.setPen(kFaint);
			p.drawText(rect(), Qt::AlignCenter, empty_);
			return;
		}
		QFont f = font();
		f.setPointSizeF(8.5);
		p.setFont(f);
		const QRectF r = QRectF(rect()).adjusted(38, 8, -10, -24);
		double mx = 0;
		for (double v : values_)
			mx = qMax(mx, v);
		mx = qMax(1.0, std::ceil(mx / 5.0) * 5.0); // 取整到 5 的倍数，刻度更整齐
		for (int i = 0; i <= 4; i++) {
			const qreal y = r.bottom() - r.height() * i / 4.0;
			p.setPen(i == 0 ? kBorder : kBorderSoft);
			p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
			p.setPen(kFaint);
			p.drawText(QRectF(0, y - 8, 34, 16), Qt::AlignRight | Qt::AlignVCenter,
			           QString::number(qRound(mx * i / 4.0)));
		}
		const int n = values_.size();
		QPolygonF line;
		QPolygonF area;
		for (int i = 0; i < n; i++) {
			const QPointF pt(r.left() + r.width() * i / (n - 1), r.bottom() - r.height() * values_[i] / mx);
			line << pt;
			area << pt;
		}
		area << QPointF(r.right(), r.bottom()) << QPointF(r.left(), r.bottom());
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(132, 204, 22, 36));
		p.drawPolygon(area);
		p.setPen(QPen(kLime500, 2));
		p.setBrush(Qt::NoBrush);
		p.drawPolyline(line);
		p.setPen(Qt::NoPen);
		p.setBrush(kLime500);
		for (const auto &pt : line)
			p.drawEllipse(pt, 3, 3);
		p.setPen(kMuted);
		for (int i = 0; i < n; i++) {
			if (n > 5 && i % 2)
				continue;
			const qreal x = r.left() + r.width() * i / (n - 1);
			p.drawText(QRectF(x - 26, r.bottom() + 4, 52, 16), Qt::AlignHCenter | Qt::AlignTop,
			           labels_.value(i));
		}
	}

  private:
	QStringList labels_;
	QList<double> values_;
	QString empty_ = QStringLiteral("暂无数据");
};

namespace {

	// ---------------------------------------------------------------- 环形 AC 率
	class StatRingChart : public QWidget {
	  public:
		explicit StatRingChart(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(46, 46); }
		void setPercent(double pct) {
			pct_ = qBound(0.0, pct, 100.0);
			update();
		}

	  protected:
		void paintEvent(QPaintEvent *) override {
			QPainter p(this);
			p.setRenderHint(QPainter::Antialiasing);
			const qreal pen = 6.0;
			const QRectF rc = QRectF(rect()).adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2);
			p.setPen(QPen(kBorder, pen));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(rc);
			if (pct_ > 0) {
				p.setPen(QPen(kLime500, pen, Qt::SolidLine, Qt::FlatCap));
				p.drawArc(rc, 90 * 16, -int(360 * 16 * pct_ / 100.0));
			}
			QFont f = font();
			f.setPointSizeF(8.5);
			f.setBold(true);
			p.setFont(f);
			p.setPen(kLime950);
			p.drawText(rect(), Qt::AlignCenter, QString::number(qRound(pct_)) + "%");
		}

	  private:
		double pct_{};
	};

	// ---------------------------------------------------------------- 分数区间分布柱（题卡底部）
	class StatDistBars : public QWidget {
	  public:
		explicit StatDistBars(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(51); }
		void setBins(const QList<int> &bins, int fullScore) {
			bins_ = bins;
			fullScore_ = fullScore;
			update();
		}

	  protected:
		void paintEvent(QPaintEvent *) override {
			QPainter p(this);
			p.setRenderHint(QPainter::Antialiasing);
			int mx = 0;
			for (int v : bins_)
				mx = qMax(mx, v);
			const QRectF r(0, 0, width(), 34);
			const qreal slot = r.width() / 5.0;
			for (int i = 0; i < 5 && i < bins_.size(); i++) {
				const qreal h = (mx > 0 && bins_[i] > 0) ? qMax<qreal>(4.0, 30.0 * bins_[i] / mx) : 3.0;
				p.setPen(Qt::NoPen);
				p.setBrush(i == 4 ? kLime600 : kLime200);
				p.drawRoundedRect(QRectF(r.left() + slot * i + 1.5, r.bottom() - h, slot - 3, h), 3, 3);
			}
			QFont f = font();
			f.setPointSizeF(7.5);
			p.setFont(f);
			p.setPen(kFaint);
			for (int i = 0; i <= 5; i++)
				p.drawText(QRectF(slot * i - 12, 38, 24, 12), Qt::AlignCenter,
				           QString::number(fullScore_ * i / 5));
		}

	  private:
		QList<int> bins_;
		int fullScore_{};
	};

	// 题卡里的一个小统计格
	QWidget *statCell(const QString &label, const QString &value, bool ok, bool bad, QWidget *parent) {
		auto *cell = new QFrame(parent);
		cell->setObjectName(QStringLiteral("PpStat"));
		auto *v = new QVBoxLayout(cell);
		v->setContentsMargins(9, 7, 9, 7);
		v->setSpacing(1);
		auto *l = new QLabel(label, cell);
		l->setObjectName(QStringLiteral("PpLabel"));
		auto *val = new QLabel(value, cell);
		val->setObjectName(ok ? QStringLiteral("PpValueOk")
		                      : (bad ? QStringLiteral("PpValueBad") : QStringLiteral("PpValue")));
		v->addWidget(l);
		v->addWidget(val);
		return cell;
	}

} // namespace

// ---------------------------------------------------------------- 排名表：名次徽章 + 总分进度条
class StatRankDelegate : public QStyledItemDelegate {
  public:
	explicit StatRankDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
	void setMetrics(int totalColumn, int maxTotal) {
		totalColumn_ = totalColumn;
		maxTotal_ = maxTotal;
	}

	void paint(QPainter *painter, const QStyleOptionViewItem &option,
	           const QModelIndex &index) const override {
		const bool special = index.column() == 0 || index.column() == totalColumn_;
		if (! special) {
			QStyledItemDelegate::paint(painter, option, index);
			return;
		}
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		// 先按样式画单元格底/分隔线（清空文字，避免与自绘内容重叠）
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		opt.text.clear();
		(option.widget ? option.widget->style() : QApplication::style())
		    ->drawControl(QStyle::CE_ItemViewItem, &opt, painter, option.widget);
		const QRect r = option.rect;
		if (index.column() == 0) {
			const int rank = index.data(Qt::DisplayRole).toInt();
			QColor bg = kFaint;
			QColor fg = Qt::white;
			if (rank == 1)
				bg = kLime600;
			else if (rank == 2) {
				bg = kLime400;
				fg = kLime950;
			} else if (rank == 3)
				bg = kAmber;
			const QRectF badge(r.center().x() - 10.5, r.center().y() - 10.5, 21, 21);
			painter->setPen(Qt::NoPen);
			painter->setBrush(bg);
			painter->drawEllipse(badge);
			QFont f = option.font;
			f.setBold(true);
			painter->setFont(f);
			painter->setPen(fg);
			painter->drawText(badge, Qt::AlignCenter, QString::number(rank));
		} else {
			const int total = index.data(Qt::DisplayRole).toInt();
			const int pct = maxTotal_ > 0 ? qRound(100.0 * total / maxTotal_) : 0;
			QFont f = option.font;
			f.setBold(true);
			painter->setFont(f);
			painter->setPen(kInk);
			painter->drawText(QRect(r.left() + 8, r.top() + 2, r.width() - 16, 16),
			                  Qt::AlignLeft | Qt::AlignVCenter, QString::number(total));
			const QRectF track(r.left() + 8, r.bottom() - 10, r.width() - 16, 4);
			painter->setPen(Qt::NoPen);
			painter->setBrush(kBorderSoft);
			painter->drawRoundedRect(track, 2, 2);
			if (pct > 0) {
				QLinearGradient g(track.left(), 0, track.right(), 0);
				g.setColorAt(0, kLime400);
				g.setColorAt(1, kLime600);
				painter->setBrush(g);
				painter->drawRoundedRect(
				    QRectF(track.left(), track.top(), track.width() * pct / 100.0, track.height()), 2, 2);
			}
		}
		painter->restore();
	}

  private:
	int totalColumn_{1};
	int maxTotal_{};
};

StatisticsBrowser::StatisticsBrowser(QWidget *parent) : QWidget(parent), ui(new Ui::StatisticsBrowser) {
	ui->setupUi(this);
	curContest = nullptr;
}

StatisticsBrowser::~StatisticsBrowser() { delete ui; }

void StatisticsBrowser::setContest(Contest *contest) { curContest = contest; }

// ---------------------------------------------------------------- 仪表盘构建（首次刷新时调用）

void StatisticsBrowser::buildDashboard() {
	if (dashboardBuilt)
		return;
	dashboardBuilt = true;
	setStyleSheet(QLatin1String(kDashboardQss));

	auto *scroll = new QScrollArea(this);
	scroll->setObjectName(QStringLiteral("StatScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	auto *root = new QWidget();
	root->setObjectName(QStringLiteral("StatRoot"));
	root->setAttribute(Qt::WA_StyledBackground, true);
	auto *v = new QVBoxLayout(root);
	v->setContentsMargins(14, 14, 16, 18);
	v->setSpacing(14);

	// ---- 页头：标题 + 副标题 + 动作按钮
	auto *head = new QHBoxLayout();
	auto *titles = new QVBoxLayout();
	titles->setSpacing(3);
	auto *title = new QLabel(tr("比赛统计分析"), root);
	title->setObjectName(QStringLiteral("PageTitle"));
	summaryLabel_ = new QLabel(root);
	summaryLabel_->setObjectName(QStringLiteral("PageSub"));
	titles->addWidget(title);
	titles->addWidget(summaryLabel_);
	head->addLayout(titles);
	head->addStretch(1);
	auto *refreshBtn = new QPushButton(tr("刷新统计"), root);
	refreshBtn->setObjectName(QStringLiteral("SBtn"));
	connect(refreshBtn, &QPushButton::clicked, this, &StatisticsBrowser::refresh);
	auto *exportBtn = new QPushButton(tr("导出统计"), root);
	exportBtn->setObjectName(QStringLiteral("SBtn"));
	connect(exportBtn, &QPushButton::clicked, this,
	        [this]() { StatisticsBrowser::exportStatistics(this, curContest); });
	head->addWidget(refreshBtn, 0, Qt::AlignTop);
	head->addWidget(exportBtn, 0, Qt::AlignTop);
	v->addLayout(head);

	// ---- KPI 卡片行
	auto *kpiRow = new QHBoxLayout();
	kpiRow->setSpacing(12);
	const int icons[5] = {IconUsers, IconBars, IconTrophy, IconUpload, IconCheck};
	const char *labels[5] = {QT_TR_NOOP("参赛人数"), QT_TR_NOOP("平均总分"), QT_TR_NOOP("最高分"),
	                         QT_TR_NOOP("提交总数"), QT_TR_NOOP("满分题次")};
	for (int i = 0; i < 5; i++) {
		auto *card = new StatKpiCard(icons[i], tr(labels[i]), root);
		kpiCards_.append(card);
		kpiRow->addWidget(card, 1);
	}
	v->addLayout(kpiRow);

	// ---- 成绩分布 + 各题 AC 率
	auto *middle = new QHBoxLayout();
	middle->setSpacing(12);
	auto *scorePanel = new StatPanel(tr("成绩分布"), tr("按选手总分"), root);
	scoreChart_ = new StatBarChart(scorePanel);
	scoreChart_->setEmptyText(tr("暂无评测结果"));
	scorePanel->body()->addWidget(scoreChart_, 1);
	middle->addWidget(scorePanel, 155);
	auto *acPanel = new StatPanel(tr("各题 AC 率"), tr("满分人次 / 作答人次"), root);
	auto *acHost = new QWidget(acPanel);
	acRingGrid_ = new QGridLayout(acHost);
	acRingGrid_->setContentsMargins(0, 0, 0, 0);
	acRingGrid_->setSpacing(10);
	acRingGrid_->setColumnStretch(0, 1);
	acRingGrid_->setColumnStretch(1, 1);
	acPanel->body()->addWidget(acHost, 1);
	middle->addWidget(acPanel, 100);
	v->addLayout(middle);

	// ---- 各题分析
	auto *sec = new QLabel(tr("各题分析"), root);
	sec->setObjectName(QStringLiteral("SecTitle"));
	v->addWidget(sec);
	auto *problemHost = new QWidget(root);
	problemGrid_ = new QGridLayout(problemHost);
	problemGrid_->setContentsMargins(0, 0, 0, 0);
	problemGrid_->setSpacing(12);
	problemGrid_->setColumnStretch(0, 1);
	problemGrid_->setColumnStretch(1, 1);
	v->addWidget(problemHost);

	// ---- 提交时间线
	auto *tlPanel = new StatPanel(tr("提交时间线"), QString(), root);
	timelineSub_ = tlPanel->subLabel();
	timelineChart_ = new StatLineChart(tlPanel);
	timelineChart_->setEmptyText(tr("暂无在线提交记录（时间线数据来自 online_submissions.log）"));
	tlPanel->body()->addWidget(timelineChart_, 1);
	v->addWidget(tlPanel);

	// ---- 选手排名
	auto *rankPanel = new StatPanel(tr("选手排名"), tr("总分降序 · 含各题得分"), root);
	rankTable_ = new QTableWidget(rankPanel);
	rankTable_->setObjectName(QStringLiteral("Rank"));
	rankTable_->setShowGrid(false);
	rankTable_->setFrameShape(QFrame::NoFrame);
	rankTable_->verticalHeader()->setVisible(false);
	rankTable_->setSelectionMode(QAbstractItemView::NoSelection);
	rankTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	rankTable_->setFocusPolicy(Qt::NoFocus);
	rankTable_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	rankTable_->setWordWrap(false);
	rankDelegate_ = new StatRankDelegate(this);
	rankTable_->setItemDelegate(rankDelegate_);
	rankPanel->body()->addWidget(rankTable_, 1);
	v->addWidget(rankPanel);

	// ---- 详细数据（保留原有统计表，避免功能丢失）
	auto *detailPanel = new StatPanel(tr("详细数据"), tr("分数段分布 / 逐题明细 / 测试点明细"), root);
	detailBrowser_ = new QTextBrowser(detailPanel);
	detailBrowser_->setObjectName(QStringLiteral("Detail"));
	detailBrowser_->setFixedHeight(360);
	detailBrowser_->setOpenExternalLinks(true);
	detailPanel->body()->addWidget(detailBrowser_, 1);
	v->addWidget(detailPanel);
	v->addStretch(1); // 窗口再高也保持各卡片自然高度，多余空白留在底部

	scroll->setWidget(root);
	if (auto *box = qobject_cast<QVBoxLayout *>(ui->verticalLayout_2))
		box->addWidget(scroll, 1);
}

// ---------------------------------------------------------------- 刷新

void StatisticsBrowser::refresh() {
	buildDashboard();

	QList<Task *> taskList;
	QList<Contestant *> contestantList;
	int totalScore = 0;
	if (curContest) {
		taskList = curContest->getTaskList();
		contestantList = curContest->getContestantList();
		totalScore = curContest->getTotalScore();
	}

	if (! curContest || taskList.isEmpty() || contestantList.isEmpty()) {
		summaryLabel_->setText(tr("尚无可统计的比赛数据"));
		for (auto *card : kpiCards_) {
			card->setValue(QStringLiteral("—"));
			card->setSub(QString());
		}
		scoreChart_->setData(QStringList{}, QList<double>{}, 0);
		clearLayout(acRingGrid_);
		clearLayout(problemGrid_);
		timelineChart_->clearData();
		rankTable_->clear();
		rankTable_->setRowCount(0);
		rankTable_->setColumnCount(0);
		rankTable_->setFixedHeight(60);
		timelineSub_->setText(QString());
		detailBrowser_->setHtml(buildDetailHtml(curContest));
		return;
	}

	summaryLabel_->setText(tr("基于当前评测结果 · %1 · %2 题 · 满分 %3")
	                           .arg(curContest->getContestTitle())
	                           .arg(taskList.size())
	                           .arg(totalScore));

	// ---- 逐选手汇总
	struct PlayerRow {
		QString name;
		QList<int> raw; // 未评测为 -1
		int total{};
		int rank{};
		bool active{};
	};
	QList<PlayerRow> rows;
	for (auto *c : contestantList) {
		PlayerRow row;
		row.name = c->getContestantName();
		for (int j = 0; j < taskList.size(); j++) {
			const int sc = c->getTaskScore(j);
			row.raw.append(sc);
			if (sc >= 0) {
				row.total += sc;
				row.active = true;
			}
		}
		rows.append(row);
	}
	std::sort(rows.begin(), rows.end(),
	          [](const PlayerRow &a, const PlayerRow &b) { return a.total > b.total; });
	for (int i = 0; i < rows.size(); i++) {
		int rank = 1;
		for (const auto &o : rows)
			if (o.total > rows[i].total)
				rank++;
		rows[i].rank = rank;
	}

	// ---- 逐题统计
	struct TaskStat {
		QString title;
		QString source;
		int full{};
		int judged{};
		int solved{};
		int zero{};
		int best{};
		int worst{};
		double average{};
		double acRate{};
		QList<int> bins{0, 0, 0, 0, 0};
	};
	QList<TaskStat> stats;
	int submittedCount = 0;
	int solvedCount = 0;
	int zeroCount = 0;
	for (int j = 0; j < taskList.size(); j++) {
		TaskStat ts;
		ts.title = taskList[j]->getProblemTitle();
		ts.source = taskList[j]->getSourceFileName();
		ts.full = taskList[j]->getTotalScore();
		long long sum = 0;
		for (auto *c : contestantList) {
			if (c->getCompileState(j) != NoValidSourceFile && c->getCompileState(j) != NoValidGraderFile)
				submittedCount++;
			const int sc = c->getTaskScore(j);
			if (sc < 0)
				continue;
			ts.judged++;
			sum += sc;
			ts.best = qMax(ts.best, sc);
			ts.worst = ts.judged == 1 ? sc : qMin(ts.worst, sc);
			if (ts.full > 0 && sc >= ts.full) {
				ts.solved++;
				solvedCount++;
			}
			if (sc == 0) {
				ts.zero++;
				zeroCount++;
			}
			const int bucket = ts.full > 0 ? qBound(0, sc * 5 / ts.full, 4) : 4;
			ts.bins[bucket]++;
		}
		ts.average = ts.judged > 0 ? 1.0 * sum / ts.judged : 0.0;
		ts.acRate = ts.judged > 0 ? 100.0 * ts.solved / ts.judged : 0.0;
		stats.append(ts);
	}

	// ---- KPI
	int activeCount = 0;
	long long activeSum = 0;
	int bestTotal = -1;
	QString bestName;
	for (const auto &row : rows) {
		if (! row.active)
			continue;
		activeCount++;
		activeSum += row.total;
		if (row.total > bestTotal) {
			bestTotal = row.total;
			bestName = row.name;
		}
	}
	const double avgTotal = activeCount > 0 ? 1.0 * activeSum / activeCount : 0.0;
	kpiCards_[0]->setValue(QString::number(contestantList.size()));
	kpiCards_[0]->setSub(
	    tr("%1 人有效作答 · %2 人未作答").arg(activeCount).arg(contestantList.size() - activeCount));
	kpiCards_[1]->setValue(QString::number(qRound(avgTotal)));
	kpiCards_[1]->setSub(
	    totalScore > 0
	        ? tr("满分 %1 · %2%").arg(totalScore).arg(QString::number(100.0 * avgTotal / totalScore, 'f', 1))
	        : QString());
	kpiCards_[2]->setValue(bestTotal >= 0 ? QString::number(bestTotal) : QStringLiteral("—"));
	kpiCards_[2]->setSub(bestName);
	kpiCards_[3]->setValue(QString::number(submittedCount));
	kpiCards_[3]->setSub(submittedCount > 0
	                         ? tr("AC 提交 %1 · %2%")
	                               .arg(solvedCount)
	                               .arg(QString::number(100.0 * solvedCount / submittedCount, 'f', 1))
	                         : QString());
	kpiCards_[4]->setValue(QString::number(solvedCount));
	kpiCards_[4]->setSub(tr("零分题次 %1").arg(zeroCount));

	// ---- 成绩分布（按选手总分降序取前 12 人）
	QStringList barLabels;
	QList<double> barValues;
	for (int i = 0; i < rows.size() && i < 12; i++) {
		barLabels << rows[i].name;
		barValues << rows[i].total;
	}
	scoreChart_->setData(barLabels, barValues, totalScore > 0 ? totalScore : 1);

	// ---- 各题 AC 率环形
	clearLayout(acRingGrid_);
	for (int i = 0; i < stats.size(); i++) {
		auto *cell = new QFrame();
		cell->setObjectName(QStringLiteral("RingCell"));
		auto *h = new QHBoxLayout(cell);
		h->setContentsMargins(10, 8, 10, 8);
		h->setSpacing(10);
		auto *ring = new StatRingChart(cell);
		ring->setPercent(stats[i].acRate);
		h->addWidget(ring);
		auto *info = new QVBoxLayout();
		info->setSpacing(1);
		auto *name = new QLabel(QStringLiteral("T%1 · %2").arg(i + 1).arg(stats[i].title), cell);
		name->setObjectName(QStringLiteral("RingTitle"));
		auto *sub = new QLabel(tr("满分 %1 / %2 人").arg(stats[i].solved).arg(stats[i].judged), cell);
		sub->setObjectName(QStringLiteral("RingSub"));
		info->addWidget(name);
		info->addWidget(sub);
		h->addLayout(info, 1);
		acRingGrid_->addWidget(cell, i / 2, i % 2);
	}

	// ---- 各题分析卡片
	clearLayout(problemGrid_);
	for (int i = 0; i < stats.size(); i++) {
		const auto &st = stats[i];
		auto *card = new StatPanel(QString(), QString());
		auto *head = new QHBoxLayout();
		head->setSpacing(9);
		auto *badge = new QLabel(QStringLiteral("T%1").arg(i + 1));
		badge->setObjectName(QStringLiteral("PpBadge"));
		auto *name = new QLabel(st.title);
		name->setObjectName(QStringLiteral("PpName"));
		auto *src = new QLabel(QStringLiteral("(%1)").arg(st.source));
		src->setObjectName(QStringLiteral("PpSrc"));
		head->addWidget(badge);
		head->addWidget(name);
		head->addStretch(1);
		head->addWidget(src);
		card->body()->addLayout(head);

		auto *cells = new QGridLayout();
		cells->setSpacing(8);
		cells->addWidget(statCell(tr("平均分"), QString::number(st.average, 'f', 1), false, false, card), 0,
		                 0);
		cells->addWidget(
		    statCell(tr("AC 率"), QString::number(st.acRate, 'f', 1) + "%", st.acRate > 0, false, card), 0,
		    1);
		cells->addWidget(statCell(tr("满分"), tr("%1 人").arg(st.solved), st.solved > 0, false, card), 0, 2);
		cells->addWidget(statCell(tr("最高分"), QString::number(st.best), false, false, card), 1, 0);
		cells->addWidget(statCell(tr("最低分"), QString::number(st.worst), false, st.worst == 0, card), 1, 1);
		cells->addWidget(statCell(tr("零分"), tr("%1 人").arg(st.zero), false, st.zero > 0, card), 1, 2);
		card->body()->addLayout(cells);

		auto *dist = new StatDistBars(card);
		dist->setBins(st.bins, st.full);
		card->body()->addWidget(dist);
		problemGrid_->addWidget(card, i / 2, i % 2);
	}

	// ---- 提交时间线：数据来自在线提交审计日志
	QList<QDateTime> submits;
	QFile log(QDir(QDir::currentPath()).filePath(QStringLiteral("online_submissions.log")));
	if (log.open(QFile::ReadOnly | QFile::Text)) {
		while (! log.atEnd()) {
			const QString line = QString::fromUtf8(log.readLine());
			if (! line.contains(QLatin1String("task=")))
				continue;
			const auto parts = line.split(QLatin1Char('\t'));
			const auto dt = QDateTime::fromString(parts.value(0), Qt::ISODateWithMs);
			if (dt.isValid())
				submits.append(dt);
		}
	}
	if (submits.size() >= 2) {
		std::sort(submits.begin(), submits.end());
		const int bins = qMin(12, submits.size());
		const qint64 span = submits.first().msecsTo(submits.last());
		QStringList labels;
		QList<double> values;
		for (int i = 0; i < bins; i++) {
			const qint64 upto = span * (i + 1) / bins;
			int count = 0;
			for (const auto &dt : submits)
				if (submits.first().msecsTo(dt) <= upto)
					count++;
			labels << submits.first().addMSecs(upto).toString(QStringLiteral("hh:mm"));
			values << count;
		}
		timelineChart_->setData(labels, values);
		timelineSub_->setText(tr("累计 %1 次在线提交").arg(submits.size()));
	} else {
		timelineChart_->clearData();
		timelineSub_->setText(QString());
	}

	// ---- 选手排名
	const int taskCount = taskList.size();
	const int totalColumn = 2 + taskCount;
	rankTable_->clear();
	rankTable_->setColumnCount(3 + taskCount + 1); // 排名 + 选手 + 各题 + 总分 + 百分比
	QStringList headers;
	headers << tr("排名") << tr("选手");
	for (int j = 0; j < taskCount; j++)
		headers << QStringLiteral("T%1").arg(j + 1);
	headers << tr("总分") << tr("百分比");
	rankTable_->setHorizontalHeaderLabels(headers);
	rankTable_->setRowCount(rows.size());
	rankDelegate_->setMetrics(totalColumn, totalScore > 0 ? totalScore : (bestTotal > 0 ? bestTotal : 1));

	for (int i = 0; i < rows.size(); i++) {
		const auto &row = rows[i];
		auto *rankItem = new QTableWidgetItem(QString::number(row.rank));
		rankItem->setTextAlignment(Qt::AlignCenter);
		rankTable_->setItem(i, 0, rankItem);
		rankTable_->setItem(i, 1, new QTableWidgetItem(row.name));
		for (int j = 0; j < taskCount; j++) {
			const int sc = row.raw.value(j, -1);
			auto *item = new QTableWidgetItem(sc < 0 ? QStringLiteral("—") : QString::number(sc));
			item->setTextAlignment(Qt::AlignCenter);
			if (sc < 0)
				item->setForeground(kFaint);
			else if (taskList[j]->getTotalScore() > 0 && sc >= taskList[j]->getTotalScore())
				item->setForeground(kLime600);
			else if (sc == 0)
				item->setForeground(kDanger);
			rankTable_->setItem(i, 2 + j, item);
		}
		auto *totalItem = new QTableWidgetItem(QString::number(row.total));
		rankTable_->setItem(i, totalColumn, totalItem);
		const double pct = totalScore > 0 ? 100.0 * row.total / totalScore : 0.0;
		auto *pctItem = new QTableWidgetItem(QString::number(pct, 'f', 1) + "%");
		pctItem->setTextAlignment(Qt::AlignCenter);
		rankTable_->setItem(i, totalColumn + 1, pctItem);
	}

	auto *header = rankTable_->horizontalHeader();
	header->setSectionResizeMode(0, QHeaderView::Fixed);
	header->resizeSection(0, 60);
	header->setSectionResizeMode(1, QHeaderView::Stretch);
	for (int j = 0; j < taskCount; j++) {
		header->setSectionResizeMode(2 + j, QHeaderView::Fixed);
		header->resizeSection(2 + j, 64);
	}
	header->setSectionResizeMode(totalColumn, QHeaderView::Fixed);
	header->resizeSection(totalColumn, 110);
	header->setSectionResizeMode(totalColumn + 1, QHeaderView::Fixed);
	header->resizeSection(totalColumn + 1, 80);
	for (int i = 0; i < rows.size(); i++)
		rankTable_->setRowHeight(i, 34);
	const int headerHeight = qMax(30, rankTable_->horizontalHeader()->sizeHint().height());
	rankTable_->setFixedHeight(headerHeight + rows.size() * 34 + 2);

	detailBrowser_->setHtml(buildDetailHtml(curContest));
}

// ---------------------------------------------------------------- 原有统计表（保留 + 导出）

auto StatisticsBrowser::getScoreNormalChart(const QMap<int, int> &scoreCount, int listSize,
                                            int totalScore) -> QString {
	QString buffer = "";
	long long overallScoreSum = 0;
	double scoreDiscrim = 0;
	double scoreStandardDevia = 0;
	int scoreTierPrefix = 0;
	int lastScoreTier = -1;
	int lastScoreTierNum = -1;

	for (auto i = scoreCount.constEnd(); i != scoreCount.constBegin();) {
		i--;
		int curScoreTier = i.key();
		int curScoreTierNum = i.value();

		if (curScoreTier < 0)
			continue;

		overallScoreSum += 1LL * curScoreTier * curScoreTierNum;

		if (lastScoreTier >= 0)
			scoreDiscrim += qLn(1 + 10.00 * (lastScoreTier - curScoreTier) / totalScore) *
			                (1.00 - 1.00 * lastScoreTierNum * curScoreTierNum / listSize / listSize);

		lastScoreTier = curScoreTier;
		lastScoreTierNum = curScoreTierNum;
	}

	double scoreAverage = 1.00 * overallScoreSum / listSize;
	buffer += "<table border=\"-1\">";
	buffer +=
	    QString(R"(<tr><th>%1</th><th>%2</th><th>%3</th><th colspan="2">%4</th><th colspan="2">%5</th></tr>)")
	        .arg(tr("Score"))
	        .arg(tr("Count"))
	        .arg(tr("Ratio"))
	        .arg(tr("Prefix"))
	        .arg(tr("Suffix"));

	for (auto i = scoreCount.constEnd(); i != scoreCount.constBegin();) {
		i--;
		int curScoreTier = i.key();
		int curScoreTierNum = i.value();
		scoreStandardDevia += qPow(curScoreTier - scoreAverage, 2) * curScoreTierNum;
		buffer += "<tr>";
		buffer += QString("<td align=\"right\"><nobr>%1 Pt</nobr></td>")
		              .arg(curScoreTier < 0 ? QString("N/A") : QString::number(curScoreTier));
		buffer += QString("<td align=\"right\"><nobr>%1</nobr></td>").arg(curScoreTierNum);
		buffer += QString("<td align=\"right\"><nobr>%1%</nobr></td>")
		              .arg(QString::number(100.00 * curScoreTierNum / listSize, 'f', 3));
		buffer += QString("<td align=\"right\"><nobr>%1</nobr></td>").arg(listSize - scoreTierPrefix);
		buffer += QString("<td align=\"right\"><nobr>%1%</nobr></td>")
		              .arg(QString::number(100.00 - 100.00 * scoreTierPrefix / listSize, 'f', 3));
		scoreTierPrefix += curScoreTierNum;
		buffer += QString("<td align=\"right\"><nobr>%1</nobr></td>").arg(scoreTierPrefix);
		buffer += QString("<td align=\"right\"><nobr>%1%</nobr></td>")
		              .arg(QString::number(100.00 * scoreTierPrefix / listSize, 'f', 3));
		buffer += "</tr>";
	}

	buffer += "</table>";
	scoreStandardDevia = qSqrt(scoreStandardDevia / listSize);
	scoreDiscrim = scoreDiscrim * scoreDiscrim;
	buffer += "<p>" + tr("Average") + " : " + QString::number(scoreAverage) + " / " +
	          QString::number(totalScore) + "</p>";
	buffer += "<p>" + tr("Standard Deviation") + " : " + QString::number(scoreStandardDevia) + "<p>";
	buffer += "<p>" + tr("Score Discrimination Power") + " : " + QString::number(scoreDiscrim) + "<p>";
	return buffer;
}

auto StatisticsBrowser::getTestcaseScoreChart(QList<TestCase *> testCaseList,
                                              QList<QList<QList<int>>> scoreList,
                                              QList<QList<QList<ResultState>>> resultList) -> QString {
	QString buffer = "";
	buffer += "<table border=\"-1\">";
	buffer +=
	    QString(
	        R"(<tr><th>%1</th><th>%2</th><th>%3</th><th colspan="2">%4</th><th colspan="2">%5</th><th colspan="2">%6</th><th>%7</th></tr>)")
	        .arg(tr("No."))
	        .arg(tr("Input"))
	        .arg(tr("Output"))
	        .arg(tr("Pure"))
	        .arg(tr("Far"))
	        .arg(tr("Lost"))
	        .arg(tr("Average"));

	for (int i = 0; i < testCaseList.length(); i++) {
		QStringList inFileList = testCaseList[i]->getInputFiles();
		QStringList outFileList = testCaseList[i]->getOutputFiles();
		int mxScore = testCaseList[i]->getFullScore();
		QList<int> miScoreRecord;
		QList<int> miStatRecord;

		for (int j = 0; j < scoreList.length(); j++) {
			miScoreRecord.append(mxScore);
			miStatRecord.append(2);
		}

		for (int j = 0; j < inFileList.length(); j++) {
			int cntFail = 0;
			int cntPati = 0;
			int cntSucc = 0;
			long long sumscore = 0;

			for (int k = 0; k < scoreList.length(); k++) {
				int score = 0;
				int statVal = 2;
				ResultState stat = WrongAnswer;

				if (scoreList[k].length() > i && scoreList[k][i].length() > j) {
					score = scoreList[k][i][j];
					stat = resultList[k][i][j];
				}

				if (stat == CorrectAnswer)
					cntSucc++, statVal = 2;
				else if (stat == PartlyCorrect)
					cntPati++, statVal = 1;
				else
					cntFail++, statVal = 0;

				sumscore += score;
				miScoreRecord[k] = qMin(miScoreRecord[k], score);
				miStatRecord[k] = qMin(miStatRecord[k], statVal);
			}

			buffer += "<tr>";
			buffer += "<td align=\"left\">" + QString("%1.%2").arg(i + 1).arg(j + 1) + "</td>";
			buffer += "<td align=\"left\">" + QString("%1").arg(inFileList[j]) + "</td>";
			buffer += "<td align=\"left\">" + QString("%1").arg(outFileList[j]) + "</td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(cntSucc) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * cntSucc / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(cntPati) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * cntPati / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(cntFail) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * cntFail / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1 / %2")
			              .arg(QString::number(1.00 * sumscore / scoreList.length(), 'f', 3))
			              .arg(mxScore) +
			          "</nobr></td>";
			buffer += "</tr>";
		}

		if (inFileList.length() > 1) {
			int sumCntFail = 0;
			int sumCntPati = 0;
			int sumCntSucc = 0;
			long long sumSumScore = 0;

			for (int j = 0; j < scoreList.length(); j++) {
				sumSumScore += miScoreRecord[j];

				if (miStatRecord[j] >= 2)
					sumCntSucc++;
				else if (miStatRecord[j] == 1)
					sumCntPati++;
				else
					sumCntFail++;
			}

			buffer += "<tr>";
			buffer += "<td align=\"left\">" + QString("%1 %2").arg(i + 1).arg("Overall") + "</td>";
			buffer +=
			    "<td align=\"left\">" + QString("%1 %2").arg(inFileList.length()).arg(tr("Files")) + "</td>";
			buffer +=
			    "<td align=\"left\">" + QString("%1 %2").arg(outFileList.length()).arg(tr("Files")) + "</td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(sumCntSucc) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * sumCntSucc / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(sumCntPati) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * sumCntPati / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" + QString("%1").arg(sumCntFail) + "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1%").arg(QString::number(100.00 * sumCntFail / scoreList.length(), 'f', 3)) +
			          "</nobr></td>";
			buffer += "<td align=\"right\"><nobr>" +
			          QString("%1 / %2")
			              .arg(QString::number(1.00 * sumSumScore / scoreList.length(), 'f', 3))
			              .arg(mxScore) +
			          "</nobr></td>";
			buffer += "</tr>";
		}
	}

	buffer += "</table>";
	return buffer;
}

auto StatisticsBrowser::checkValid(QList<Task *> taskList,
                                   const QList<Contestant *> &contestantList) -> bool {
	for (auto *i : taskList) {
		for (auto *j : i->getTestCaseList()) {
			if (j->getInputFiles().length() != j->getOutputFiles().length())
				return false;
		}
	}

	for (auto *i : contestantList) {
		for (int j = 0; j < taskList.length(); j++) {
			QList<QList<int>> scoreList;
			QList<QList<ResultState>> resultList;
			QList<TestCase *> testCaseList;
			bool isJudged = false;

			try {
				scoreList = i->getScore(j);
				resultList = i->getResult(j);
				testCaseList = taskList[j]->getTestCaseList();
				isJudged = i->getCheckJudged(j);
			} catch (...) {
				return false;
			}

			if (! isJudged)
				return false;

			if (scoreList.length() != resultList.length())
				return false;

			if (scoreList.length() > 0 && resultList.length() > 0 && testCaseList.length() > 0) {
				if (scoreList.length() != testCaseList.length())
					return false;

				if (resultList.length() != testCaseList.length())
					return false;

				for (int k = 0; k < testCaseList.length(); k++) {

					// 如果有子任务依赖，就会比一般的题目多一个 score 存依赖

					if (scoreList[k].length() - (! testCaseList[k]->getDependenceSubtask().empty()) !=
					    testCaseList[k]->getInputFiles().length())
						return false;

					if (resultList[k].length() != testCaseList[k]->getInputFiles().length())
						return false;
				}
			}
		}
	}

	return true;
}

auto StatisticsBrowser::buildDetailHtml(Contest *contest) -> QString {
	QString buffer;

	if (! contest) {
		nowBrowserText = tr("No contest yet");
		return nowBrowserText;
	}

	QList<Task *> taskList = contest->getTaskList();
	QList<Contestant *> contestantList = contest->getContestantList();

	if (taskList.empty()) {
		nowBrowserText = tr("No task yet");
		return nowBrowserText;
	}

	if (contestantList.empty()) {
		nowBrowserText = tr("No contestant yet");
		return nowBrowserText;
	}

	if (! checkValid(taskList, contestantList)) {
		nowBrowserText =
		    tr("Some unhandled situation happened. May not all contestants are well judged, or not "
		       "rejudged after changing testcases. Please refresh and rejudge.");
		return nowBrowserText;
	}

	int totalScore = contest->getTotalScore();
	buffer += "<html><head>";
	// 与主界面一致的轻量样式：卡片内阅读友好，表格用统一的浅灰描边
	buffer += "<style type=\"text/css\">"
	          "body { color: #1E293B; }"
	          "h1 { color: #0F172A; }"
	          "h2 { color: #334155; border-bottom: 1px solid #E5E7EB; padding-bottom: 4px; }"
	          "h3 { color: #475569; }"
	          "table { border-collapse: collapse; }"
	          "th, td { padding-left: 1em; padding-right: 1em; padding-top: 3px; padding-bottom: 3px; "
	          "border: 1px solid #E5E7EB; }"
	          "th { background-color: #F8FAFC; color: #475569; }"
	          "</style>";
	buffer += "</head><body>";
	buffer += "<h1>" + QString("%1 %2").arg(tr("Contest")).arg(contest->getContestTitle()) + "</h1>";

	// 各题目得分概况：整行铺满宽度的行式布局（对齐预览版统计页）——
	// 题名 + AC 进度条 + AC 率 + 均分。表格显式 width=100%，避免内容只挤在右侧。
	buffer += "<h2>" + tr("Overview") + "</h2>";
	buffer += "<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"2\">";

	for (int i = 0; i < taskList.size(); i++) {
		const int fullScore = taskList[i]->getTotalScore();
		int judgedCount = 0;
		int solvedCount = 0;
		long long scoreSum = 0;

		for (auto &j : contestantList) {
			const int score = j->getTaskScore(i);

			if (score < 0)
				continue; // 未评测的不计入统计

			judgedCount++;
			scoreSum += score;

			if (score >= fullScore)
				solvedCount++;
		}

		const double acRate = judgedCount > 0 ? 100.00 * solvedCount / judgedCount : 0.00;
		const double average = judgedCount > 0 ? 1.00 * scoreSum / judgedCount : 0.00;
		buffer += "<tr>";
		buffer += "<td width=\"180\"><nobr>" +
		          QString("%1 %2: %3").arg(tr("Task")).arg(i + 1).arg(taskList[i]->getProblemTitle()) +
		          "</nobr></td>";
		// 进度条：内层两格按 AC 率分配百分比宽度（Qt 富文本支持嵌套表格与百分比宽度）
		buffer += QString("<td><table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\">"
		                  "<tr><td width=\"%1%\" bgcolor=\"#84CC16\">&nbsp;</td>"
		                  "<td bgcolor=\"#F1F5F9\">&nbsp;</td></tr></table></td>")
		              .arg(QString::number(acRate, 'f', 1));
		buffer += "<td width=\"96\" align=\"right\"><nobr>" + tr("AC Rate") + " " +
		          QString::number(acRate, 'f', 1) + "%</nobr></td>";
		buffer += "<td width=\"96\" align=\"right\"><nobr>" + tr("Average") + " " +
		          QString::number(average, 'f', 1) + "</nobr></td>";
		buffer += "</tr>";
	}

	buffer += "</table>";
	buffer += "<h2>" + tr("Overall") + "</h2>";
	bool haveError = false;
	QMap<int, int> scoreCount;

	for (auto &i : contestantList) {
		int contestantTotalScore = 0;
		bool loss = false;

		for (int j = 0; j < taskList.size(); j++) {
			contestantTotalScore += i->getTaskScore(j);

			if (i->getTaskScore(j) < 0)
				haveError = true, loss = true;
		}

		if (! loss)
			scoreCount[contestantTotalScore]++;
		else
			scoreCount[-1]++;
	}

	if (haveError) {
		buffer += "<p style=\"font-size: large; color: red;\">" + tr("Warning: Judgement is not finished.") +
		          "</p><br>";
	}

	buffer += getScoreNormalChart(scoreCount, contestantList.size(), totalScore);
	buffer += "<br>";
	buffer += "<br>";
	buffer += "<h2>" + tr("Problems") + "</h2>";

	for (int i = 0; i < taskList.size(); i++) {
		buffer += "<h3>";
		buffer += QString("%1 %2: %3").arg(tr("Task")).arg(i + 1).arg(taskList[i]->getProblemTitle());
		buffer += "</h3>";
		int numberSubmitted = 0;
		QMap<int, int> cnts;
		QList<QList<QList<int>>> TestcaseScoreList;
		QList<QList<QList<ResultState>>> resultList;

		for (auto &j : contestantList) {
			cnts[j->getTaskScore(i)]++;

			if (j->getCompileState(i) != NoValidSourceFile && j->getCompileState(i) != NoValidGraderFile)
				numberSubmitted++;

			TestcaseScoreList.append(j->getScore(i));
			resultList.append(j->getResult(i));
		}

		buffer += getScoreNormalChart(cnts, contestantList.size(), taskList[i]->getTotalScore());
		buffer += "<p>" + tr("Number of answer submitted") + " : " + QString::number(numberSubmitted) +
		          " / " + QString::number(contestantList.size()) + " (" +
		          QString::number(100.00 * numberSubmitted / contestantList.size()) + "%)</p>";
		buffer += getTestcaseScoreChart(taskList[i]->getTestCaseList(), TestcaseScoreList, resultList);
		buffer += "<br>";
		buffer += "<br>";
	}

	buffer += "</body></html>";
	nowBrowserText = buffer;
	return buffer;
}

void StatisticsBrowser::exportStatisticsHtml(QWidget *widget, const QString &fileName) {
	QFile file(fileName);

	if (! file.open(QFile::WriteOnly)) {
		QMessageBox::warning(widget, tr("LemonLime"),
		                     tr("Cannot open file %1").arg(QFileInfo(file).fileName()), QMessageBox::Ok);
		return;
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);
	QTextStream out(&file);
	out << nowBrowserText;
	QApplication::restoreOverrideCursor();
	QMessageBox::information(widget, tr("LemonLime"), tr("Export is done"), QMessageBox::Ok);
}

void StatisticsBrowser::exportStatistics(QWidget *widget, Contest *curContest) {
	if (! curContest) {
		QMessageBox::warning(widget, tr("LemonLime"), tr("No contest yet"), QMessageBox::Ok);
		return;
	}

	QList<Task *> taskList = curContest->getTaskList();
	QList<Contestant *> contestantList = curContest->getContestantList();

	if (taskList.empty()) {
		QMessageBox::warning(widget, tr("LemonLime"), tr("No task yet"), QMessageBox::Ok);
		return;
	}

	if (contestantList.empty()) {
		QMessageBox::warning(widget, tr("LemonLime"), tr("No contestant yet"), QMessageBox::Ok);
		return;
	}

	QString filter = tr("HTML Document (*.html)");
	QString fileName = QFileDialog::getSaveFileName(
	    widget, tr("Export Statistics"), QDir::currentPath() + QDir::separator() + "statistics.html", filter);

	if (fileName.isEmpty())
		return;

	if (QFileInfo(fileName).suffix() == "html")
		exportStatisticsHtml(widget, fileName);
}