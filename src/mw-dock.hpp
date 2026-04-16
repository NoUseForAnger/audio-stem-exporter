#pragma once
#ifdef MW_ENABLE_QT

#include "mp3-writer-filter.hpp"

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QTimer>
#include <QSettings>

class MwDockWidget : public QWidget {
	Q_OBJECT
public:
	explicit MwDockWidget(QWidget *parent = nullptr);
	~MwDockWidget() override = default;

private slots:
	void refresh();
	void saveColumnWidths();
	void onCellClicked(int row, int col);

private:
	void startAll();
	void stopAll();

	QTableWidget *m_table;
	QPushButton  *m_startAll;
	QPushButton  *m_stopAll;
	QTimer       *m_timer;
};

void mw_dock_register();

#endif // MW_ENABLE_QT
