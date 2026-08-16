#include "output-dock.hpp"

#include "../version.h"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QCheckBox>
#include <QDateTime>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QToolBar>
#include <QToolButton>
#include <QStyle>
#include <src/utils/color.hpp>
#include <src/utils/icon.hpp>
#include <src/utils/output-health.hpp>
#include <util/config-file.h>
#include <util/platform.h>

void open_config_dialog(int tab, const char *create_type);

OutputDock::OutputDock(QWidget *parent) : QFrame(parent)
{

	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	auto t = new QWidget;

	QScrollArea *scrollArea = new QScrollArea;
	scrollArea->setWidget(t);
	scrollArea->setWidgetResizable(true);
	scrollArea->setLineWidth(0);
	scrollArea->setFrameShape(QFrame::NoFrame);
	//scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	auto layout = new QVBoxLayout;
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(scrollArea);

	auto toolbar = new QToolBar();

	toolbar->setObjectName(QStringLiteral("outputsToolbar"));
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setFloatable(false);

	auto a = toolbar->addAction(QIcon(QString::fromUtf8(":/settings/images/settings/general.svg")),
				    QString::fromUtf8(obs_frontend_get_locale_string("Settings")),
				    [this] { open_config_dialog(2, nullptr); });
	toolbar->widgetForAction(a)->setProperty("themeID", QVariant(QString::fromUtf8("propertiesIconSmall")));
	toolbar->widgetForAction(a)->setProperty("class", "icon-gear");

	auto addMenu = new QMenu;

	a = toolbar->addAction(QIcon(":/res/images/plus.svg"), QString::fromUtf8(obs_module_text("AddOutput")),
			       [addMenu] { addMenu->exec(QCursor::pos()); });
	toolbar->widgetForAction(a)->setProperty("themeID", QVariant(QString::fromUtf8("addIconSmall")));
	toolbar->widgetForAction(a)->setProperty("class", "icon-plus");

	startAllAction = toolbar->addAction(
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
		QIcon::fromTheme(QIcon::ThemeIcon::MediaPlaybackStart, QIcon(":/res/images/media/media_play.svg")),
#else
		QIcon(":/res/images/media/media_play.svg"),
#endif
		QString::fromUtf8(obs_module_text("StartAll")));
	connect(startAllAction, &QAction::triggered, [this] {
		QMenu startMenu;
		auto a2 = startMenu.addAction(
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
			QIcon::fromTheme(QIcon::ThemeIcon::MediaPlaybackStart, QIcon(":/res/images/media/media_play.svg")),
#else
			QIcon(":/res/images/media/media_play.svg"),
#endif
			QString::fromUtf8(obs_module_text("StartAllOutputs")), [this]() { StartAll(false, false); });
		a2->setProperty("themeID", QVariant(QString::fromUtf8("playIcon")));
		a2->setProperty("class", "icon-media-play");
		if (mainStreamEnabled || std::find_if(outputWidgets.begin(), outputWidgets.end(),
						      [](OutputWidget *w) { return w->IsStream(); }) != outputWidgets.end()) {
			startMenu.addAction(QIcon(QString::fromUtf8(":/aitum/media/stream.svg")),
					    QString::fromUtf8(obs_module_text("StartAllStreams")),
					    [this]() { StartAll(true, false); });
		}
		if (mainRecordEnabled || std::find_if(outputWidgets.begin(), outputWidgets.end(),
						      [](OutputWidget *w) { return w->IsRecord(); }) != outputWidgets.end()) {
			startMenu.addAction(QIcon(QString::fromUtf8(":/aitum/media/record.svg")),
					    QString::fromUtf8(obs_module_text("StartAllRecordings")),
					    [this]() { StartAll(false, true); });
		}
		startMenu.exec(QCursor::pos());
	});
	startAllButton = static_cast<QToolButton *>(toolbar->widgetForAction(startAllAction));
	startAllButton->setProperty("themeID", QVariant(QString::fromUtf8("playIcon")));
	startAllButton->setProperty("class", "icon-media-play");
	startAllAction->setIconText(QString::fromUtf8(obs_module_text("StartAll")));
	startAllButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	startAllButton->setStyleSheet("QToolButton { min-width: 80px; max-width: none; }");

	stopAllAction = toolbar->addAction(
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
		QIcon::fromTheme(QIcon::ThemeIcon::MediaPlaybackStop, QIcon(":/res/images/media/media_stop.svg")),
#else
		QIcon(":/res/images/media/media_stop.svg"),
#endif
		QString::fromUtf8(obs_module_text("StopAllOutputs")), [this] { StopAll(false, false); });
	stopAllButton = static_cast<QToolButton *>(toolbar->widgetForAction(stopAllAction));
	stopAllButton->setProperty("themeID", QVariant(QString::fromUtf8("stopIcon")));
	stopAllButton->setProperty("class", "icon-media-stop");
	stopAllButton->setStyleSheet("QToolButton { min-width: 30px; max-width: none; }");

	a = addMenu->addAction(QIcon(QString::fromUtf8(":/aitum/media/stream.svg")), QString::fromUtf8(obs_module_text("Stream")),
			       [this] { open_config_dialog(2, "stream"); });

	a = addMenu->addAction(QIcon(QString::fromUtf8(":/aitum/media/record.svg")), QString::fromUtf8(obs_module_text("Record")),
			       [this] { open_config_dialog(2, "record"); });

	a = addMenu->addAction(QIcon(QString::fromUtf8(":/aitum/media/backtrack_off.svg")),
			       QString::fromUtf8(obs_module_text("Backtrack")), [this] { open_config_dialog(2, "backtrack"); });

	a = addMenu->addAction(QIcon(QString::fromUtf8(":/aitum/media/virtual_cam_off.svg")),
			       QString::fromUtf8(obs_module_text("VirtualCamera")),
			       [this] { open_config_dialog(2, "virtual_cam"); });

	a = addMenu->addAction(QIcon(QString::fromUtf8(":/aitum/media/ffmpeg_off.svg")),
			       QString::fromUtf8(obs_module_text("FfmpegOutput")), [this] { open_config_dialog(2, "ffmpeg"); });

	layout->addWidget(toolbar);

	setLayout(layout);

	mainLayout = new QVBoxLayout;
	mainLayout->setContentsMargins(0, 0, 0, 0);
	t->setLayout(mainLayout);

	mainPlatformIconLabel = new QLabel;
	mainPlatformIconLabel->setPixmap(
		getPlatformIconFromEndpoint(QString::fromUtf8("")).pixmap(outputPlatformIconSize, outputPlatformIconSize));

	auto l2 = new QHBoxLayout;
	l2->setContentsMargins(5, 0, 5, 0);
	l2->setSpacing(0);

	l2->addWidget(mainPlatformIconLabel);
	l2->addWidget(new QLabel(QString::fromUtf8(obs_module_text("BuiltinStream"))), 1);
	l2->addWidget(new QLabel(QString::fromUtf8(obs_module_text("MainCanvas"))), 1);
	mainStreamButton = new QPushButton;
	mainStreamButton->setObjectName(QStringLiteral("canvasStream"));
	mainStreamButton->setMinimumHeight(30);
	mainStreamButton->setIcon(create2StateIcon(":/aitum/media/streaming.svg", ":/aitum/media/stream.svg"));
	mainStreamButton->setStyleSheet("QAbstractButton:checked{background: rgb(0,210,153);}");
	mainStreamButton->setCheckable(true);
	mainStreamButton->setChecked(false);

	connect(mainStreamButton, &QPushButton::clicked, [this] {
		if (mainStreamStarting || mainStreamStopping)
			return;
		const auto config = obs_frontend_get_user_config();
		if (obs_frontend_streaming_active()) {
			mainStreamButton->setChecked(true); // A checkable button unchecks before clicked runs; restore the live state before any confirmation dialog so stop never flickers off and back on. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
			bool stop = true;
			bool warnBeforeStreamStop = config_get_bool(config, "BasicWindow", "WarnBeforeStoppingStream");
			if (warnBeforeStreamStop && isVisible()) {
				auto button = QMessageBox::question(
					this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Title")),
					QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Text")),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
				if (button == QMessageBox::No)
					stop = false;
			}
			if (stop) {
				UpdateMainStreamStopping();
				obs_frontend_streaming_stop();
			}
		} else {
			bool warnBeforeStreamStart = config_get_bool(config, "BasicWindow", "WarnBeforeStartingStream");
			if (warnBeforeStreamStart && isVisible()) {
				auto button = QMessageBox::question(
					this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Title")),
					QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Text")),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
				if (button == QMessageBox::No) {
					mainStreamButton->setChecked(false);
					return;
				} else {
					UpdateMainStreamStarting();
					obs_frontend_streaming_start();
				}
			} else {
				UpdateMainStreamStarting();
				obs_frontend_streaming_start();
			}
		}
	});
	//streamButton->setSizePolicy(sp2);
	mainStreamButton->setToolTip(QString::fromUtf8(obs_module_text("Stream")));
	l2->addWidget(mainStreamButton);
	//mainLayout->addLayout(l2);

	mainStreamGroup = new QFrame;
	mainStreamGroup->setObjectName(QStringLiteral("mainStreamRow"));
	mainStreamGroup->setProperty("streaming", false);
	mainStreamGroup->setStyleSheet(
		"QFrame#mainStreamRow[streaming=\"true\"] { background: rgb(0,210,153); }");
	mainStreamGroup->setLayout(l2);

	mainLayout->addWidget(mainStreamGroup);

	l2 = new QHBoxLayout;
	l2->setContentsMargins(5, 0, 5, 0);
	l2->setSpacing(0);
	l2->addWidget(new QLabel(QString::fromUtf8(obs_module_text("BuiltinRecord"))), 1);
	l2->addWidget(new QLabel(QString::fromUtf8(obs_module_text("MainCanvas"))), 1);
	mainRecordButton = new QPushButton;
	mainRecordButton->setObjectName(QStringLiteral("canvasRecord"));
	mainRecordButton->setMinimumHeight(30);
	mainRecordButton->setIcon(create2StateIcon(":/aitum/media/recording.svg", ":/aitum/media/record.svg"));
	mainRecordButton->setStyleSheet("QAbstractButton:checked{background: rgb(255,0,0);}");
	mainRecordButton->setCheckable(true);
	mainRecordButton->setChecked(false);
	QObject::connect(mainRecordButton, SIGNAL(clicked()), main_window, SLOT(RecordActionTriggered()));

	l2->addWidget(mainRecordButton);

	mainRecordGroup = new QFrame;
	mainRecordGroup->setLayout(l2);
	mainLayout->addWidget(mainRecordGroup);

	l2 = new QHBoxLayout;
	l2->setContentsMargins(5, 0, 5, 0);
	l2->setSpacing(0);
	l2->addWidget(new QLabel(QString::fromUtf8(obs_module_text("BuiltinBacktrack"))), 1);
	l2->addWidget(new QLabel(QString::fromUtf8(obs_module_text("MainCanvas"))), 1);

	//mainBacktrackCheckbox =

	mainBacktrackCheckboxButton = new QPushButton;
	mainBacktrackCheckboxButton->setCheckable(true);
	mainBacktrackCheckboxButton->setStyleSheet(QString::fromUtf8(
		"QPushButton:checked{background: rgb(26,87,255);} QPushButton{ border-top-right-radius: 0; border-bottom-right-radius: 0; width: 32px; padding-left: 0px; padding-right: 0px;}"));

	auto buttonLayout = new QHBoxLayout;
	mainBacktrackCheckboxButton->setLayout(buttonLayout);

	mainBacktrackCheckbox = new QCheckBox;
	buttonLayout->addWidget(mainBacktrackCheckbox);

	mainBacktrackButton = new QPushButton;
	mainBacktrackButton->setObjectName(QStringLiteral("canvasBacktrack"));
	mainBacktrackButton->setMinimumHeight(30);
	mainBacktrackButton->setIcon(create2StateIcon(":/aitum/media/backtrack_on.svg", ":/aitum/media/backtrack_off.svg"));
	mainBacktrackButton->setStyleSheet(
		"QPushButton:checked{background: rgb(26,87,255);} QPushButton{min-width: 32px; padding-left: 0px; padding-right: 0px; border-top-left-radius: 0; border-bottom-left-radius: 0;}");
	mainBacktrackButton->setCheckable(true);
	mainBacktrackButton->setChecked(false);
	QObject::connect(mainBacktrackButton, &QPushButton::clicked, [this] {
		if (obs_frontend_replay_buffer_active()) {
			obs_frontend_replay_buffer_save();
			mainBacktrackButton->setChecked(true);
			mainBacktrackStartTime = QDateTime::currentDateTime();
		} else {
			mainBacktrackButton->setChecked(false);
		}
	});

	connect(mainBacktrackCheckboxButton, &QPushButton::clicked, [this] {
		bool enabled = mainBacktrackCheckboxButton->isChecked();
		if (enabled != mainBacktrackCheckbox->isChecked())
			mainBacktrackCheckbox->setChecked(enabled);
		if (enabled != mainBacktrackButton->isChecked())
			mainBacktrackButton->setChecked(enabled);
		if (enabled)
			obs_frontend_replay_buffer_start();
		else
			obs_frontend_replay_buffer_stop();
	});
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	connect(mainBacktrackCheckbox, &QCheckBox::checkStateChanged, [this] {
#else
	connect(mainBacktrackCheckbox, &QCheckBox::stateChanged, [this] {
#endif
		if (mainBacktrackCheckbox->isChecked() != mainBacktrackCheckboxButton->isChecked()) {
			mainBacktrackCheckboxButton->click();
		}
	});

	auto l3 = new QHBoxLayout;
	l3->setContentsMargins(0, 0, 0, 0);
	l3->setSpacing(0);
	l3->addWidget(mainBacktrackCheckboxButton);
	l3->addWidget(mainBacktrackButton);
	l2->addLayout(l3);

	mainBacktrackGroup = new QFrame;
	mainBacktrackGroup->setLayout(l2);
	mainLayout->addWidget(mainBacktrackGroup);

	l2 = new QHBoxLayout;
	l2->setContentsMargins(5, 0, 5, 0);
	l2->setSpacing(0);
	l2->addWidget(new QLabel(QString::fromUtf8(obs_module_text("BuiltinVirtualCamera"))), 1);
	l2->addWidget(new QLabel(QString::fromUtf8(obs_module_text("MainCanvas"))), 1);
	mainVirtualCamButton = new QPushButton;
	mainVirtualCamButton->setObjectName(QStringLiteral("canvasVirtualCamera"));
	mainVirtualCamButton->setMinimumHeight(30);
	mainVirtualCamButton->setIcon(create2StateIcon(":/aitum/media/virtual_cam_on.svg", ":/aitum/media/virtual_cam_off.svg"));
	mainVirtualCamButton->setStyleSheet("QAbstractButton:checked{background: rgb(192,128,0);}");
	mainVirtualCamButton->setCheckable(true);
	mainVirtualCamButton->setChecked(false);

	connect(mainVirtualCamButton, &QPushButton::clicked, [this] {
		if (obs_frontend_virtualcam_active()) {
			obs_frontend_stop_virtualcam();
			mainVirtualCamButton->setChecked(false);
		} else {
			obs_frontend_start_virtualcam();
			mainVirtualCamButton->setChecked(true);
		}
	});

	l2->addWidget(mainVirtualCamButton);

	mainVirtualCamGroup = new QFrame;
	mainVirtualCamGroup->setLayout(l2);
	mainLayout->addWidget(mainVirtualCamGroup);

	connect(&videoCheckTimer, &QTimer::timeout, [this] {
		if (exiting)
			return;

		if (mainPlatformIconLabel) {
			auto service = obs_frontend_get_streaming_service();
			auto url = QString::fromUtf8(
				service ? obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL) : "");
			if (url != mainPlatformUrl) {
				mainPlatformUrl = url;
				mainPlatformIconLabel->setPixmap(
					getPlatformIconFromEndpoint(url).pixmap(outputPlatformIconSize, outputPlatformIconSize));
			}
		}

		if (mainStreamEnabled) {
			auto active = obs_frontend_streaming_active();
			if (mainStreamStopping) {
				UpdateMainStreamRowStyle(true);
			} else { // OBS can report inactive before the final stopped event; keep Stopping… visible until that event confirms completion. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
				UpdateMainStreamRowStyle(active);
				if (mainStreamButton->isChecked() != active) {
					mainStreamButton->setChecked(active);
					if (!active) {
						mainStreamButton->setText("");
						mainStreamBitrateTimer.invalidate();
						mainStreamBytes = 0;
						mainStreamBitrateKbps = 0.0;
					}
				} else if (active) {
				auto t = QTime::fromMSecsSinceStartOfDay(mainStreamStartTime.msecsTo(QDateTime::currentDateTime()));
				auto output = obs_frontend_get_streaming_output();
				OutputHealthStats health;
				if (output) {
					if (!mainStreamBitrateTimer.isValid()) {
						mainStreamBytes = obs_output_get_total_bytes(output);
						mainStreamBitrateTimer.start();
					} else {
						const auto elapsed = mainStreamBitrateTimer.elapsed();
						if (elapsed >= 1000) {
							const auto outputBytes = obs_output_get_total_bytes(output);
							mainStreamBitrateKbps = outputBytes >= mainStreamBytes
										? (double)(outputBytes - mainStreamBytes) * 8.0 / (double)elapsed
										: 0.0;
							mainStreamBytes = outputBytes;
							mainStreamBitrateTimer.restart();
						}
					}
					health = GetOutputHealthStats(output);
					obs_output_release(output);
				}
				const auto bitrateMbps = QString::number(mainStreamBitrateKbps / 1000.0, 'f', 1);
				const auto droppedPercentage = QString::number(health.dropped_percentage, 'f', 1);
				mainStreamButton->setText(QString::fromUtf8(obs_module_text("StreamHealthSummary"))
								  .arg(bitrateMbps, droppedPercentage,
								       t.toString(t.hour() ? "hh:mm:ss" : "mm:ss"))); // Keep built-in and custom streams consistent so every destination exposes its own transport health. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
				mainStreamButton->setToolTip(QString::fromUtf8(obs_module_text("OutputHealthTooltip"))
								     .arg(bitrateMbps, QString::number(health.dropped_frames),
									  QString::number(health.total_frames), droppedPercentage,
									  QString::number(health.congestion_percentage, 'f', 1)));
				}
			}
		}

		if (mainRecordEnabled) {
			auto active = obs_frontend_recording_active();
			if (mainRecordButton->isChecked() != active) {
				mainRecordButton->setChecked(active);
				if (!active)
					mainRecordButton->setText("");
			} else if (active) {
				auto t = QTime::fromMSecsSinceStartOfDay(mainRecordStartTime.msecsTo(QDateTime::currentDateTime()));
				mainRecordButton->setText(t.toString(t.hour() ? "hh:mm:ss" : "mm:ss"));
			}
		}

		if (mainBacktrackEnabled) {
			auto enabled = obs_frontend_replay_buffer_active();
			if (mainBacktrackCheckboxButton->isChecked() != enabled) {
				mainBacktrackCheckboxButton->setChecked(enabled);
				if (!enabled)
					mainBacktrackButton->setText("");
			} else if (enabled) {
				auto t = QTime::fromMSecsSinceStartOfDay(
					mainBacktrackStartTime.msecsTo(QDateTime::currentDateTime()));
				mainBacktrackButton->setText(t.toString(t.hour() ? "hh:mm:ss" : "mm:ss"));
			}
			if (mainBacktrackCheckbox->isChecked() != enabled) {
				mainBacktrackCheckbox->setChecked(enabled);
			}
			if (mainBacktrackButton->isChecked() != enabled) {
				mainBacktrackButton->setChecked(enabled);
			}
		}

		if (mainVirtualCamEnabled) {
			auto active = obs_frontend_virtualcam_active();
			if (mainVirtualCamButton->isChecked() != active) {
				mainVirtualCamButton->setChecked(active);
				if (!active)
					mainVirtualCamButton->setText("");
			} else if (active) {
				auto t = QTime::fromMSecsSinceStartOfDay(
					mainVirtualCamStartTime.msecsTo(QDateTime::currentDateTime()));
				mainVirtualCamButton->setText(t.toString(t.hour() ? "hh:mm:ss" : "mm:ss"));
			}
		}

		for (auto it = outputWidgets.begin(); it != outputWidgets.end(); it++) {
			(*it)->CheckActive();
		}
		if (outputsStopping) {
			bool active = (mainStreamEnabled && obs_frontend_streaming_active()) ||
				      (mainRecordEnabled && obs_frontend_recording_active()) ||
				      (mainBacktrackEnabled && obs_frontend_replay_buffer_active()) ||
				      (mainVirtualCamEnabled && obs_frontend_virtualcam_active());
			for (auto *outputWidget : outputWidgets) {
				auto output = outputWidget->GetOutput();
				if (outputWidget->IsStopping() || (output && obs_output_active(output))) {
					active = true;
					break;
				}
			}
			if (!active)
				SetOutputsStopping(false);
		}
	});
	videoCheckTimer.start(500);
	obs_frontend_add_event_callback(frontend_event, this);

	StartStopHotkey = obs_hotkey_pair_register_frontend(
		"AitumStreamSuiteStartAllOutputs", obs_module_text("StartAllOutputs"), "AitumStreamSuiteStopAllOutputs",
		obs_module_text("StopAllOutputs"),
		[](void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed) {
			UNUSED_PARAMETER(id);
			UNUSED_PARAMETER(hotkey);
			if (!pressed)
				return false;
			auto dock = static_cast<OutputDock *>(data);
			QMetaObject::invokeMethod(dock, [dock] { dock->StartAll(false, false); });
			return true;
		},
		[](void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed) {
			UNUSED_PARAMETER(id);
			UNUSED_PARAMETER(hotkey);
			if (!pressed)
				return false;
			auto dock = static_cast<OutputDock *>(data);
			QMetaObject::invokeMethod(dock, [dock] { dock->StopAll(false, false); });
			return true;
		},
		this, this);

	StartStreamHotkey = obs_hotkey_register_frontend(
		"AitumStreamSuiteStartAllStreams", obs_module_text("StartAllStreams"),
		[](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed) {
			UNUSED_PARAMETER(id);
			UNUSED_PARAMETER(hotkey);
			if (!pressed)
				return;
			auto dock = static_cast<OutputDock *>(data);
			QMetaObject::invokeMethod(dock, [dock] { dock->StartAll(true, false); });
		},
		this);

	StartRecordHotkey = obs_hotkey_register_frontend(
		"AitumStreamSuiteStartAllRecordings", obs_module_text("StartAllRecordings"),
		[](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed) {
			UNUSED_PARAMETER(id);
			UNUSED_PARAMETER(hotkey);
			if (!pressed)
				return;
			auto dock = static_cast<OutputDock *>(data);
			QMetaObject::invokeMethod(dock, [dock] { dock->StartAll(false, true); });
		},
		this);
}

extern OutputDock *output_dock;
void RemoveWidget(QWidget *widget);

OutputDock::~OutputDock()
{
	if (StartStopHotkey != OBS_INVALID_HOTKEY_PAIR_ID)
		obs_hotkey_pair_unregister(StartStopHotkey);
	if (StartStreamHotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(StartStreamHotkey);
	if (StartRecordHotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(StartRecordHotkey);
	obs_frontend_add_event_callback(frontend_event, this);
	videoCheckTimer.stop();
	for (auto it = outputWidgets.begin(); it != outputWidgets.end(); it++) {
		mainLayout->removeWidget(*it);
		RemoveWidget(*it);
	}
	outputWidgets.clear();
	output_dock = nullptr;
}

extern obs_data_t *current_profile_config;

void OutputDock::LoadSettings()
{
	mainStreamEnabled = obs_data_get_bool(current_profile_config, "main_stream_output_show");
	mainStreamGroup->setVisible(mainStreamEnabled);

	mainRecordEnabled = obs_data_get_bool(current_profile_config, "main_record_output_show");
	mainRecordGroup->setVisible(mainRecordEnabled);

	mainBacktrackEnabled = obs_data_get_bool(current_profile_config, "main_backtrack_output_show");
	mainBacktrackGroup->setVisible(mainBacktrackEnabled);

	mainVirtualCamEnabled = obs_data_get_bool(current_profile_config, "main_virtual_cam_output_show");
	mainVirtualCamGroup->setVisible(mainVirtualCamEnabled);

	auto outputs2 = obs_data_get_array(current_profile_config, "outputs");
	for (auto it = outputWidgets.begin(); it != outputWidgets.end();) {
		bool found = false;
		auto count = obs_data_array_count(outputs2);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *data2 = obs_data_array_item(outputs2, i);
			auto name = QString::fromUtf8(obs_data_get_string(data2, "name"));
			obs_data_release(data2);
			if (name == (*it)->objectName()) {
				(*it)->UpdateSettings(data2);
				auto j = mainLayout->indexOf(*it);
				if (j >= 0 && i + 4 != (size_t)j) {
					mainLayout->takeAt(j);
					mainLayout->insertWidget((int)i + 4, (*it));
				}
				found = true;
				break;
			}
		}
		if (!found) {
			mainLayout->removeWidget(*it);
			RemoveWidget(*it);
			it = outputWidgets.erase(it);
		} else {
			it++;
		}
	}

	auto count = obs_data_array_count(outputs2);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *data2 = obs_data_array_item(outputs2, i);
		auto name = QString::fromUtf8(obs_data_get_string(data2, "name"));
		if (std::find_if(outputWidgets.begin(), outputWidgets.end(),
				 [&name](OutputWidget *ow) { return ow->objectName() == name; }) == outputWidgets.end()) {
			auto outputWidget = new OutputWidget(data2, this);
			outputWidgets.push_back(outputWidget);
			mainLayout->insertWidget((int)i + 4, outputWidget);
		}
		obs_data_release(data2);
	}

	obs_data_array_release(outputs2);

	QWidget *spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	mainLayout->addWidget(spacer);

	auto start_hotkey = obs_data_get_array(current_profile_config, "start_all_outputs_hotkey");
	auto stop_hotkey = obs_data_get_array(current_profile_config, "stop_all_outputs_hotkey");
	obs_hotkey_pair_load(StartStopHotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

	auto stream_hotkey = obs_data_get_array(current_profile_config, "start_all_streams_hotkey");
	obs_hotkey_load(StartStreamHotkey, stream_hotkey);
	obs_data_array_release(stream_hotkey);

	auto record_hotkey = obs_data_get_array(current_profile_config, "start_all_recordings_hotkey");
	obs_hotkey_load(StartRecordHotkey, record_hotkey);
	obs_data_array_release(record_hotkey);
}

void OutputDock::SaveSettings()
{
	for (auto it = outputWidgets.begin(); it != outputWidgets.end(); it++) {
		(*it)->SaveSettings();
	}

	if (StartStopHotkey != OBS_INVALID_HOTKEY_PAIR_ID) {
		obs_data_array_t *start_hotkey = nullptr;
		obs_data_array_t *stop_hotkey = nullptr;
		obs_hotkey_pair_save(StartStopHotkey, &start_hotkey, &stop_hotkey);
		obs_data_set_array(current_profile_config, "start_all_outputs_hotkey", start_hotkey);
		obs_data_set_array(current_profile_config, "stop_all_outputs_hotkey", stop_hotkey);
		obs_data_array_release(start_hotkey);
		obs_data_array_release(stop_hotkey);
	}
	if (StartStreamHotkey != OBS_INVALID_HOTKEY_ID) {
		obs_data_array_t *stream_hotkey = obs_hotkey_save(StartStreamHotkey);
		obs_data_set_array(current_profile_config, "start_all_streams_hotkey", stream_hotkey);
		obs_data_array_release(stream_hotkey);
	}
	if (StartRecordHotkey != OBS_INVALID_HOTKEY_ID) {
		obs_data_array_t *record_hotkey = obs_hotkey_save(StartRecordHotkey);
		obs_data_set_array(current_profile_config, "start_all_recordings_hotkey", record_hotkey);
		obs_data_array_release(record_hotkey);
	}
}

void OutputDock::UpdateMainStreamStarting()
{
	mainStreamStarting = true;
	mainStreamStopping = false;
	mainStreamButton->setChecked(true);
	mainStreamButton->setEnabled(false); // A stream start is asynchronous, so disable repeat clicks and show the pending state until OBS reports started or stopped. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
	mainStreamButton->setText(QString::fromUtf8(obs_module_text("StartingOutput")));
	mainStreamButton->repaint();
}

void OutputDock::UpdateMainStreamStopping()
{
	mainStreamStarting = false;
	mainStreamStopping = true;
	mainStreamButton->setChecked(true);
	mainStreamButton->setEnabled(false);
	mainStreamButton->setText(QString::fromUtf8(obs_module_text("StoppingOutput")));
	mainStreamButton->repaint(); // Keep the built-in stream visibly live while OBS stops it, then clear the row only on the confirmed stopped event. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
}

void OutputDock::UpdateMainStreamStatus(bool active)
{
	mainStreamStarting = false;
	mainStreamStopping = false;
	mainStreamButton->setEnabled(true);
	mainStreamButton->setChecked(active);
	UpdateMainStreamRowStyle(active);
	if (active) {
		mainStreamStartTime = QDateTime::currentDateTime();
		mainStreamBitrateTimer.invalidate();
		mainStreamBytes = 0;
		mainStreamBitrateKbps = 0.0;
		mainStreamButton->setText(QString::fromUtf8(obs_module_text("StreamHealthSummary")).arg("0.0", "0.0", "00:00"));
	} else {
		mainStreamButton->setText("");
		mainStreamButton->setToolTip(QString::fromUtf8(obs_module_text("Stream")));
		mainStreamBitrateTimer.invalidate();
		mainStreamBytes = 0;
		mainStreamBitrateKbps = 0.0;
		return;
	}
	if (!current_profile_config)
		return;
	struct obs_video_info ovi = {0};
	obs_get_video_info(&ovi);
	double fps = ovi.fps_den > 0 ? (double)ovi.fps_num / (double)ovi.fps_den : 0.0;
	auto output = obs_frontend_get_streaming_output();
	bool found = false;
	for (auto i = 0; i < MAX_OUTPUT_VIDEO_ENCODERS; i++) {
		auto encoder = obs_output_get_video_encoder2(output, i);
		QString settingName = QString::fromUtf8("video_encoder_description") + QString::number(i);
		if (encoder) {
			found = true;
			auto mainEncoderDescription = QString::number(obs_encoder_get_width(encoder)) + "x" +
						      QString::number(obs_encoder_get_height(encoder));
			auto divisor = obs_encoder_get_frame_rate_divisor(encoder);
			if (divisor > 0)
				mainEncoderDescription +=
					QString::fromUtf8(" ") + QString::number(fps / divisor, 'g', 4) + QString::fromUtf8("fps");

			auto settings = obs_encoder_get_settings(encoder);
			auto bitrate = settings ? obs_data_get_int(settings, "bitrate") : 0;
			if (bitrate > 0)
				mainEncoderDescription +=
					QString::fromUtf8(" ") + QString::number(bitrate) + QString::fromUtf8("Kbps");
			obs_data_release(settings);

			obs_data_set_string(current_profile_config, settingName.toUtf8().constData(),
					    mainEncoderDescription.toUtf8().constData());

		} else if (found && obs_data_has_user_value(current_profile_config, settingName.toUtf8().constData())) {
			obs_data_unset_user_value(current_profile_config, settingName.toUtf8().constData());
		}
	}
	obs_output_release(output);
}

void OutputDock::UpdateMainStreamRowStyle(bool active)
{
	if (mainStreamGroup->property("streaming").toBool() == active)
		return;
	mainStreamGroup->setProperty("streaming", active);
	mainStreamGroup->style()->unpolish(mainStreamGroup);
	mainStreamGroup->style()->polish(mainStreamGroup);
	mainStreamGroup->update(); // Built-in streaming uses the same full-row warning as custom stream outputs; recording stays unchanged. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
}

void OutputDock::UpdateMainRecordingStatus(bool active)
{
	mainRecordButton->setChecked(active);
	if (active) {
		mainRecordStartTime = QDateTime::currentDateTime();
		mainRecordButton->setText("00:00");
	} else {
		mainRecordButton->setText("");
	}
}

void OutputDock::UpdateMainBacktrackStatus(bool active)
{
	mainBacktrackCheckboxButton->setChecked(active);
	mainBacktrackCheckbox->setChecked(active);
	mainBacktrackButton->setChecked(active);
	if (active) {
		mainBacktrackStartTime = QDateTime::currentDateTime();
		mainBacktrackButton->setText("00:00");
	} else {
		mainBacktrackButton->setText("");
	}
}

void OutputDock::UpdateMainVirtualCameraStatus(bool active)
{
	mainVirtualCamButton->setChecked(active);
	if (active) {
		mainVirtualCamStartTime = QDateTime::currentDateTime();
		mainVirtualCamButton->setText("00:00");
	} else {
		mainVirtualCamButton->setText("");
	}
}

obs_data_array_t *OutputDock::GetOutputsArray()
{
	auto outputs2 = obs_data_array_create();
	for (auto it = outputWidgets.begin(); it != outputWidgets.end(); it++) {
		auto data = obs_data_create();
		obs_data_set_string(data, "name", (*it)->objectName().toUtf8().constData());
		obs_data_set_string(data, "type", (*it)->GetOutputType());
		obs_data_set_bool(data, "active", obs_output_active((*it)->GetOutput()));
		obs_data_array_push_back(outputs2, data);
		obs_data_release(data);
	}
	return outputs2;
}

bool OutputDock::AddChapterToOutput(const char *output_name, const char *chapter_name)
{
	auto on = QString::fromUtf8(output_name);
	for (auto it = outputWidgets.begin(); it != outputWidgets.end(); it++) {
		if ((*it)->objectName() == on) {
			return (*it)->AddChapter(chapter_name);
		}
	}
	return false;
}

void OutputDock::StartNextOutput()
{
	if (outputsToStart.empty()) {
		SetOutputsStarting(false);
		return;
	}
	if (outputStarting >= outputsToStart.size())
		outputStarting = 0;
	auto startedAt = outputStarting;
	while (true) {
		auto action = std::next(outputsToStart.begin(), outputStarting);
		if ((*action)([this] { QMetaObject::invokeMethod(this, [this] { StartNextOutput(); }, Qt::QueuedConnection); })) {
			outputsToStart.erase(action);
			break;
		}
		outputStarting++;
		if (outputStarting >= outputsToStart.size()) {
			outputStarting = 0;
		}
		if (outputStarting == startedAt) {
			blog(LOG_WARNING, "[Aitum Stream Suite] went through all outputs and %zu could not be started",
			     outputsToStart.size());
			outputsToStart.clear();
			SetOutputsStarting(false);
			break;
		}
	}
}

void OutputDock::SetOutputsStarting(bool starting)
{
	outputsStarting = starting;
	if (!startAllAction || !startAllButton)
		return;
	startAllAction->setEnabled(!starting); // Start All owns one sequential queue, so its pending state is also the lock that prevents duplicate queue submissions. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
	const auto text = QString::fromUtf8(obs_module_text(starting ? "StartingOutputs" : "StartAll"));
	startAllAction->setText(text);
	startAllAction->setIconText(text);
	startAllButton->repaint();
}

void OutputDock::SetOutputsStopping(bool stopping)
{
	outputsStopping = stopping;
	if (!stopAllAction || !stopAllButton)
		return;
	stopAllAction->setEnabled(!stopping);
	const auto text = QString::fromUtf8(obs_module_text(stopping ? "StoppingOutputs" : "StopAllOutputs"));
	stopAllAction->setText(text);
	stopAllAction->setIconText(text);
	stopAllButton->setToolButtonStyle(stopping ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
	stopAllButton->setStyleSheet(stopping ? "QToolButton { min-width: 80px; max-width: none; }"
						  : "QToolButton { min-width: 30px; max-width: none; }");
	stopAllButton->repaint(); // Show text only during the pending stop so the idle toolbar stays compact. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
}

void OutputDock::frontend_event(enum obs_frontend_event event, void *private_data)
{
	auto dock = static_cast<OutputDock *>(private_data);
	if (!dock)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		if (dock->mainStreamOnStarted) {
			blog(LOG_INFO, "[Aitum Stream Suite] Main stream started, starting next output");
			dock->mainStreamOnStarted();
			dock->mainStreamOnStarted = nullptr;
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		if (dock->mainStreamOnStarted) {
			blog(LOG_INFO, "[Aitum Stream Suite] Main stream stopped, starting next output");
			dock->mainStreamOnStarted();
			dock->mainStreamOnStarted = nullptr;
		}
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		if (dock->mainRecordOnStarted) {
			blog(LOG_INFO, "[Aitum Stream Suite] Main recording started, starting next output");
			dock->mainRecordOnStarted();
			dock->mainRecordOnStarted = nullptr;
		}
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		if (dock->mainRecordOnStarted) {
			blog(LOG_INFO, "[Aitum Stream Suite] Main recording stopped, starting next output");
			dock->mainRecordOnStarted();
			dock->mainRecordOnStarted = nullptr;
		}
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
		if (dock->mainBacktrackOnStarted) {
			blog(LOG_INFO, "[Aitum Stream Suite] Main replay buffer started, starting next output");
			dock->mainBacktrackOnStarted();
			dock->mainBacktrackOnStarted = nullptr;
		}
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		if (dock->mainBacktrackOnStarted) {
			blog(LOG_INFO, "[Aitum Stream Suite] Main replay buffer stopped, starting next output");
			dock->mainBacktrackOnStarted();
			dock->mainBacktrackOnStarted = nullptr;
		}
		break;
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
		if (dock->mainVirtualCamOnStarted) {
			blog(LOG_INFO, "[Aitum Stream Suite] Main virtual camera started, starting next output");
			dock->mainVirtualCamOnStarted();
			dock->mainVirtualCamOnStarted = nullptr;
		}
		break;
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
		if (dock->mainVirtualCamOnStarted) {
			blog(LOG_INFO, "[Aitum Stream Suite] Main virtual camera stopped, starting next output");
			dock->mainVirtualCamOnStarted();
			dock->mainVirtualCamOnStarted = nullptr;
		}
		break;
	default:
		break;
	}
}

void OutputDock::StartAll(bool streamOnly, bool recordOnly)
{
	if (outputsStarting) {
		blog(LOG_INFO, "[Aitum Stream Suite] ignored duplicate Start All while outputs are starting");
		return;
	}
	outputsToStart.clear();
	if (mainStreamEnabled && !recordOnly) {
		bool warnBeforeStreamStart =
			config_get_bool(obs_frontend_get_user_config(), "BasicWindow", "WarnBeforeStartingStream");
		if (warnBeforeStreamStart && isVisible()) {
			auto button = QMessageBox::question(this,
							    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Title")),
							    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Text")),
							    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
			if (button == QMessageBox::No)
				return;
		}

		outputsToStart.push_back([this](std::function<void()> onStarted) {
			if (obs_frontend_streaming_active()) {
				blog(LOG_INFO, "[Aitum Stream Suite] Skipped starting main stream, already active");
				onStarted();
				return true;
			}
			this->mainStreamOnStarted = onStarted;
			obs_frontend_streaming_start();
			return true;
		});
	}
	if (mainRecordEnabled && !streamOnly) {
		outputsToStart.push_back([this](std::function<void()> onStarted) {
			if (obs_frontend_recording_active()) {
				blog(LOG_INFO, "[Aitum Stream Suite] Skipped starting main recording, already active");
				onStarted();
				return true;
			}
			this->mainRecordOnStarted = onStarted;
			obs_frontend_recording_start();
			auto output = obs_frontend_get_recording_output();
			if (!output) {
				blog(LOG_INFO, "[Aitum Stream Suite] No main recording output found");
				this->mainRecordOnStarted = nullptr;
				onStarted();
			}
			obs_output_release(output);
			return true;
		});
	}
	if (mainBacktrackEnabled && !streamOnly) {
		outputsToStart.push_back([this](std::function<void()> onStarted) {
			if (obs_frontend_replay_buffer_active()) {
				blog(LOG_INFO, "[Aitum Stream Suite] Skipped starting main replay buffer, already active");
				onStarted();
				return true;
			}
			this->mainBacktrackOnStarted = onStarted;
			obs_frontend_replay_buffer_start();
			auto output = obs_frontend_get_replay_buffer_output();
			if (!output) {
				blog(LOG_INFO, "[Aitum Stream Suite] No main replay buffer output found");
				this->mainBacktrackOnStarted = nullptr;
				onStarted();
			}
			obs_output_release(output);
			return true;
		});
	}
	if (mainVirtualCamEnabled && !streamOnly && !recordOnly) {
		outputsToStart.push_back([this](std::function<void()> onStarted) {
			if (obs_frontend_virtualcam_active()) {
				blog(LOG_INFO, "[Aitum Stream Suite] Skipped starting main virtual camera, already active");
				onStarted();
				return true;
			}
			this->mainVirtualCamOnStarted = onStarted;
			obs_frontend_start_virtualcam();
			auto output = obs_frontend_get_virtualcam_output();
			if (!output) {
				blog(LOG_INFO, "[Aitum Stream Suite] No main virtual camera output found");
				this->mainVirtualCamOnStarted = nullptr;
				onStarted();
			}
			obs_output_release(output);
			return true;
		});
	}
	for (auto &ow : outputWidgets) {
		if (streamOnly && !ow->IsStream())
			continue;
		if (recordOnly && !ow->IsRecord())
			continue;
		outputsToStart.push_back([this, ow](std::function<void()> onStarted) { return ow->StartOutput(onStarted); });
	}
	if (outputsToStart.empty())
		return;
	blog(LOG_INFO, "[Aitum Stream Suite] Starting %zu outputs", outputsToStart.size());
	SetOutputsStarting(true);
	StartNextOutput();
}

void OutputDock::StopAll(bool streamOnly, bool recordOnly)
{
	if (outputsStopping) {
		blog(LOG_INFO, "[Aitum Stream Suite] ignored duplicate Stop All while outputs are stopping");
		return;
	}
	outputsToStart.clear();
	SetOutputsStarting(false);
	bool warnStream = config_get_bool(obs_frontend_get_user_config(), "BasicWindow", "WarnBeforeStoppingStream");
	bool warnRecord = config_get_bool(obs_frontend_get_user_config(), "BasicWindow", "WarnBeforeStoppingRecord");
	if (warnStream && mainStreamEnabled && !recordOnly && obs_frontend_streaming_active() && isVisible()) {
		auto button = QMessageBox::question(this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Title")),
						    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Text")),
						    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (button == QMessageBox::No)
			return;
	} else if (warnRecord && mainRecordEnabled && !streamOnly && obs_frontend_recording_active() && isVisible()) {
		auto button = QMessageBox::question(this,
						    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStopRecord.Title")),
						    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStopRecord.Text")),
						    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (button == QMessageBox::No)
			return;
	}

	bool active = (mainStreamEnabled && !recordOnly && obs_frontend_streaming_active()) ||
		      (mainRecordEnabled && !streamOnly && obs_frontend_recording_active()) ||
		      (mainBacktrackEnabled && !streamOnly && obs_frontend_replay_buffer_active()) ||
		      (mainVirtualCamEnabled && !streamOnly && !recordOnly && obs_frontend_virtualcam_active());
	for (auto *outputWidget : outputWidgets) {
		if ((streamOnly && !outputWidget->IsStream()) || (recordOnly && !outputWidget->IsRecord()))
			continue;
		auto output = outputWidget->GetOutput();
		if (output && obs_output_active(output)) {
			active = true;
			break;
		}
	}
	if (!active)
		return;
	SetOutputsStopping(true);

	if (mainStreamEnabled && !recordOnly && obs_frontend_streaming_active())
		UpdateMainStreamStopping();
	if (mainStreamEnabled && !recordOnly && obs_frontend_streaming_active())
		obs_frontend_streaming_stop();
	if (mainRecordEnabled && !streamOnly && obs_frontend_recording_active())
		obs_frontend_recording_stop();
	if (mainBacktrackEnabled && !streamOnly && obs_frontend_replay_buffer_active())
		obs_frontend_replay_buffer_stop();
	if (mainVirtualCamEnabled && !streamOnly && !recordOnly && obs_frontend_virtualcam_active())
		obs_frontend_stop_virtualcam();
	for (auto &ow : outputWidgets) {
		if (streamOnly && !ow->IsStream())
			continue;
		if (recordOnly && !ow->IsRecord())
			continue;
		ow->StopOutput();
	}
}
