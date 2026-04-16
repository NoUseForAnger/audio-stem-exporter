#ifdef MW_ENABLE_QT

#include "mw-dock.hpp"

#include <obs-module.h>
#ifdef MW_ENABLE_FRONTEND
#include <obs-frontend-api.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSettings>

#include <chrono>

// ─── Column indices ───────────────────────────────────────────────────────────
static constexpr int COL_SOURCE = 0;
static constexpr int COL_STATUS = 1;
static constexpr int COL_FORMAT = 2;
static constexpr int COL_ACTION = 3;
static constexpr int COL_COUNT  = 4;

static const char *SETTINGS_ORG   = "obs-mp3-writer";
static const char *SETTINGS_APP   = "dock";
static const char *KEY_COL_WIDTHS = "columnWidths";

// ─────────────────────────────────────────────────────────────────────────────
// MwDockWidget
// ─────────────────────────────────────────────────────────────────────────────

MwDockWidget::MwDockWidget(QWidget *parent)
	: QWidget(parent)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	// ── Table ─────────────────────────────────────────────────────────────────
	m_table = new QTableWidget(0, COL_COUNT, this);
	m_table->setHorizontalHeaderLabels({"Source", "Status", "Fmt", ""});

	QHeaderView *hdr = m_table->horizontalHeader();
	hdr->setSectionResizeMode(QHeaderView::Interactive);
	hdr->resizeSection(COL_SOURCE, 110);
	hdr->resizeSection(COL_STATUS,  78);
	hdr->resizeSection(COL_FORMAT,  38);
	hdr->resizeSection(COL_ACTION,  24);

	// Restore saved column widths
	QSettings s(SETTINGS_ORG, SETTINGS_APP);
	QVariantList saved = s.value(KEY_COL_WIDTHS).toList();
	if (saved.size() == COL_COUNT) {
		for (int c = 0; c < COL_COUNT; ++c)
			hdr->resizeSection(c, saved[c].toInt());
	}

	connect(hdr, &QHeaderView::sectionResized,
		this, &MwDockWidget::saveColumnWidths);

	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionMode(QAbstractItemView::NoSelection);
	m_table->verticalHeader()->setVisible(false);
	m_table->setShowGrid(false);
	m_table->setAlternatingRowColors(true);

	// Click on the action column to start/stop that source
	connect(m_table, &QTableWidget::cellClicked,
		this, &MwDockWidget::onCellClicked);

	root->addWidget(m_table);

	// ── Global buttons ────────────────────────────────────────────────────────
	auto *btnRow = new QHBoxLayout;
	m_startAll   = new QPushButton("Start All", this);
	m_stopAll    = new QPushButton("Stop All",  this);
	btnRow->addWidget(m_startAll);
	btnRow->addWidget(m_stopAll);
	root->addLayout(btnRow);

	// ── Timer ─────────────────────────────────────────────────────────────────
	m_timer = new QTimer(this);
	m_timer->setInterval(1000);
	connect(m_timer, &QTimer::timeout, this, &MwDockWidget::refresh);
	m_timer->start();

	connect(m_startAll, &QPushButton::clicked, this, &MwDockWidget::startAll);
	connect(m_stopAll,  &QPushButton::clicked, this, &MwDockWidget::stopAll);

	refresh();
}

void MwDockWidget::saveColumnWidths()
{
	QHeaderView *hdr = m_table->horizontalHeader();
	QVariantList widths;
	for (int c = 0; c < COL_COUNT; ++c)
		widths << hdr->sectionSize(c);
	QSettings(SETTINGS_ORG, SETTINGS_APP).setValue(KEY_COL_WIDTHS, widths);
}

void MwDockWidget::onCellClicked(int row, int col)
{
	if (col != COL_ACTION) return;

	std::vector<MwFilter *> snap;
	mw_registry_snapshot(snap);
	if (row < 0 || row >= (int)snap.size()) return;

	MwFilter *f = snap[row];
	if (f->active.load(std::memory_order_relaxed))
		mw_stop_recording_one(f);
	else
		mw_start_recording_one(f);

	refresh();
}

void MwDockWidget::refresh()
{
	std::vector<MwFilter *> snap;
	mw_registry_snapshot(snap);

	m_table->horizontalHeader()->blockSignals(true);
	m_table->setRowCount((int)snap.size());
	m_table->horizontalHeader()->blockSignals(false);

	for (int i = 0; i < (int)snap.size(); ++i) {
		MwFilter *f = snap[i];

		obs_source_t *parent = obs_filter_get_parent(f->context);
		const char   *name   = parent ? obs_source_get_name(parent)
		                              : obs_source_get_name(f->context);

		bool active = f->active.load(std::memory_order_relaxed);

		// Status
		QString statusText;
		QColor  statusColor;
		if (active) {
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::steady_clock::now() - f->start_time).count();
			statusText  = QString("● %1:%2")
				          .arg((int)(elapsed / 60), 2, 10, QChar('0'))
				          .arg((int)(elapsed % 60), 2, 10, QChar('0'));
			statusColor = QColor(220, 50, 50);
		} else {
			statusText  = "○ Idle";
			statusColor = QColor(160, 160, 160);
		}

		// Format
		const char *fmtStr = "WAV";
		if (f->format == MwFormat::MP3)  fmtStr = "MP3";
		if (f->format == MwFormat::AIFF) fmtStr = "AIFF";

		auto setCell = [&](int col, const QString &text) -> QTableWidgetItem * {
			QTableWidgetItem *item = m_table->item(i, col);
			if (!item) {
				item = new QTableWidgetItem;
				item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
				m_table->setItem(i, col, item);
			}
			item->setText(text);
			return item;
		};

		setCell(COL_SOURCE, name ? QString::fromUtf8(name) : "(unknown)");
		setCell(COL_FORMAT, fmtStr);
		setCell(COL_STATUS, statusText)->setForeground(statusColor);

		// Action cell — plain text, centered, colored to hint it's clickable
		QTableWidgetItem *act = m_table->item(i, COL_ACTION);
		if (!act) {
			act = new QTableWidgetItem;
			act->setTextAlignment(Qt::AlignCenter);
			m_table->setItem(i, COL_ACTION, act);
		}
		if (active) {
			act->setText("■");
			act->setForeground(QColor(220, 50, 50));
			act->setToolTip("Stop recording");
		} else {
			act->setText("▶");
			act->setForeground(QColor(50, 200, 80));
			act->setToolTip("Start recording");
		}
	}

	m_table->resizeRowsToContents();

	bool anyIdle = false, anyActive = false;
	for (MwFilter *f : snap) {
		if (f->active.load(std::memory_order_relaxed)) anyActive = true;
		else                                            anyIdle   = true;
	}
	m_startAll->setEnabled(anyIdle);
	m_stopAll->setEnabled(anyActive);
}

void MwDockWidget::startAll() { mw_start_all(); refresh(); }
void MwDockWidget::stopAll()  { mw_stop_all();  refresh(); }

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void mw_dock_register()
{
#ifdef MW_ENABLE_FRONTEND
	auto *widget = new MwDockWidget(
		static_cast<QWidget *>(obs_frontend_get_main_window()));

	obs_frontend_add_dock_by_id(
		"obs-mp3-writer-dock",
		"Audio Stem Exporter",
		widget);
#endif
}

#endif // MW_ENABLE_QT
