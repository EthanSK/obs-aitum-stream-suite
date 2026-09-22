#include "dialogs/config-dialog.hpp"
#include "dialogs/name-dialog.hpp"
#include "docks/browser-dock.hpp"
#include "docks/canvas-clone-dock.hpp"
#include "docks/canvas-dock.hpp"
#include "docks/capture-dock.hpp"
#include "docks/filters-dock.hpp"
#include "docks/live-scenes-dock.hpp"
#include "docks/output-dock.hpp"
#include "docks/properties-dock.hpp"
#include "docks/scenes-dock.hpp"
#include "docks/sources-dock.hpp"
#include "docks/stats-dock.hpp"
#include "docks/transform-dock.hpp"
#include "docks/transitions-dock.hpp"
#include "utils/file-download.h"
#include "utils/icon.hpp"
#include "utils/obs-websocket-api.h"
#include "utils/widgets/pixmap-label.hpp"
#include "version.h"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs-properties.h>
#include <QApplication>
#include <QDesktopServices>
#include <QDockWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <random>
#include <util/dstr.h>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Aitum");
OBS_MODULE_USE_DEFAULT_LOCALE("aitum-stream-suite", "en-US")

download_info_t *plugin_metadata_download_info = nullptr;
obs_data_t *current_profile_config = nullptr;
QTabBar *modesTabBar = nullptr;
QToolBar *toolbar = nullptr;
QString modesTab;

QList<QAction *> partnerBlockActions;
QAction *studioModeAction = nullptr;

OBSBasicSettings *configDialog = nullptr;
OutputDock *output_dock = nullptr;
PropertiesDock *properties_dock = nullptr;
FiltersDock *filters_dock = nullptr;
TransformDock *transform_dock = nullptr;
LiveScenesDock *live_scenes_dock = nullptr;
CanvasDock *component_dock = nullptr;
StatsDock *stats_dock = nullptr;
ScenesDock *scenes_dock = nullptr;
SourcesDock *sources_dock = nullptr;
TransitionsDock *transitions_dock = nullptr;

QTimer load_dock_state_timer;
QList<QString> loaded_docks;

extern std::list<CanvasDock *> canvas_docks;
extern std::list<CanvasCloneDock *> canvas_clone_docks;
extern obs_websocket_vendor vendor;

std::list<QFrame *> empty_docks;
std::vector<std::tuple<std::string, std::string, std::string>> extensions = {};

extern QWidget *aitumSettingsWidget;

static bool finished_loading = false;

// Aitum++: additive UI state. Advanced Workspaces opens after the normal OBS layout is captured, and the Tools toggle can still restore that familiar layout for the current session.
static bool aitum_pp_controls_active = false;
static bool aitum_pp_workspaces_active = false;
static QByteArray aitum_pp_normal_state;
static QAction *aitumPPControlsAction = nullptr;
static QAction *aitumPPWorkspacesAction = nullptr;

#ifdef __APPLE__
struct MacOSScreenCaptureRestartResult {
	size_t found = 0;
	size_t restarted = 0;
};

struct MacOSCameraSourceRefreshResult {
	size_t found = 0;
	size_t refreshed = 0;
};

static bool camera_services_restart_in_progress = false;

static void restart_macos_screen_captures()
{
	MacOSScreenCaptureRestartResult result;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			auto result = static_cast<MacOSScreenCaptureRestartResult *>(data);
			const char *source_id = obs_source_get_id(source);
			if (!source_id || strcmp(source_id, "screen_capture") != 0) {
				return true;
			}

			result->found++;
			auto properties = obs_source_properties(source);
			auto restart = properties ? obs_properties_get(properties, "reactivate_capture") : nullptr;
			const bool restarted = restart && obs_property_enabled(restart) &&
					       obs_property_button_clicked(restart, source);
			if (restarted) {
				result->restarted++;
			}
			blog(restarted ? LOG_INFO : LOG_WARNING,
			     "[Aitum++] Restart Screen Capture: source='%s' result=%s", obs_source_get_name(source),
			     restarted ? "restarted" : "not-restartable");
			if (properties) {
				obs_properties_destroy(properties);
			}
			return true;
		},
		&result);

	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window || !main_window->statusBar()) {
		return;
	}
	QString status;
	if (result.found == 0) {
		status = QString::fromUtf8(obs_module_text("RestartScreenCaptureNoneFound"));
	} else if (result.restarted == 0) {
		// OBS only enables its native restart action after ScreenCaptureKit reports a failure; forcing a rebuild by toggling source settings previously caused unsafe overlapping recoveries. (Codex task: 019ff120-ea11-71a3-8b65-c55b45cac2fe)
		status = QString::fromUtf8(obs_module_text("RestartScreenCaptureNoneRestartable"));
	} else {
		status = QString::fromUtf8(obs_module_text("RestartScreenCaptureResult"))
				 .arg(result.restarted)
				 .arg(result.found);
	}
	main_window->statusBar()->showMessage(status, 5000);
}

static void refresh_macos_camera_sources()
{
	MacOSCameraSourceRefreshResult result;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			auto *result = static_cast<MacOSCameraSourceRefreshResult *>(data);
			const char *source_id = obs_source_get_id(source);
			if (!source_id || (strcmp(source_id, "macos-avcapture") != 0 &&
				   strcmp(source_id, "macos-avcapture-fast") != 0)) {
				return true;
			}

			result->found++;
			auto *settings = obs_source_get_settings(source);
			const bool refreshed = settings != nullptr;
			if (settings) {
				obs_source_update(source, settings); // A camera missing during OBS startup is not reopened by its later macOS connect event, so reapply the unchanged settings after the camera services return. (Codex task: 01a01b14-9ef1-7082-99e7-1885d5d90235)
				obs_data_release(settings);
				result->refreshed++;
			}
			blog(LOG_INFO, "[Aitum++] Restart Camera Services: source='%s' result=%s",
			     obs_source_get_name(source), refreshed ? "refreshed" : "settings-unavailable");
			return true;
		},
		&result);

	camera_services_restart_in_progress = false;
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window || !main_window->statusBar()) {
		return;
	}
	const QString status = result.found == 0
				       ? QString::fromUtf8(obs_module_text("RestartCameraServicesNoSources"))
				       : QString::fromUtf8(obs_module_text("RestartCameraServicesResult"))
						 .arg(result.refreshed)
						 .arg(result.found);
	main_window->statusBar()->showMessage(status, 5000);
}

static void restart_macos_camera_services()
{
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (camera_services_restart_in_progress) {
		if (main_window && main_window->statusBar()) {
			main_window->statusBar()->showMessage(
				QString::fromUtf8(obs_module_text("RestartCameraServicesInProgress")), 5000);
		}
		return;
	}

	camera_services_restart_in_progress = true; // A camera can disappear during a live output, so keep recording, streaming, and the virtual camera running while the macOS camera services recover; frames can briefly drop, but stopping the output creates a permanent gap. (Codex task: 01a01b14-9ef1-7082-99e7-1885d5d90235)
	auto *process = new QProcess(main_window);
	QObject::connect(process, &QProcess::errorOccurred, [process, main_window](QProcess::ProcessError) {
		if (!camera_services_restart_in_progress) {
			return;
		}
		camera_services_restart_in_progress = false;
		blog(LOG_ERROR, "[Aitum++] Restart Camera Services: could not launch administrator request");
		if (main_window && main_window->statusBar()) {
			main_window->statusBar()->showMessage(
				QString::fromUtf8(obs_module_text("RestartCameraServicesFailed")), 5000);
		}
		process->deleteLater();
	});
	QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			 [process, main_window](int exit_code, QProcess::ExitStatus exit_status) {
				 if (!camera_services_restart_in_progress) {
					 return;
				 }
				 process->deleteLater();
				 if (exit_status != QProcess::NormalExit || exit_code != 0) {
					 camera_services_restart_in_progress = false;
					 blog(LOG_WARNING,
					      "[Aitum++] Restart Camera Services: administrator request cancelled or failed");
					 if (main_window && main_window->statusBar()) {
						 main_window->statusBar()->showMessage(
							 QString::fromUtf8(obs_module_text("RestartCameraServicesFailed")),
							 5000);
					 }
					 return;
				 }

				 blog(LOG_INFO, "[Aitum++] Restart Camera Services: macOS camera services restarted");
				 if (main_window && main_window->statusBar()) {
					 main_window->statusBar()->showMessage(
						 QString::fromUtf8(obs_module_text("RestartCameraServicesRefreshing")), 5000);
				 }
				 QTimer::singleShot(2000, main_window, refresh_macos_camera_sources);
			 });
	process->start(
		QStringLiteral("/usr/bin/osascript"),
		{QStringLiteral("-e"),
		 QStringLiteral("do shell script \"/usr/bin/killall -TERM UVCAssistant cameracaptured VDCAssistant\" "
				"with administrator privileges")});
}

static void restart_obs_via_obscene()
{
	char *profile = obs_frontend_get_current_profile();
	char *scene_collection = obs_frontend_get_current_scene_collection();
	auto scene = obs_frontend_get_current_scene();
	const QString profile_name = QString::fromUtf8(profile ? profile : "");
	const QString scene_collection_name = QString::fromUtf8(scene_collection ? scene_collection : "");
	const QString scene_name = QString::fromUtf8(scene ? obs_source_get_name(scene) : "");
	bfree(profile);
	bfree(scene_collection);
	obs_source_release(scene);

	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (profile_name.isEmpty() || scene_collection_name.isEmpty() || scene_name.isEmpty()) {
		blog(LOG_ERROR, "[Aitum++] Restart OBS: current OBS selection is unavailable");
		if (main_window && main_window->statusBar()) {
			main_window->statusBar()->showMessage(QString::fromUtf8(obs_module_text("RestartOBSContextUnavailable")),
							     5000);
		}
		return;
	}

	QUrl restart_request;
	restart_request.setScheme("obscene");
	restart_request.setHost("restart-obs");
	QUrlQuery restart_query;
	restart_query.addQueryItem("profile", profile_name);
	restart_query.addQueryItem("sceneCollection", scene_collection_name);
	restart_query.addQueryItem("scene", scene_name);
	restart_request.setQuery(restart_query);
	if (!QDesktopServices::openUrl(restart_request)) { // This is a real restart request: the app URL reaches OBScene even when macOS must launch it first, then OBScene checks output state, saves the collection, relaunches the exact profile/collection/scene, and restores the OBS Space. (Codex task: 01a01b14-9ef1-7082-99e7-1885d5d90235)
		blog(LOG_ERROR, "[Aitum++] Restart OBS: OBScene could not be opened");
		if (main_window && main_window->statusBar()) {
			main_window->statusBar()->showMessage(QString::fromUtf8(obs_module_text("RestartOBSOBSceneUnavailable")),
							     5000);
		}
		return;
	}
	blog(LOG_INFO, "[Aitum++] Restart OBS requested through OBScene");
	if (main_window && main_window->statusBar()) {
		main_window->statusBar()->showMessage(QString::fromUtf8(obs_module_text("RestartOBSRequested")), 5000);
	}
}
#endif

bool plugin_metadata_downloaded(void *param, struct file_download_data *file)
{
	UNUSED_PARAMETER(param);
	if (!file || !file->buffer.num) {
		return true;
	}

	auto d = obs_data_create_from_json((const char *)file->buffer.array);
	if (!d) {
		if (plugin_metadata_download_info) {
			download_info_destroy(plugin_metadata_download_info);
			plugin_metadata_download_info = nullptr;
		}
		return true;
	}

	auto data_obj = obs_data_get_obj(d, "data");
	obs_data_release(d);
	if (!data_obj) {
		if (plugin_metadata_download_info) {
			download_info_destroy(plugin_metadata_download_info);
			plugin_metadata_download_info = nullptr;
		}
		return true;
	}

	obs_data_array_t *blocks = obs_data_get_array(data_obj, "partnerBlocks");
	if (obs_data_array_count(blocks) > 0) {
		time_t current_time = time(nullptr);
		auto partnerBlockTime = (time_t)config_get_int(obs_frontend_get_user_config(), "Aitum", "partner_block");
		if (current_time < partnerBlockTime || current_time - partnerBlockTime > 1209600) {
			obs_data_array_addref(blocks);
			QMetaObject::invokeMethod(
				toolbar,
				[blocks] {
					auto before = studioModeAction;
					size_t count = obs_data_array_count(blocks);
					for (size_t i = 0; i < count; i++) {
						obs_data_t *block = obs_data_array_item(blocks, i);
						auto block_type = obs_data_get_string(block, "type");
						if (strcmp(block_type, "LINK") == 0) {
							auto button = new QPushButton(
								QString::fromUtf8(obs_data_get_string(block, "label")));
							button->setStyleSheet(QString::fromUtf8(obs_data_get_string(block, "qss")));
							auto url = QString::fromUtf8(obs_data_get_string(block, "data"));
							button->connect(button, &QPushButton::clicked,
									[url] { QDesktopServices::openUrl(QUrl(url)); });
							partnerBlockActions.append(toolbar->insertWidget(before, button));
						} else if (strcmp(block_type, "IMAGE") == 0) {
							auto image_data = QString::fromUtf8(obs_data_get_string(block, "data"));
							if (image_data.startsWith("data:image/")) {
								auto pos = image_data.indexOf(";");
								auto format = image_data.mid(11, pos - 11);
								QImage image;
								if (image.loadFromData(
									    QByteArray::fromBase64(
										    image_data.mid(pos + 7).toUtf8().constData()),
									    format.toUtf8().constData())) {
									auto label = new AspectRatioPixmapLabel;
									label->setPixmap(QPixmap::fromImage(image));
									label->setAlignment(Qt::AlignCenter);
									label->setStyleSheet(QString::fromUtf8(
										obs_data_get_string(block, "qss")));
									partnerBlockActions.append(
										toolbar->insertWidget(before, label));
								}
							}
						} else if (strcmp(block_type, "LABEL") == 0) {
							auto label =
								new QLabel(QString::fromUtf8(obs_data_get_string(block, "label")));
							label->setOpenExternalLinks(true);
							label->setStyleSheet(QString::fromUtf8(obs_data_get_string(block, "qss")));
							partnerBlockActions.append(toolbar->insertWidget(before, label));
						}
						obs_data_release(block);
					}
					auto close = new QAction("x");
					close->setToolTip(QString::fromUtf8(obs_module_text("ClosePartnerBlock")));
					close->connect(close, &QAction::triggered, [] {
						for (auto &a : partnerBlockActions) {
							toolbar->removeAction(a);
						}
						partnerBlockActions.clear();
						config_set_int(obs_frontend_get_user_config(), "Aitum", "partner_block",
							       (int64_t)time(nullptr));
					});
					toolbar->insertAction(before, close);
					partnerBlockActions.append(close);
					QWidget *spacer = new QWidget();
					spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
					partnerBlockActions.append(toolbar->insertWidget(before, spacer));
					obs_data_array_release(blocks);
				},
				Qt::BlockingQueuedConnection);
		}
	}
	obs_data_array_release(blocks);
	obs_data_array_t *ea = obs_data_get_array(data_obj, "extensions");
	size_t ec = obs_data_array_count(ea);
	if (ec > 0) {
		extensions.clear();
		for (size_t i = 0; i < ec; i++) {
			obs_data_t *extension = obs_data_array_item(ea, i);
			extensions.push_back({obs_data_get_string(extension, "name"), obs_data_get_string(extension, "title"),
					      obs_data_get_string(extension, "url")});
			obs_data_release(extension);
		}
		for (int i = 0; i < modesTabBar->count(); ++i) {
			if (modesTabBar->tabData(i) == QString::fromUtf8("Extensions")) {
				modesTabBar->setTabVisible(i, true);
				break;
			}
		}
	}
	obs_data_array_release(ea);

	if (obs_data_get_bool(data_obj, "show_overlays")) {
		for (int i = 0; i < modesTabBar->count(); ++i) {
			if (modesTabBar->tabData(i) == QString::fromUtf8("Overlays")) {
				modesTabBar->setTabVisible(i, true);

				break;
			}
		}
		QMetaObject::invokeMethod(toolbar, [] {
			auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			obs_frontend_add_dock_by_id("AitumStreamSuiteOverlays", obs_module_text("AitumStreamSuiteOverlays"),
						    new BrowserDock("overlays", "https://chat.aitumsuite.tv/overlays",
								    main_window));
			obs_frontend_add_dock_by_id("AitumStreamSuiteSelect", obs_module_text("AitumStreamSuiteSelect"),
						    new BrowserDock("select", "https://chat.aitumsuite.tv/select", main_window));
		});
	}

	obs_data_release(data_obj);

	if (plugin_metadata_download_info) {
		download_info_destroy(plugin_metadata_download_info);
		plugin_metadata_download_info = nullptr;
	}
	return true;
}

void transition_start(void *, calldata_t *)
{
	QMetaObject::invokeMethod(live_scenes_dock, "MainSceneChanged", Qt::QueuedConnection);
	for (const auto &it : canvas_docks) {
		QMetaObject::invokeMethod(it, "MainSceneChanged", Qt::QueuedConnection);
	}
}

void save_dock_state(QString mode)
{
	// Aitum++: only persist workspace dock layouts while the user has the
	// workspaces explicitly active, so the stock OBS layout never
	// overwrites a stored Aitum workspace state.
	if (!aitum_pp_workspaces_active) {
		return;
	}
	if (mode.isEmpty()) {
		return;
	}
	if (!current_profile_config) {
		return;
	}
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	auto state = main_window->saveState();
	auto b64 = state.toBase64();
	auto state_chars = b64.constData();
	std::string setting_name = "dock_state_" + mode.toStdString();
	obs_data_set_string(current_profile_config, setting_name.c_str(), state_chars);
	auto main_dock = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteMainCanvas"));
	if (!main_dock) {
		main_dock = main_window->findChild<QDockWidget *>(QStringLiteral("previewDock"));
	}
	if (main_dock) {
		setting_name = "dock_state_main_restored_" + mode.toStdString();
		obs_data_set_bool(current_profile_config, setting_name.c_str(), true);
	}
	for (const auto &it : canvas_docks) {
		QMetaObject::invokeMethod(it, "SaveSettings", Q_ARG(bool, false), Q_ARG(QString, mode));
	}
	for (const auto &it : canvas_clone_docks) {
		QMetaObject::invokeMethod(it, "SaveSettings", Q_ARG(bool, false), Q_ARG(QString, mode));
	}
	if (component_dock) {
		QMetaObject::invokeMethod(component_dock, "SaveSettings", Q_ARG(bool, false), Q_ARG(QString, mode));
	}
	if (stats_dock) {
		QMetaObject::invokeMethod(stats_dock, "SaveSettings", Q_ARG(bool, false), Q_ARG(QString, mode));
	}
}

void reset_dock_corners()
{
	auto uc = obs_frontend_get_user_config();
	if (uc) {
		config_set_bool(uc, "BasicWindow", "SideDocks", true);
	}
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	if (main_window->corner(Qt::TopLeftCorner) != Qt::LeftDockWidgetArea) {
		main_window->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
	}
	if (main_window->corner(Qt::TopRightCorner) != Qt::RightDockWidgetArea) {
		main_window->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
	}
	if (main_window->corner(Qt::BottomLeftCorner) != Qt::LeftDockWidgetArea) {
		main_window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
	}
	if (main_window->corner(Qt::BottomRightCorner) != Qt::RightDockWidgetArea) {
		main_window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	}
}

void reset_live_dock_state()
{
	//Shows activity feeds, chat (multi-chat), Game capture change dock, main scenes quick switch dock, canvas previews, multi-stream dock. Hides scene list or sources or anything related to actually making a stream setup and not actually being live
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	reset_dock_corners();

	QMap<QString, enum Qt::DockWidgetArea> allow_docks = {
		{QStringLiteral("AitumStreamSuiteMainCanvas"), Qt::TopDockWidgetArea},
		{QStringLiteral("previewDock"), Qt::TopDockWidgetArea},

		{QStringLiteral("AitumStreamSuiteChat"), Qt::RightDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteActivity"), Qt::RightDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteInfo"), Qt::RightDockWidgetArea},
		{QStringLiteral("AitumStreamSuitePortal"), Qt::RightDockWidgetArea},

		{QStringLiteral("mixerDock"), Qt::BottomDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteOutput"), Qt::BottomDockWidgetArea},

		{QStringLiteral("AitumStreamSuiteLiveScenes"), Qt::LeftDockWidgetArea},
	};

	QList<QDockWidget *> left_docks;
	auto docks = main_window->findChildren<QDockWidget *>();
	for (auto dock : docks) {
		auto canvas_dock = dynamic_cast<CanvasDock *>(dock->widget());
		auto clone_dock = dynamic_cast<CanvasCloneDock *>(dock->widget());
		if (canvas_dock || clone_dock) {
			dock->setVisible(true);
			dock->setFloating(false);
			if (main_window->dockWidgetArea(dock) != Qt::LeftDockWidgetArea) {
				main_window->addDockWidget(Qt::LeftDockWidgetArea, dock);
			}
			if (left_docks.isEmpty()) {
				main_window->addDockWidget(Qt::LeftDockWidgetArea, dock);
				left_docks.append(dock);
			} else {
				main_window->tabifyDockWidget(left_docks.first(), dock);
			}
			if (canvas_dock) {
				canvas_dock->reset_build_state();
			} else if (clone_dock) {
				clone_dock->reset_build_state();
			}
			continue;
		}
		if (allow_docks.contains(dock->objectName())) {
			if (!dock->isVisible()) {
				dock->setVisible(true);
			}
			if (dock->isFloating()) {
				dock->setFloating(false);
			}
			main_window->addDockWidget(allow_docks[dock->objectName()], dock);
		} else {
			if (dock->isVisible()) {
				dock->setVisible(false);
			}
		}
	}

	QList<QDockWidget *> right_docks;
	QList<int> right_dock_sizes;

	auto chat = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteChat"));
	if (chat) {
		right_docks.append(chat);
		right_dock_sizes.append(3);
	}

	auto activity = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteActivity"));
	if (activity) {
		right_docks.append(activity);
		right_dock_sizes.append(3);
		if (chat) {

			main_window->splitDockWidget(chat, activity, Qt::Horizontal);
		}
	}

	auto info = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteInfo"));
	if (info) {
		right_docks.append(info);
		right_dock_sizes.append(1);
	}

	auto portal = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuitePortal"));
	if (portal) {
		right_docks.append(portal);
		right_dock_sizes.append(1);
		if (info) {
			main_window->splitDockWidget(portal, info, Qt::Horizontal);
		}
	}

	main_window->resizeDocks(right_docks, right_dock_sizes, Qt::Vertical);

	auto cw = main_window->centralWidget();
	if (cw && cw->height() > 10 && cw->width() > 10) {
		auto mcd = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteMainCanvas"));
		if (!mcd) {
			mcd = main_window->findChild<QDockWidget *>(QStringLiteral("previewDock"));
		}
		if (mcd) {
			auto area = main_window->dockWidgetArea(mcd);
			if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea) {
				main_window->resizeDocks({mcd}, {mcd->height() + cw->height()}, Qt::Vertical);
			} else if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
				main_window->resizeDocks({mcd}, {mcd->width() + cw->width()}, Qt::Horizontal);
			}
		}
	}
	save_dock_state(QString::fromStdString("Live"));
}

void reset_build_dock_state()
{
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	reset_dock_corners();

	QMap<QString, enum Qt::DockWidgetArea> allow_docks = {
		{QStringLiteral("AitumStreamSuiteScenes"), Qt::LeftDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteSources"), Qt::LeftDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteFilters"), Qt::LeftDockWidgetArea},

		{QStringLiteral("AitumStreamSuiteMainCanvas"), Qt::TopDockWidgetArea},
		{QStringLiteral("previewDock"), Qt::TopDockWidgetArea},

		{QStringLiteral("AitumStreamSuiteProperties"), Qt::RightDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteTransform"), Qt::RightDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteTransitions"), Qt::RightDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteSelect"), Qt::RightDockWidgetArea},

		{QStringLiteral("mixerDock"), Qt::BottomDockWidgetArea},
		{QStringLiteral("SceneNotesDock"), Qt::BottomDockWidgetArea},
	};

	QList<QDockWidget *> top_docks;
	auto docks = main_window->findChildren<QDockWidget *>();
	for (auto dock : docks) {
		auto canvas_dock = dynamic_cast<CanvasDock *>(dock->widget());
		auto clone_dock = dynamic_cast<CanvasCloneDock *>(dock->widget());
		if (canvas_dock || clone_dock) {
			dock->setVisible(true);
			dock->setFloating(false);
			if (main_window->dockWidgetArea(dock) != Qt::TopDockWidgetArea) {
				main_window->addDockWidget(Qt::TopDockWidgetArea, dock);
			}
			if (top_docks.isEmpty()) {
				main_window->addDockWidget(Qt::TopDockWidgetArea, dock);
				top_docks.append(dock);
			} else {
				main_window->tabifyDockWidget(top_docks.first(), dock);
			}
			if (canvas_dock) {
				canvas_dock->reset_build_state();
			} else if (clone_dock) {
				clone_dock->reset_build_state();
			}
			continue;
		}
		if (allow_docks.contains(dock->objectName())) {
			if (!dock->isVisible()) {
				dock->setVisible(true);
			}
			if (dock->isFloating()) {
				dock->setFloating(false);
			}
			if (main_window->dockWidgetArea(dock) != allow_docks[dock->objectName()]) {
				main_window->addDockWidget(allow_docks[dock->objectName()], dock);
			}
		} else {
			if (dock->isVisible()) {
				dock->setVisible(false);
			}
		}
	}

	auto props = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteProperties"));
	auto transform = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteTransform"));
	if (props && transform) {
		main_window->tabifyDockWidget(props, transform);
		auto select = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteSelect"));
		if (select) {
			main_window->tabifyDockWidget(props, select);
		}
	}

	auto mcd = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteMainCanvas"));
	if (!mcd) {
		mcd = main_window->findChild<QDockWidget *>(QStringLiteral("previewDock"));
	}
	if (mcd) {
		if (top_docks.isEmpty()) {
			main_window->addDockWidget(Qt::TopDockWidgetArea, mcd);
			top_docks.append(mcd);
		} else {
			main_window->tabifyDockWidget(top_docks.first(), mcd);
		}
		top_docks.append(mcd);
	}

	auto cw = main_window->centralWidget();
	if (mcd && cw && cw->height() > 10 && cw->width() > 10) {
		auto area = main_window->dockWidgetArea(mcd);
		if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea) {
			main_window->resizeDocks({mcd}, {mcd->height() + cw->height()}, Qt::Vertical);
		} else if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
			main_window->resizeDocks({mcd}, {mcd->width() + cw->width()}, Qt::Horizontal);
		}
	}

	save_dock_state(QString::fromStdString("Build"));
}

void reset_design_dock_state()
{
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	QMetaObject::invokeMethod(main_window, "on_resetDocks_triggered", Q_ARG(bool, true));
	auto d = main_window->findChild<QDockWidget *>(QStringLiteral("controlsDock"));
	if (d) {
		d->setVisible(false);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
	if (d) {
		d->setVisible(true);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteChat"));
	if (d) {
		d->setVisible(false);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteActivity"));
	if (d) {
		d->setVisible(false);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteInfo"));
	if (d) {
		d->setVisible(false);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteOutput"));
	if (d) {
		d->setVisible(false);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteProperties"));
	if (d) {
		d->setVisible(true);
		d->setFloating(false);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteFilters"));
	if (d) {
		d->setVisible(true);
		d->setFloating(false);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteLiveScenes"));
	if (d) {
		d->setVisible(false);
	}

	d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteComponent"));
	if (d) {
		d->setVisible(true);
		d->setFloating(false);
	}

	save_dock_state(QString::fromStdString("Design"));
}

void reset_overlays_dock_state()
{
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto docks = main_window->findChildren<QDockWidget *>();
	QDockWidget *od = nullptr;
	for (auto &d : docks) {
		if (d->objectName() == QStringLiteral("AitumStreamSuiteOverlays")) {
			d->setVisible(true);
			od = d;
		} else {
			d->setVisible(false);
		}
	}
	if (!od) {
		od = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteOverlays"));
	}
	if (!od) {
		return;
	}
	od->setVisible(true);
	od->setFloating(false);
	if (main_window->dockWidgetArea(od) != Qt::TopDockWidgetArea) {
		main_window->addDockWidget(Qt::TopDockWidgetArea, od);
	}
	QMetaObject::invokeMethod(
		main_window,
		[main_window, od] {
			auto cw = main_window->centralWidget();
			if (cw && cw->height() > 10 && cw->width() > 10) {
				main_window->resizeDocks({od}, {main_window->height()}, Qt::Vertical);
			}
		},
		Qt::QueuedConnection);
}

void reset_extensions_dock_state()
{
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto docks = main_window->findChildren<QDockWidget *>();
	for (auto &d : docks) {
		bool found = false;
		for (auto &e : extensions) {
			if (d->objectName() == QString::fromStdString(std::get<0>(e).c_str())) {
				found = true;
				break;
			}
		}
		if (!found) {
			d->setVisible(false);
		}
	}
	std::shuffle(extensions.begin(), extensions.end(), std::default_random_engine());
	bool resize = false;
	QList<QDockWidget *> extension_docks;
	QList<int> extension_dock_sizes;
	for (auto &e : extensions) {
		auto name = QString::fromStdString(std::get<0>(e));
		auto title = QString::fromStdString(std::get<1>(e));
		auto d = main_window->findChild<QDockWidget *>(name);
		if (!d) {
			d = new QDockWidget(title, main_window);
			d->setObjectName(name);
			auto bd = new BrowserDock(std::get<0>(e).c_str(), std::get<2>(e).c_str(), d);
			d->setWidget(bd);
			//obs_frontend_add_dock_by_id(std::get<0>(e).c_str(), std::get<1>(e).c_str(), bd);
			obs_frontend_add_custom_qdock(std::get<0>(e).c_str(), d);
			//d = main_window->findChild<QDockWidget *>(name);
			d->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
		}
		d->setVisible(true);
		d->setFloating(false);
		if (main_window->dockWidgetArea(d) != Qt::TopDockWidgetArea) {
			main_window->addDockWidget(Qt::TopDockWidgetArea, d);
			resize = true;
		}
		extension_docks.append(d);
		extension_dock_sizes.append(1);
	}
	if (extension_docks.isEmpty()) {
		return;
	}
	if (resize) {
		main_window->resizeDocks(extension_docks, extension_dock_sizes, Qt::Horizontal);
	}
	QMetaObject::invokeMethod(
		main_window,
		[main_window, extension_docks] {
			auto cw = main_window->centralWidget();
			if (cw && cw->height() > 10 && cw->width() > 10) {
				QList<int> extension_dock_sizes;
				for (auto i = 0; i < extension_docks.count(); ++i) {
					extension_dock_sizes.append(main_window->height());
				}
				main_window->resizeDocks(extension_docks, extension_dock_sizes, Qt::Vertical);
			}
		},
		Qt::QueuedConnection);
}

std::vector<std::tuple<std::string, void (*)(void), QString, bool>> fixed_tabs = {
	{"Live", reset_live_dock_state, QString::fromUtf8("📡"), false},
	{"Build", reset_build_dock_state, QString::fromUtf8("🔨"), false},
	{"Overlays", reset_overlays_dock_state, QString::fromUtf8("✨"), true},
	{"Extensions", reset_extensions_dock_state, QString::fromUtf8("🔌"), true}};
//,{"Design", reset_design_dock_state, QString::fromUtf8("🎨"), false}};

static bool scene_collection_changing = false;

void load_dock_state(QString mode)
{
	if (!current_profile_config) {
		return;
	}
	scene_collection_changing = false;
	// Aitum++: never rearrange the user's docks unless the workspaces were
	// explicitly activated. OBS keeps its own familiar layout by default.
	if (!aitum_pp_workspaces_active) {
		return;
	}
	std::string state;
	bool main_restored = false;
	std::string setting_name = "dock_state_" + mode.toStdString();
	state = obs_data_get_string(current_profile_config, setting_name.c_str());
	setting_name = "dock_state_main_restored_" + mode.toStdString();
	main_restored = obs_data_get_bool(current_profile_config, setting_name.c_str());
	if (state.empty()) {
		setting_name = "dock_state_" + mode.toLower().toStdString();
		state = obs_data_get_string(current_profile_config, setting_name.c_str());
		setting_name = "dock_state_main_restored_" + mode.toLower().toStdString();
		main_restored = obs_data_get_bool(current_profile_config, setting_name.c_str());
	}
	void (*reset_func)(void) = nullptr;
	for (auto it = fixed_tabs.begin(); it != fixed_tabs.end(); ++it) {
		auto name = std::get<0>(*it);
		auto translated = obs_module_text(name.c_str());
		if ((translated && mode == translated) || mode == QString::fromStdString(name)) {
			if (std::get<3>(*it)) {
				reset_func = std::get<1>(*it);
			} else if (state.empty()) {
				std::get<1> (*it)();
				return;
			}
			break;
		}
	}
	QList<QDockWidget *> visible_canvas_docks;
	loaded_docks.clear();
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!state.empty()) {
		if (!main_window) {
			return;
		}
		main_window->restoreState(QByteArray::fromBase64(state.c_str()));

		auto d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteMainCanvas"));
		if (!d) {
			d = main_window->findChild<QDockWidget *>(QStringLiteral("previewDock"));
		}
		if (d) {
			if (d->isVisible()) {
				visible_canvas_docks.append(d);
			}
			if (!main_restored && !d->isVisibleTo(main_window)) {
				bool canvas_mode = false;
				for (auto it : canvas_docks) {
					if (it->parentWidget()->objectName() == mode) {
						canvas_mode = true;
						break;
					}
				}
				for (auto it : canvas_clone_docks) {
					if (it->parentWidget()->objectName() == mode) {
						canvas_mode = true;
						break;
					}
				}
				if (!canvas_mode) {
					d->setVisible(true);
					d->setFloating(false);
					main_window->addDockWidget(Qt::TopDockWidgetArea, d);
				}
			}

			QMetaObject::invokeMethod(
				main_window,
				[main_window, d] {
					auto cw = main_window->centralWidget();
					if (cw && cw->height() > 10 && cw->width() > 10) {
						auto area = main_window->dockWidgetArea(d);
						if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea) {
							main_window->resizeDocks({d}, {d->height() + cw->height()}, Qt::Vertical);
						} else if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
							main_window->resizeDocks({d}, {d->width() + cw->width()}, Qt::Horizontal);
						}
					}
				},
				Qt::QueuedConnection);
		}

		auto docks = main_window->findChildren<QDockWidget *>();
		for (auto &dock : docks) {
			if (dock->isVisible()) {
				loaded_docks.append(dock->objectName());
			}
		}
	}
	for (const auto &it : canvas_docks) {
		auto dw = qobject_cast<QDockWidget *>(it->parentWidget());
		if (dw && dw->isVisible()) {
			visible_canvas_docks.append(dw);
		}
		QMetaObject::invokeMethod(it, "LoadMode", Qt::QueuedConnection, Q_ARG(QString, mode));
	}
	for (const auto &it : canvas_clone_docks) {
		auto dw = qobject_cast<QDockWidget *>(it->parentWidget());
		if (dw && dw->isVisible()) {
			visible_canvas_docks.append(dw);
		}
		QMetaObject::invokeMethod(it, "LoadMode", Qt::QueuedConnection, Q_ARG(QString, mode));
	}
	if (component_dock) {
		QMetaObject::invokeMethod(component_dock, "LoadMode", Qt::QueuedConnection, Q_ARG(QString, mode));
	}
	if (stats_dock) {
		QMetaObject::invokeMethod(stats_dock, "LoadMode", Qt::QueuedConnection, Q_ARG(QString, mode));
	}
	if (scenes_dock && !visible_canvas_docks.isEmpty()) {
		QMetaObject::invokeMethod(scenes_dock, "UpdateCanvasFromDockList",
					  Q_ARG(QList<QDockWidget *>, visible_canvas_docks));
	}
	if (reset_func) {
		reset_func();
	} else if (main_window && !visible_canvas_docks.empty()) {
		auto fd = visible_canvas_docks.first();
		QMetaObject::invokeMethod(
			main_window,
			[main_window, fd] {
				auto cw = main_window->centralWidget();
				if (cw && cw->height() > 10 && cw->width() > 10) {
					auto area = main_window->dockWidgetArea(fd);
					if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea) {
						main_window->resizeDocks({fd}, {fd->height() + cw->height()}, Qt::Vertical);
					} else if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
						main_window->resizeDocks({fd}, {fd->width() + cw->width()}, Qt::Horizontal);
					}
				}
			},
			Qt::QueuedConnection);
	}
}

// Aitum++: show only the minimum useful controls, the extra canvases and the
// outputs dock, without touching the rest of the OBS layout.
static void aitum_pp_show_minimal_controls()
{
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	QList<QDockWidget *> docks;
	for (const auto &it : canvas_docks) {
		auto dw = qobject_cast<QDockWidget *>(it->parentWidget());
		if (dw) {
			docks.append(dw);
		}
	}
	for (const auto &it : canvas_clone_docks) {
		auto dw = qobject_cast<QDockWidget *>(it->parentWidget());
		if (dw) {
			docks.append(dw);
		}
	}
	auto output = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteOutput"));
	if (output) {
		docks.append(output);
	}
	QDockWidget *first_added = nullptr;
	for (const auto &dock : docks) {
		dock->setFloating(false);
		if (main_window->dockWidgetArea(dock) == Qt::NoDockWidgetArea) {
			if (first_added) {
				main_window->tabifyDockWidget(first_added, dock);
			} else {
				main_window->addDockWidget(Qt::RightDockWidgetArea, dock);
				first_added = dock;
			}
		}
		dock->setVisible(true);
		dock->raise();
	}
}

// Aitum++: hide every plugin-owned dock so a session that ended in an Aitum
// workspace cannot replace the stock OBS docks on the next launch. The main
// canvas dock is excluded because creating it already requires an explicit
// Aitum/MainCanvasDock opt-in. Ordinary OBS/user docks are never touched.
static void aitum_pp_hide_additive_docks()
{
	if (aitum_pp_controls_active || aitum_pp_workspaces_active) {
		return;
	}
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	for (const auto &it : canvas_docks) {
		auto dw = qobject_cast<QDockWidget *>(it->parentWidget());
		if (dw && dw->isVisible()) {
			dw->setVisible(false);
		}
	}
	for (const auto &it : canvas_clone_docks) {
		auto dw = qobject_cast<QDockWidget *>(it->parentWidget());
		if (dw && dw->isVisible()) {
			dw->setVisible(false);
		}
	}
	for (const auto &it : empty_docks) {
		auto dw = qobject_cast<QDockWidget *>(it->parentWidget());
		if (dw && dw->isVisible()) {
			dw->setVisible(false);
		}
	}
	for (const auto &dock : main_window->findChildren<QDockWidget *>()) {
		if (dock->objectName().startsWith(QStringLiteral("AitumStreamSuite")) &&
		    dock->objectName() != QStringLiteral("AitumStreamSuiteMainCanvas") && dock->isVisible()) {
			dock->setVisible(false);
		}
	}
}

// Aitum++: OBS has already restored the live dock layout before plugin docks are registered. Reapplying the saved DockState here hid stock docks on macOS, so only remove additive docks and capture the live layout.
static void aitum_pp_preserve_live_obs_layout()
{
	if (aitum_pp_controls_active || aitum_pp_workspaces_active) {
		return;
	}
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	aitum_pp_hide_additive_docks();
	aitum_pp_normal_state = main_window->saveState();
	blog(LOG_INFO, "[Aitum++] Preserved live OBS dock layout");
}

static void aitum_pp_capture_normal_state()
{
	if (aitum_pp_controls_active || aitum_pp_workspaces_active) {
		return;
	}
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (main_window) {
		aitum_pp_normal_state = main_window->saveState();
	}
}

static void aitum_pp_restore_normal_state()
{
	if (aitum_pp_workspaces_active) {
		return;
	}
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	if (!aitum_pp_normal_state.isEmpty()) {
		main_window->restoreState(aitum_pp_normal_state);
	}
	if (toolbar) {
		toolbar->hide();
	}
	if (aitum_pp_controls_active) {
		aitum_pp_show_minimal_controls();
	}
}

static void aitum_pp_set_controls_active(bool active)
{
	if (active == aitum_pp_controls_active) {
		return;
	}
	if (active) {
		aitum_pp_capture_normal_state();
		aitum_pp_controls_active = true;
		aitum_pp_show_minimal_controls();
	} else {
		aitum_pp_controls_active = false;
		aitum_pp_restore_normal_state();
	}
	if (aitumPPControlsAction && aitumPPControlsAction->isChecked() != active) {
		aitumPPControlsAction->setChecked(active);
	}
}

static void aitum_pp_set_workspaces_active(bool active)
{
	if (active == aitum_pp_workspaces_active) {
		return;
	}
	if (active) {
		aitum_pp_capture_normal_state();
		aitum_pp_workspaces_active = true;
		// Load the workspace state before showing the toolbar so the
		// toolbar's resize driven auto-save cannot overwrite a stored
		// workspace layout with the normal OBS layout.
		if (modesTabBar) {
			auto index = modesTabBar->currentIndex();
			if (index >= 0) {
				auto d = modesTabBar->tabData(index);
				if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
					modesTab = d.toString();
					load_dock_state(d.toString());
				} else {
					modesTab = modesTabBar->tabText(index);
					load_dock_state(modesTabBar->tabText(index));
				}
			}
		}
		if (toolbar) {
			toolbar->show();
		}
	} else {
		// Keep the user's current workspace arrangement before leaving.
		if (!current_profile_config || !obs_data_get_bool(current_profile_config, "dock_mode_manual_save")) {
			save_dock_state(modesTab);
		}
		aitum_pp_workspaces_active = false;
		if (toolbar) {
			toolbar->hide();
		}
		aitum_pp_restore_normal_state();
	}
	if (aitumPPWorkspacesAction && aitumPPWorkspacesAction->isChecked() != active) {
		aitumPPWorkspacesAction->setChecked(active);
	}
}

void load_outputs()
{
	QMetaObject::invokeMethod(
		output_dock,
		[] {
			if (output_dock) {
				output_dock->LoadSettings();
			}
		},
		Qt::QueuedConnection);
}

void reset_canvas_dock_state(QString name)
{
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	reset_dock_corners();

	bool main = false;
	QDockWidget *cd = nullptr;
	if (name == "Main") {
		auto d = main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteMainCanvas"));
		if (!d) {
			d = main_window->findChild<QDockWidget *>(QStringLiteral("previewDock"));
		}
		cd = d;
		main = true;
	}

	QMap<QString, enum Qt::DockWidgetArea> allow_docks = {
		{QStringLiteral("AitumStreamSuiteScenes"), Qt::LeftDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteSources"), Qt::LeftDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteFilters"), Qt::LeftDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteTransitions"), Qt::LeftDockWidgetArea},

		{QStringLiteral("AitumStreamSuiteProperties"), Qt::RightDockWidgetArea},
		{QStringLiteral("AitumStreamSuiteTransform"), Qt::RightDockWidgetArea},
		{QStringLiteral("mixerDock"), Qt::BottomDockWidgetArea},
	};
	if (main) {
		allow_docks.insert(QStringLiteral("AitumStreamSuiteMainCanvas"), Qt::TopDockWidgetArea);
		allow_docks.insert(QStringLiteral("previewDock"), Qt::TopDockWidgetArea);
		allow_docks.insert(QStringLiteral("SceneNotesDock"), Qt::BottomDockWidgetArea);
	}

	auto docks = main_window->findChildren<QDockWidget *>();
	for (auto dock : docks) {
		auto canvas_dock = dynamic_cast<CanvasDock *>(dock->widget());
		auto clone_dock = dynamic_cast<CanvasCloneDock *>(dock->widget());
		if (canvas_dock || clone_dock) {
			if (!main && dock->objectName() == name) {
				cd = dock;
				dock->setVisible(true);
				dock->setFloating(false);
				if (main_window->dockWidgetArea(dock) != Qt::TopDockWidgetArea) {
					main_window->addDockWidget(Qt::TopDockWidgetArea, dock);
				}

				if (canvas_dock) {
					canvas_dock->reset_build_state();
				} else if (clone_dock) {
					clone_dock->reset_build_state();
				}
			} else if (dock->isVisible()) {
				dock->setVisible(false);
			}
			continue;
		}
		if (allow_docks.contains(dock->objectName())) {
			if (!dock->isVisible()) {
				dock->setVisible(true);
			}
			if (dock->isFloating()) {
				dock->setFloating(false);
			}
			if (main_window->dockWidgetArea(dock) != allow_docks[dock->objectName()]) {
				main_window->addDockWidget(allow_docks[dock->objectName()], dock);
			}
		} else {
			if (dock->isVisible()) {
				dock->setVisible(false);
			}
		}
	}
}

void create_new_dock_mode(const char *name)
{
	QString qname = QString::fromUtf8(name);
	for (int i = 0; i < modesTabBar->count(); i++) {
		if (modesTabBar->tabText(i) == qname) {
			return;
		}
	}

	auto index = modesTabBar->addTab(qname);
	modesTabBar->setCurrentIndex(index);
	// Aitum++: only rearrange docks for the new mode while the workspaces
	// are explicitly active.
	if (aitum_pp_workspaces_active) {
		reset_canvas_dock_state(name);
		save_dock_state(qname);
	}
}

void load_canvas(bool check_new_canvas)
{
	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window) {
		return;
	}
	auto canvas = obs_data_get_array(current_profile_config, "canvas");
	auto canvas_count = obs_data_array_count(canvas);
	for (size_t i = 0; i < canvas_count;) {
		obs_data_t *t = obs_data_array_item(canvas, i);
		if (!t) {
			i++;
			continue;
		}
		if (obs_data_get_bool(t, "delete")) {
			const char *canvas_name = obs_data_get_string(t, "name");
			obs_frontend_remove_dock(canvas_name);
			auto uuid = obs_data_get_string(t, "uuid");
			for (const auto &it : canvas_docks) {
				if (strcmp(obs_canvas_get_uuid(it->GetCanvas()), uuid) == 0) {
					obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
					break;
				}
			}
			for (const auto &it : canvas_clone_docks) {
				if (strcmp(obs_canvas_get_uuid(it->GetCanvas()), uuid) == 0) {
					obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
					break;
				}
			}
			obs_canvas_t *c = obs_get_canvas_by_uuid(uuid);
			if (c && obs_canvas_removed(c)) {
				obs_canvas_release(c);
				c = nullptr;
			}
			if (!c) {
				c = obs_get_canvas_by_name(canvas_name);
			}
			if (c && obs_canvas_removed(c)) {
				obs_canvas_release(c);
				c = nullptr;
			}
			if (c) {
				obs_frontend_remove_canvas(c);
				obs_canvas_remove(c);
				obs_canvas_release(c);
			}
			obs_data_array_erase(canvas, i);
			obs_data_release(t);
			canvas_count--;
		} else if (strcmp(obs_data_get_string(t, "type"), "clone") == 0) {
			auto uuid = obs_data_get_string(t, "uuid");
			auto name = obs_data_get_string(t, "name");
			for (const auto &it : canvas_docks) {
				if (strcmp(obs_canvas_get_uuid(it->GetCanvas()), uuid) == 0) {
					obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
					check_new_canvas = false;
					break;
				}
			}
			CanvasCloneDock *ccd = nullptr;
			for (const auto &it : canvas_clone_docks) {
				auto cuuid = obs_canvas_get_uuid(it->GetCanvas());
				if (strcmp(cuuid, uuid) == 0) {
					if (obs_canvas_removed(it->GetCanvas()) ||
					    strcmp(it->parentWidget()->objectName().toUtf8().constData(), name) != 0) {
						// canvas name changed, remove old dock and create a new one
						obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
						check_new_canvas = false;
					} else {
						ccd = it;
					}
					break;
				} else if (strcmp(it->parentWidget()->objectName().toUtf8().constData(), name) == 0) {
					if (obs_canvas_removed(it->GetCanvas())) {
						obs_frontend_remove_dock(name);
						check_new_canvas = false;
						break;
					}
					obs_data_set_string(t, "uuid", cuuid);
					ccd = it;
					break;
				}
			}
			if (ccd) {
				ccd->UpdateSettings(t);
			} else {
				ccd = new CanvasCloneDock(t, main_window);
				std::string title = "🧬 ";
				title += name;
				if (obs_frontend_add_dock_by_id(name, title.c_str(), ccd)) {
					canvas_clone_docks.push_back(ccd);
					if (!obs_data_get_bool(t, "has_loaded")) {
						ccd->parentWidget()->show();
						obs_data_set_bool(t, "has_loaded", true);
					}
					if (check_new_canvas) {
						create_new_dock_mode(name);
					}
				} else {
					delete ccd;
				}
			}
			i++;
		} else {
			auto uuid = obs_data_get_string(t, "uuid");
			auto name = obs_data_get_string(t, "name");
			for (const auto &it : canvas_clone_docks) {
				if (strcmp(obs_canvas_get_uuid(it->GetCanvas()), uuid) == 0) {
					obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
					check_new_canvas = false;
					break;
				}
			}
			CanvasDock *cd = nullptr;
			for (const auto &it : canvas_docks) {
				auto cuuid = obs_canvas_get_uuid(it->GetCanvas());
				if (strcmp(cuuid, uuid) == 0) {
					if (obs_canvas_removed(it->GetCanvas()) ||
					    strcmp(it->parentWidget()->objectName().toUtf8().constData(), name) != 0) {
						// canvas name changed, remove old dock and create a new one
						obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
						check_new_canvas = false;
					} else {
						cd = it;
					}
					break;
				} else if (strcmp(it->parentWidget()->objectName().toUtf8().constData(), name) == 0) {
					if (obs_canvas_removed(it->GetCanvas())) {
						obs_frontend_remove_dock(name);
						check_new_canvas = false;
						break;
					}
					obs_data_set_string(t, "uuid", cuuid);
					cd = it;
					break;
				}
			}

			if (cd) {
				cd->UpdateSettings(t);
			} else {
				cd = new CanvasDock(t, main_window);
				std::string title = "🖼️ ";
				title += name;
				if (obs_frontend_add_dock_by_id(name, title.c_str(), cd)) {
					canvas_docks.push_back(cd);
					if (!obs_data_get_bool(t, "has_loaded")) {
						cd->parentWidget()->show();
						obs_data_set_bool(t, "has_loaded", true);
					}
					if (check_new_canvas) {
						create_new_dock_mode(name);
					}
				} else {
					delete cd;
				}
			}
			i++;
		}
	}
	obs_data_array_release(canvas);
	for (const auto &it : canvas_clone_docks) {
		it->UpdateSettings(nullptr);
	}
}

void save_current_profile_config(bool save_docks)
{
	if (!current_profile_config) {
		return;
	}
	char *profile_path = obs_frontend_get_current_profile_path();
	if (!profile_path) {
		return;
	}

	struct dstr path;
	dstr_init_copy(&path, profile_path);
	bfree(profile_path);

	if (!dstr_is_empty(&path) && dstr_end(&path) != '/') {
		dstr_cat_ch(&path, '/');
	}
	dstr_cat(&path, "aitum.json");

	if (save_docks) {
		if (!obs_data_get_bool(current_profile_config, "dock_mode_manual_save")) {
			auto index = modesTabBar->currentIndex();
			if (index >= 0) {
				QString tn;
				auto d = modesTabBar->tabData(index);
				if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
					tn = d.toString();
				} else {
					tn = modesTabBar->tabText(index);
				}
				save_dock_state(tn);
				obs_data_set_string(current_profile_config, "dock_state_mode", tn.toUtf8().constData());
			}
		}
		auto custom_modes = obs_data_array_create();
		for (int i = 0; i < modesTabBar->count(); i++) {
			auto d = modesTabBar->tabData(i);
			if (d.isNull() || !d.isValid() || d.toString().isEmpty()) {
				auto cm = obs_data_create();
				obs_data_set_string(cm, "name", modesTabBar->tabText(i).toUtf8().constData());
				obs_data_array_push_back(custom_modes, cm);
				obs_data_release(cm);
			}
		}
		obs_data_set_array(current_profile_config, "custom_dock_modes", custom_modes);
		obs_data_array_release(custom_modes);
	}
	if (output_dock) {
		output_dock->SaveSettings();
	}

	if (!obs_data_save_json_safe(current_profile_config, path.array, "tmp", "bak")) {
		blog(LOG_WARNING, "[Aitum Stream Suite] Failed to save configuration file");
	} else {
		blog(LOG_INFO, "[Aitum Stream Suite] Saved configuration file");
	}

	dstr_free(&path);
}

void load_current_profile_config()
{
	obs_data_release(current_profile_config);
	current_profile_config = nullptr;

	char *profile_path = obs_frontend_get_current_profile_path();
	if (!profile_path) {
		return;
	}

	struct dstr path;
	dstr_init_copy(&path, profile_path);
	bfree(profile_path);

	if (!dstr_is_empty(&path) && dstr_end(&path) != '/') {
		dstr_cat_ch(&path, '/');
	}
	dstr_cat(&path, "aitum.json");

	current_profile_config = obs_data_create_from_json_file_safe(path.array, "bak");
	dstr_free(&path);
	if (!current_profile_config) {
		current_profile_config = obs_data_create();
		obs_data_set_bool(current_profile_config, "main_stream_output_show", true);
		obs_data_set_bool(current_profile_config, "main_record_output_show", true);
		obs_data_set_bool(current_profile_config, "main_backtrack_output_show", true);
		obs_data_set_bool(current_profile_config, "main_virtual_cam_output_show", true);
		blog(LOG_WARNING, "[Aitum Stream Suite] No configuration file loaded");
		if (modesTabBar->count() <= (int)fixed_tabs.size()) {
			auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			if (main_window) {
				auto main_dock =
					main_window->findChild<QDockWidget *>(QStringLiteral("AitumStreamSuiteMainCanvas"));
				if (!main_dock) {
					main_dock = main_window->findChild<QDockWidget *>(QStringLiteral("previewDock"));
				}
				if (main_dock) {
					main_dock->setVisible(true);
					main_dock->setFloating(false);
					main_window->addDockWidget(Qt::TopDockWidgetArea, main_dock);
				}
			}
			modesTab = "";
			auto tn = QString::fromUtf8(obs_module_text("User"));
			auto index = modesTabBar->addTab(tn);
			modesTabBar->setCurrentIndex(index);
			save_dock_state(tn);
			save_current_profile_config(true);
		}
	} else {
		blog(LOG_INFO, "[Aitum Stream Suite] Loaded configuration file");
	}
	for (const auto &it : empty_docks) {
		obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
	}
	empty_docks.clear();
	auto ed = obs_data_get_array(current_profile_config, "empty_docks");
	if (ed) {
		obs_data_array_enum(
			ed,
			[](obs_data_t *data, void *param) {
				UNUSED_PARAMETER(param);
				auto empty_dock = new QFrame;
				std::string title = "⬜ ";
				title += obs_data_get_string(data, "name");
				if (obs_frontend_add_dock_by_id(obs_data_get_string(data, "name"), title.c_str(), empty_dock)) {
					empty_docks.push_back(empty_dock);
				} else {
					delete empty_dock;
				}
			},
			nullptr);
		obs_data_array_release(ed);
	}

	bool first_create = false;
	auto canvas = obs_data_get_array(current_profile_config, "canvas");
	if (!canvas) {
		first_create = true;
		canvas = obs_data_array_create();
		const char *canvas_name = "Vertical";
		auto vertical_canvas = obs_get_canvas_by_name("Aitum Vertical");
		if (vertical_canvas) {
			obs_canvas_release(vertical_canvas);
			canvas_name = "Aitum Vertical";
		}
		auto new_canvas = obs_data_create();
		obs_data_set_string(new_canvas, "name", canvas_name);
		obs_data_set_int(new_canvas, "color", 0x1F1A17);
		obs_data_array_push_back(canvas, new_canvas);
		obs_data_release(new_canvas);

		obs_data_set_array(current_profile_config, "canvas", canvas);

		auto outputs2 = obs_data_get_array(current_profile_config, "outputs");
		if (!outputs2) {
			outputs2 = obs_data_array_create();
			obs_data_set_array(current_profile_config, "outputs", outputs2);
		}
		if (obs_data_array_count(outputs2) < 1) {
			auto new_output = obs_data_create();
			obs_data_set_bool(new_output, "enabled", true);
			obs_data_set_string(new_output, "type", "stream");
			obs_data_set_string(new_output, "name", "Vertical Stream");
			obs_data_set_string(new_output, "canvas", canvas_name);
			obs_data_array_push_back(outputs2, new_output);
			obs_data_release(new_output);

			new_output = obs_data_create();
			obs_data_set_bool(new_output, "enabled", true);
			obs_data_set_string(new_output, "type", "backtrack");

			config_t *config = obs_frontend_get_profile_config();
			const char *mode = config_get_string(config, "Output", "Mode");
			const char *path = nullptr;
			if (mode && strcmp(mode, "Advanced") == 0) {
				path = config_get_string(config, "AdvOut", "RecFilePath");
			}
			if (!path || path[0] == '\0') {
				path = config_get_string(config, "SimpleOutput", "FilePath");
			}
			if (path) {
				obs_data_set_string(new_output, "path", path);
			}
			obs_data_set_string(new_output, "filename", "%CCYY-%MM-%DD %hh-%mm-%ss Vertical Backtrack");
			obs_data_set_string(new_output, "format", "hybrid_mp4");
			obs_data_set_string(new_output, "name", "Vertical Backtrack");
			obs_data_set_string(new_output, "canvas", canvas_name);
			obs_data_set_int(new_output, "max_time_sec", 10);
			obs_data_array_push_back(outputs2, new_output);
			obs_data_release(new_output);

			new_output = obs_data_create();
			obs_data_set_bool(new_output, "enabled", true);
			obs_data_set_string(new_output, "type", "virtual_cam");
			obs_data_set_string(new_output, "name", "Vertical Virtual Camera");
			obs_data_set_string(new_output, "canvas", canvas_name);
			obs_data_array_push_back(outputs2, new_output);
			obs_data_release(new_output);
		}
		obs_data_array_release(outputs2);
	} else {
		obs_data_array_release(canvas);
	}
	auto dock_modes = obs_data_get_array(current_profile_config, "custom_dock_modes");
	obs_data_array_enum(
		dock_modes,
		[](obs_data_t *data, void *) {
			auto name = QString::fromUtf8(obs_data_get_string(data, "name"));
			for (int i = 0; i < modesTabBar->count(); i++) {
				auto d = modesTabBar->tabData(i);
				if (!d.isNull() && d.isValid() && d.toString() == name) {
					return;
				} else if (modesTabBar->tabText(i) == name) {
					return;
				}
			}
			modesTabBar->addTab(name);
		},
		nullptr);
	if (!first_create) {
		for (int i = modesTabBar->count() - 1; i >= 0; i--) {
			auto d = modesTabBar->tabData(i);
			if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
				continue;
			}
			bool found = false;
			for (size_t j = 0; j < obs_data_array_count(dock_modes); j++) {
				auto dm = obs_data_array_item(dock_modes, j);
				auto name = QString::fromUtf8(obs_data_get_string(dm, "name"));
				obs_data_release(dm);
				if (name == modesTabBar->tabText(i)) {
					found = true;
					break;
				}
			}
			if (!found) {
				modesTabBar->removeTab(i);
			}
		}
	}

	obs_data_array_release(dock_modes);

	auto dsm = obs_data_item_byname(current_profile_config, "dock_state_mode");
	if (obs_data_item_gettype(dsm) == OBS_DATA_STRING) {
		auto mode = QString::fromUtf8(obs_data_item_get_string(dsm));
		for (int i = 0; i < modesTabBar->count(); i++) {
			auto d = modesTabBar->tabData(i);
			if (!d.isNull() && d.isValid() && d.toString() == mode) {
				if (modesTabBar->currentIndex() != i) {
					modesTab = "";
					modesTabBar->setCurrentIndex(i);
				}
				break;
			} else if (modesTabBar->tabText(i) == mode) {
				if (modesTabBar->currentIndex() != i) {
					modesTab = "";
					modesTabBar->setCurrentIndex(i);
				}
				break;
			}
		}
	} else {
		auto index = obs_data_item_get_int(dsm);
		if (modesTabBar->currentIndex() != index) {
			modesTab = "";
			modesTabBar->setCurrentIndex(index);
		}
	}
	obs_data_item_release(&dsm);
	load_canvas(first_create);
	if (first_create) {
		create_new_dock_mode("Main");
	}

	load_outputs();

	QMetaObject::invokeMethod(
		modesTabBar,
		[] {
			auto index = modesTabBar->currentIndex();
			if (index >= 0) {
				auto d = modesTabBar->tabData(index);
				if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
					load_dock_state(d.toString());
				} else {
					load_dock_state(modesTabBar->tabText(index));
				}
			}
		},
		Qt::QueuedConnection);
	// Aitum++: the additive docks now exist, so hide them without replaying
	// OBS's saved layout. Queued so it runs after pending dock events; no-op
	// while Aitum++ is active.
	QMetaObject::invokeMethod(modesTabBar, [] { aitum_pp_preserve_live_obs_layout(); }, Qt::QueuedConnection);
}

bool load_cef();

void load_browser_panels()
{
	if (!load_cef()) {
		return;
	}

	auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	obs_frontend_add_dock_by_id("AitumStreamSuiteChat", obs_module_text("AitumStreamSuiteChat"),
				    new BrowserDock("chat", "https://chat.aitumsuite.tv/chat", main_window));
	obs_frontend_add_dock_by_id("AitumStreamSuiteActivity", obs_module_text("AitumStreamSuiteActivity"),
				    new BrowserDock("activity", "https://chat.aitumsuite.tv/activity", main_window));
	obs_frontend_add_dock_by_id("AitumStreamSuiteInfo", obs_module_text("AitumStreamSuiteInfo"),
				    new BrowserDock("info", "https://chat.aitumsuite.tv/info", main_window));
	obs_frontend_add_dock_by_id("AitumStreamSuitePortal", obs_module_text("AitumStreamSuitePortal"),
				    new BrowserDock("portal", "https://chat.aitumsuite.tv/portal", main_window));
}

void unload_browser_panels()
{
	obs_frontend_remove_dock("AitumStreamSuiteChat");
	obs_frontend_remove_dock("AitumStreamSuiteActivity");
	obs_frontend_remove_dock("AitumStreamSuiteInfo");
	obs_frontend_remove_dock("AitumStreamSuitePortal");
	obs_frontend_remove_dock("AitumStreamSuiteOverlays");
	obs_frontend_remove_dock("AitumStreamSuiteSelect");
}

static bool all_string_settings_the_same(obs_data_t *settings_a, obs_data_t *settings_b)
{
	size_t string_count = 0;
	obs_data_item_t *i = obs_data_first(settings_a);
	while (i) {
		const enum obs_data_type t = obs_data_item_gettype(i);
		if (t == OBS_DATA_STRING) {
			string_count++;
			const char *name = obs_data_item_get_name(i);
			const char *value_a = obs_data_item_get_string(i);
			const char *value_b = obs_data_get_string(settings_b, name);
			if (strcmp(value_a, value_b) != 0) {
				return false;
			}
		}
		obs_data_item_next(&i);
	}
	return string_count > 0;
}

static void log_same_sources()
{
	std::list<obs_source_t *> sources;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			auto sources = static_cast<std::list<obs_source_t *> *>(data);
			auto id_a = obs_source_get_id(source);
			auto settings_a = obs_source_get_settings(source);
			bool do_not_duplicate = obs_source_get_output_flags(source) & OBS_SOURCE_DO_NOT_DUPLICATE;
			if (settings_a) {
				const char *json_a = obs_data_get_json(settings_a);
				for (auto &it : (*sources)) {
					auto id_b = obs_source_get_id(it);
					if (strcmp(id_a, id_b) == 0) {
						auto settings_b = obs_source_get_settings(it);
						if (settings_b) {
							const char *json_b = obs_data_get_json(settings_b);
							if (strcmp(json_a, json_b) == 0) {
								blog(LOG_WARNING,
								     "[Aitum Stream Suite] Duplicate source found: '%s', '%s'",
								     obs_source_get_name(source), obs_source_get_name(it));
							} else if (do_not_duplicate &&
								   all_string_settings_the_same(settings_a, settings_b)) {
								blog(LOG_WARNING,
								     "[Aitum Stream Suite] Similar source found: '%s', '%s'",
								     obs_source_get_name(source), obs_source_get_name(it));
							}
							obs_data_release(settings_b);
						}
					}
				}
				obs_data_release(settings_a);
			}
			sources->push_back(obs_source_get_ref(source));
			return true;
		},
		&sources);
	for (auto &it : sources) {
		obs_source_release(it);
	}
}

struct QCef;
extern QCef *cef;
void DestroyPanelCookieManager();
static bool restart = false;

static void frontend_event(enum obs_frontend_event event, void *private_data)
{
	UNUSED_PARAMETER(private_data);
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		finished_loading = true;
		// Aitum++: OBS may have restored a window state that had the
		// workspace toolbar or the additive docks visible (e.g. from a
		// stock install or a session that ended with Aitum++ active);
		// hide only those additions and keep the live stock layout.
		if (toolbar) {
			QMetaObject::invokeMethod(
				toolbar,
				[] {
					if (toolbar && !aitum_pp_workspaces_active) {
						toolbar->hide();
					}
					aitum_pp_preserve_live_obs_layout();
					aitum_pp_set_workspaces_active(true);
				},
				Qt::QueuedConnection);
		}
		if (restart) {
			const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			if (!main_window) {
				return;
			}
			QTimer::singleShot(2000, main_window, [main_window] {
				auto dialogs = main_window->findChildren<QDialog *>();
				for (auto dialog : dialogs) {
					dialog->close();
				}
				QMetaObject::invokeMethod(main_window, "close", Qt::QueuedConnection);
			});
			return;
		}
		log_same_sources();
		load_browser_panels();
		struct obs_frontend_source_list transitions = {};
		obs_frontend_get_transitions(&transitions);
		for (size_t i = 0; i < transitions.sources.num; i++) {
			auto sh = obs_source_get_signal_handler(transitions.sources.array[i]);
			signal_handler_connect(sh, "transition_start", transition_start, nullptr);
		}
		obs_frontend_source_list_free(&transitions);

		load_current_profile_config();
		auto scene = obs_frontend_get_current_scene();
		if (scene) {
			if (properties_dock) {
				QMetaObject::invokeMethod(properties_dock, "SceneChanged", Qt::QueuedConnection,
							  Q_ARG(OBSSource, OBSSource(scene)));
			}
			obs_source_release(scene);
		}
		if (scenes_dock) {
			QMetaObject::invokeMethod(scenes_dock, "FinishedLoading", Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED) {
		DestroyPanelCookieManager();
		load_browser_panels();
		load_current_profile_config();
	} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING) {
		save_current_profile_config(true);
		unload_browser_panels();
	} else if (event == OBS_FRONTEND_EVENT_EXIT || event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		if (current_profile_config) {
			obs_data_release(current_profile_config);
			current_profile_config = nullptr;
		}
		if (output_dock) {
			output_dock->Exiting();
		}
		if (properties_dock) {
			properties_dock->Exiting();
		}
		for (auto &it : empty_docks) {
			obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
		}
		empty_docks.clear();
		obs_queue_task(
			OBS_TASK_GRAPHICS,
			[](void *) {
				obs_queue_task(
					OBS_TASK_UI,
					[](void *) {
						unload_browser_panels();
						const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
						for (auto &e : extensions) {
							auto d = main_window ? main_window->findChild<QDockWidget *>(
										       QString::fromStdString(std::get<0>(e)))
									     : nullptr;
							obs_frontend_remove_dock(std::get<0>(e).c_str());
							if (d) {
								main_window->removeDockWidget(d);
								d->deleteLater();
							}
						}
						DestroyPanelCookieManager();
						if (cef) {
							delete cef;
							cef = nullptr;
						}
					},
					nullptr, false);
			},
			nullptr, false);
	} else if (event == OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED) {
		if (studioModeAction && !studioModeAction->isChecked()) {
			studioModeAction->setChecked(true);
		}
	} else if (event == OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED) {
		if (studioModeAction && studioModeAction->isChecked()) {
			studioModeAction->setChecked(false);
		}
	} else if (event == OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED) {
		if (output_dock) {
			output_dock->UpdateMainVirtualCameraStatus(true);
		}
	} else if (event == OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED) {
		if (output_dock) {
			output_dock->UpdateMainVirtualCameraStatus(false);
		}
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING) {
		if (output_dock) {
			output_dock->UpdateMainStreamStarting();
		}
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
		if (output_dock) {
			output_dock->UpdateMainStreamStatus(true);
		}
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPING) {
		if (output_dock) {
			output_dock->UpdateMainStreamStopping();
		}
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
		if (output_dock) {
			output_dock->UpdateMainStreamStatus(false);
		}
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STARTING || event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		if (output_dock) {
			output_dock->UpdateMainRecordingStatus(true);
		}
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPING || event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		if (output_dock) {
			output_dock->UpdateMainRecordingStatus(false);
		}
	} else if (event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING || event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED) {
		if (output_dock) {
			output_dock->UpdateMainBacktrackStatus(true);
		}
	} else if (event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING || event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED) {
		if (output_dock) {
			output_dock->UpdateMainBacktrackStatus(false);
		}
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		if (live_scenes_dock) {
			QMetaObject::invokeMethod(live_scenes_dock, "MainSceneChanged", Qt::QueuedConnection);
		}
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainSceneChanged", Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		if (finished_loading) {
			log_same_sources();
			struct obs_frontend_source_list transitions = {};
			obs_frontend_get_transitions(&transitions);
			for (size_t i = 0; i < transitions.sources.num; i++) {
				auto sh = obs_source_get_signal_handler(transitions.sources.array[i]);
				signal_handler_connect(sh, "transition_start", transition_start, nullptr);
			}
			obs_frontend_source_list_free(&transitions);
			load_current_profile_config();
			if (scenes_dock) {
				QMetaObject::invokeMethod(scenes_dock, "BindMainCanvas", Qt::QueuedConnection); // Switching collections destroys the canvas tracked by the shared Sources dock, so bind it to the new main canvas after Aitum reloads the collection. (Codex task: 01a01b14-9ef1-7082-99e7-1885d5d90235)
			}

			auto scene = obs_frontend_get_current_scene();
			if (scene) {
				if (properties_dock) {
					QMetaObject::invokeMethod(properties_dock, "SceneChanged", Qt::QueuedConnection,
								  Q_ARG(OBSSource, OBSSource(scene)));
				}
				obs_source_release(scene);
			}
		}
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
		scene_collection_changing = true;
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		for (auto i = canvas_clone_docks.size(); i > 0; i--) {
			auto it = canvas_clone_docks.begin();
			std::advance(it, i - 1);
			auto dock = (*it)->parentWidget();
			if (dock) {
				obs_frontend_remove_dock(dock->objectName().toUtf8().constData());
			}
		}

		for (auto i = canvas_docks.size(); i > 0; i--) {
			auto it = canvas_docks.begin();
			std::advance(it, i - 1);
			auto dock = (*it)->parentWidget();
			if (dock) {
				obs_frontend_remove_dock(dock->objectName().toUtf8().constData());
			}
		}
	} else if (event == OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED) {
		if (transitions_dock) {
			QMetaObject::invokeMethod(transitions_dock, "TransitionDurationChanged", Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_TRANSITION_CHANGED) {
		if (transitions_dock) {
			QMetaObject::invokeMethod(transitions_dock, "TransitionChanged", Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED) {
		if (transitions_dock) {
			QMetaObject::invokeMethod(transitions_dock, "TransitionListChanged", Qt::QueuedConnection);
		}
	}
}

class TabToolBar : public QToolBar {
private:
	QTabBar *tabs;

	void checkOrientation() const;

public:
	TabToolBar(QTabBar *tabs_);
	//QSize minimumSizeHint() const override;

protected:
	virtual void resizeEvent(QResizeEvent *event) override;
};

QWidget *aitumSettingsWidget = nullptr;

bool obs_data_array_equal(obs_data_array_t *a, obs_data_array_t *b)
{
	size_t a_count = obs_data_array_count(a);
	size_t b_count = obs_data_array_count(b);
	if (a_count != b_count) {
		return false;
	}
	for (size_t i = 0; i < a_count; i++) {
		obs_data_t *a_item = obs_data_array_item(a, i);
		obs_data_t *b_item = obs_data_array_item(b, i);
		const char *a_json = obs_data_get_json(a_item);
		const char *b_json = obs_data_get_json(b_item);
		bool equal = (strcmp(a_json, b_json) == 0);
		obs_data_release(a_item);
		obs_data_release(b_item);
		if (!equal) {
			return false;
		}
	}
	return true;
}

void open_config_dialog(int tab, const char *create_type)
{
	if (!configDialog) {
		configDialog = new OBSBasicSettings((QMainWindow *)obs_frontend_get_main_window());
		QObject::connect(configDialog, &OBSBasicSettings::accepted, [] {

		});
	}
	auto settings = obs_data_create();
	if (current_profile_config) {
		const char *geom = obs_data_get_string(current_profile_config, "config_geometry");
		if (geom && strlen(geom)) {
			QByteArray ba = QByteArray::fromBase64(QByteArray(geom));
			configDialog->restoreGeometry(ba);
		}
		obs_data_apply(settings, current_profile_config);
	}

	configDialog->LoadSettings(settings);
	if (tab > 0) {
		configDialog->ShowTab(tab);
	}
	configDialog->show();
	configDialog->SetCreateType(create_type);

	if (configDialog->exec() == QDialog::Accepted) {
		bool canvas_changed = false;
		bool outputs_changed = false;
		bool check_new_canvas = false;
		if (current_profile_config) {
			auto show = obs_data_get_bool(settings, "main_stream_output_show");
			if (show != obs_data_get_bool(current_profile_config, "main_stream_output_show")) {
				obs_data_set_bool(current_profile_config, "main_stream_output_show", show);
				outputs_changed = true;
			}
			show = obs_data_get_bool(settings, "main_record_output_show");
			if (show != obs_data_get_bool(current_profile_config, "main_record_output_show")) {
				obs_data_set_bool(current_profile_config, "main_record_output_show", show);
				outputs_changed = true;
			}
			show = obs_data_get_bool(settings, "main_backtrack_output_show");
			if (show != obs_data_get_bool(current_profile_config, "main_backtrack_output_show")) {
				obs_data_set_bool(current_profile_config, "main_backtrack_output_show", show);
				outputs_changed = true;
			}
			show = obs_data_get_bool(settings, "main_virtual_cam_output_show");
			if (show != obs_data_get_bool(current_profile_config, "main_virtual_cam_output_show")) {
				obs_data_set_bool(current_profile_config, "main_virtual_cam_output_show", show);
				outputs_changed = true;
			}
			obs_data_array_t *a = obs_data_get_array(current_profile_config, "canvas");
			obs_data_array_t *b = obs_data_get_array(settings, "canvas");
			if (!obs_data_array_equal(a, b)) {
				canvas_changed = true;
				obs_data_set_array(current_profile_config, "canvas", b);
			}
			obs_data_array_release(a);
			obs_data_array_release(b);
			a = obs_data_get_array(current_profile_config, "outputs");
			b = obs_data_get_array(settings, "outputs");
			if (!obs_data_array_equal(a, b)) {
				outputs_changed = true;
				check_new_canvas = true;
				obs_data_set_array(current_profile_config, "outputs", b);
			}
			obs_data_array_release(a);
			obs_data_array_release(b);
			obs_data_release(settings);
		} else {
			canvas_changed = true;
			outputs_changed = true;
			current_profile_config = settings;
		}
		obs_data_set_string(current_profile_config, "config_geometry", configDialog->saveGeometry().toBase64().constData());
		configDialog->SaveHotkeys();

		save_current_profile_config(true);
		if (canvas_changed) {
			load_canvas(check_new_canvas);
			// Aitum++: editing canvases can create a newly visible OBS dock, so immediately match the UI mode the user chose.
			if (aitum_pp_controls_active && !aitum_pp_workspaces_active) {
				aitum_pp_show_minimal_controls();
			} else {
				aitum_pp_hide_additive_docks();
			}
			if (vendor) {
				obs_websocket_vendor_emit_event(vendor, "canvas_changed", nullptr);
			}
		}

		if (outputs_changed) {
			load_outputs();
			if (vendor) {
				obs_websocket_vendor_emit_event(vendor, "outputs_changed", nullptr);
			}
		}
	} else {
		obs_data_release(settings);
	}
}
extern "C" const struct obs_source_info component_info;

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Aitum Stream Suite] loaded version %s (Aitum++ fork)", PROJECT_VERSION);

	obs_register_source(&component_info);

	QFontDatabase::addApplicationFont(":/aitum/media/Roboto.ttf");
	QFontDatabase::addApplicationFont(":/aitum/media/Roboto-Italic.ttf");
	QFontDatabase::addApplicationFont(":/aitum/media/RobotoCondensed.ttf");
	QFontDatabase::addApplicationFont(":/aitum/media/RobotoCondensed-Italic.ttf");

	obs_frontend_add_event_callback(frontend_event, nullptr);

	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto user_config = obs_frontend_get_user_config();
	if (user_config) {
		// Aitum++: no first-run appearance changes. The user's theme,
		// mixer orientation and context toolbars stay untouched. Only
		// apply the margins the Aitum theme expects if the user has
		// picked that theme themselves.
		const char *theme = config_get_string(user_config, "Appearance", "Theme");
		if (theme && strcmp(theme, "com.obsproject.Aitum.Original") == 0) {
			main_window->setContentsMargins(10, 10, 10, 10);
		}
	}

	modesTabBar = new QTabBar();
	modesTabBar->setContextMenuPolicy(Qt::CustomContextMenu);
	modesTabBar->setMovable(true);

	toolbar = new TabToolBar(modesTabBar);
	toolbar->setObjectName(QStringLiteral("AitumToolbar"));
	main_window->addToolBar(toolbar);
	toolbar->setFloatable(false);
	// Aitum++: keep the toolbar hidden until OBS finishes loading so the untouched normal layout can be captured before Advanced Workspaces becomes the default.
	toolbar->hide();
	//tb->setMovable(false);
	//tb->setAllowedAreas(Qt::ToolBarArea::TopToolBarArea);

	for (auto it : fixed_tabs) {
		auto index = modesTabBar->addTab(QString::fromUtf8(obs_module_text(std::get<0>(it).c_str())));
		modesTabBar->setTabData(index, QString::fromUtf8(std::get<0>(it).c_str()));
		modesTabBar->setTabIcon(index, generateEmojiQIcon(std::get<2>(it), modesTabBar->palette().color(QPalette::Text)));
		modesTabBar->setTabVisible(index, !std::get<3>(it));
	}
	toolbar->addWidget(modesTabBar);
	auto addModeAction =
		toolbar->addAction(QIcon(":/res/images/plus.svg"), QString::fromUtf8(obs_module_text("AddDockMode")), [] {
			const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			if (!main_window) {
				return;
			}
			std::string name = obs_module_text("DockMode");
			if (NameDialog::AskForName(main_window, QString::fromUtf8(obs_module_text("DockMode")), name)) {
				if (name.empty()) {
					return;
				}
				for (int i = 0; i < modesTabBar->count(); i++) {
					auto d = modesTabBar->tabData(i);
					if (!d.isNull() && d.isValid() && d.toString().toStdString() == name) {
						return;
					} else if (modesTabBar->tabText(i).toStdString() == name) {
						return;
					}
				}
				auto index = modesTabBar->addTab(QString::fromStdString(name));
				modesTabBar->setCurrentIndex(index);
				save_current_profile_config(true);
			}
		});
	toolbar->widgetForAction(addModeAction)->setProperty("themeID", QVariant(QString::fromUtf8("addIconSmall")));
	toolbar->widgetForAction(addModeAction)->setProperty("class", "icon-plus");
	toolbar->addSeparator();

	QObject::connect(modesTabBar, &QTabBar::currentChanged, [](int index) {
		if (!current_profile_config || !obs_data_get_bool(current_profile_config, "dock_mode_manual_save")) {
			save_dock_state(modesTab);
		}
		auto d = modesTabBar->tabData(index);
		if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
			modesTab = d.toString();
			load_dock_state(d.toString());
			if (vendor) {
				auto d2 = obs_data_create();
				obs_data_set_string(d2, "name", d.toString().toUtf8().constData());
				obs_data_set_bool(d2, "fixed", true);
				obs_websocket_vendor_emit_event(vendor, "switched_dock_mode", d2);
				obs_data_release(d2);
			}
		} else {
			modesTab = modesTabBar->tabText(index);
			load_dock_state(modesTabBar->tabText(index));
			if (vendor) {
				auto d2 = obs_data_create();
				obs_data_set_string(d2, "name", modesTabBar->tabText(index).toUtf8().constData());
				obs_data_set_bool(d2, "fixed", false);
				obs_websocket_vendor_emit_event(vendor, "switched_dock_mode", d2);
				obs_data_release(d2);
			}
		}
	});

	QObject::connect(modesTabBar, &QTabBar::customContextMenuRequested, [] {
		int tab = modesTabBar->tabAt(QCursor::pos() - modesTabBar->mapToGlobal(QPoint(0, 0)));
		QMenu menu;
		auto index = modesTabBar->currentIndex();
		if (tab == index || tab == -1) {
			auto d = modesTabBar->tabData(index);
			if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
				menu.addAction(QString::fromUtf8(obs_module_text("Reset")), [d] {
					for (auto it : fixed_tabs) {
						if (std::get<0>(it) == d.toString().toUtf8().constData()) {
							std::get<1>(it)();
							return;
						}
					}
				});
			} else {
				auto tabName = modesTabBar->tabText(index);
				bool found = tabName == "Main";
				for (auto it : canvas_docks) {
					if (it->parentWidget()->objectName() == tabName) {
						found = true;
						break;
					}
				}
				for (auto it : canvas_clone_docks) {
					if (it->parentWidget()->objectName() == tabName) {
						found = true;
						break;
					}
				}
				if (found) {
					menu.addAction(QString::fromUtf8(obs_module_text("Reset")), [] {
						auto index = modesTabBar->currentIndex();
						if (index < 0) {
							return;
						}
						reset_canvas_dock_state(modesTabBar->tabText(index));
					});
				}
				menu.addAction(QString::fromUtf8(obs_module_text("Remove")), [] {
					auto index = modesTabBar->currentIndex();
					if (index < 0) {
						return;
					}
					modesTabBar->removeTab(index);
					save_current_profile_config(true);
				});
			}
		}
		auto a = menu.addAction(QString::fromUtf8(obs_module_text("DockModeAutoSave")), [] {
			if (!current_profile_config) {
				return;
			}
			obs_data_set_bool(current_profile_config, "dock_mode_manual_save",
					  !obs_data_get_bool(current_profile_config, "dock_mode_manual_save"));
		});
		a->setCheckable(true);
		a->setChecked(current_profile_config ? !obs_data_get_bool(current_profile_config, "dock_mode_manual_save") : true);
		if (tab == index || tab == -1) {
			menu.addAction(QString::fromUtf8(obs_module_text("DockModeSave")), [] {
				auto index = modesTabBar->currentIndex();
				if (index < 0) {
					return;
				}
				QString tn;
				auto d = modesTabBar->tabData(index);
				if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
					tn = d.toString();
				} else {
					tn = modesTabBar->tabText(index);
				}
				save_dock_state(tn);
				save_current_profile_config(true);
			});
		}
		menu.addSeparator();
		menu.addAction(QString::fromUtf8(obs_module_text("AddEmptyDock")), [] {
			const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			if (!main_window) {
				return;
			}
			std::string name = obs_module_text("EmptyDock");
			if (NameDialog::AskForName(main_window, QString::fromUtf8(obs_module_text("EmptyDockName")), name)) {
				//break;
				auto empty_dock = new QFrame;
				std::string title = "⬜ ";
				title += name;
				if (obs_frontend_add_dock_by_id(name.c_str(), title.c_str(), empty_dock)) {
					empty_dock->parentWidget()->show();
					auto ed = obs_data_get_array(current_profile_config, "empty_docks");
					if (!ed) {
						ed = obs_data_array_create();
						obs_data_set_array(current_profile_config, "empty_docks", ed);
					}
					auto edd = obs_data_create();
					obs_data_set_string(edd, "name", name.c_str());
					obs_data_array_push_back(ed, edd);
					obs_data_release(edd);
					empty_docks.push_back(empty_dock);
				} else {
					delete empty_dock;
				}
			}
		});
		if (!empty_docks.empty()) {
			auto removeMenu = menu.addMenu(QString::fromUtf8(obs_module_text("RemoveEmptyDock")));
			for (const auto &it : empty_docks) {
				QFrame *w = it;
				removeMenu->addAction(it->parentWidget()->objectName(), [w] {
					std::string name = w->parentWidget()->objectName().toUtf8().constData();
					obs_frontend_remove_dock(name.c_str());
					empty_docks.remove(w);
					auto ed = obs_data_get_array(current_profile_config, "empty_docks");
					auto count = obs_data_array_count(ed);
					for (size_t i = count; i > 0; i--) {
						auto item = obs_data_array_item(ed, i - 1);
						if (!item) {
							continue;
						}
						if (strcmp(name.c_str(), obs_data_get_string(item, "name")) == 0) {
							obs_data_array_erase(ed, i - 1);
						}
					}
				});
			}
		}
		if (tab >= 0) {
			menu.exec(QCursor::pos());
		} else {
			menu.exec(modesTabBar->mapToGlobal(modesTabBar->tabRect(index).center()));
		}
	});

	auto aitumSettingsAction = toolbar->addAction(QString::fromUtf8(obs_module_text("Settings")));
	aitumSettingsAction->setProperty("themeID", "configIconSmall");
	aitumSettingsAction->setProperty("class", "icon-gear");
	aitumSettingsWidget = toolbar->widgetForAction(aitumSettingsAction);
	aitumSettingsWidget->setProperty("themeID", "configIconSmall");
	aitumSettingsWidget->setProperty("class", "icon-gear");
	aitumSettingsWidget->setObjectName("AitumStreamSuiteSettingsButton");
	QObject::connect(aitumSettingsAction, &QAction::triggered, [] { open_config_dialog(0, nullptr); });

	// Contribute Button
	auto contributeButton = toolbar->addAction(generateEmojiQIcon("❤️", toolbar->palette().color(QPalette::Text)),
						   QString::fromUtf8(obs_module_text("Donate")));
	contributeButton->setProperty("themeID", "icon-aitum-donate");
	contributeButton->setProperty("class", "icon-aitum-donate");
	toolbar->widgetForAction(contributeButton)->setProperty("themeID", "icon-aitum-donate");
	toolbar->widgetForAction(contributeButton)->setProperty("class", "icon-aitum-donate");
	contributeButton->setToolTip(QString::fromUtf8(obs_module_text("AitumStreamSuiteDonate")));
	QAction::connect(contributeButton, &QAction::triggered,
			 [] { QDesktopServices::openUrl(QUrl("https://aitum.tv/contribute")); });

	// Aitum Button
	auto aitumButton = toolbar->addAction(QIcon(":/aitum/media/aitum.png"), QString::fromUtf8(obs_module_text("Aitum")));
	aitumButton->setProperty("themeID", "icon-aitum");
	aitumButton->setProperty("class", "icon-aitum");
	toolbar->widgetForAction(aitumButton)->setProperty("themeID", "icon-aitum");
	toolbar->widgetForAction(aitumButton)->setProperty("class", "icon-aitum");
	aitumButton->setToolTip(QString::fromUtf8("https://aitum.tv"));
	QAction::connect(aitumButton, &QAction::triggered, [] { QDesktopServices::openUrl(QUrl("https://aitum.tv")); });

	auto addCanvas = toolbar->addAction(QString::fromUtf8(obs_module_text("AddCanvas")));
	QAction::connect(addCanvas, &QAction::triggered, [] { open_config_dialog(1, nullptr); });

	//tb->addAction(QString::fromUtf8(obs_module_text("Reset")));
	//tb->layout()->addItem(new QSpacerItem(0, 0));
	QWidget *spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	toolbar->addWidget(spacer);

	//auto controlsToolBar = main_window->addToolBar(QString::fromUtf8(obs_module_text("Controls")));
	auto controlsToolBar = toolbar;

#ifdef __APPLE__ // Three identical icon-only controls hide which recovery will run, so use compact two-emoji labels and keep the full accessible names; do not reintroduce identical icons. (Codex task: 01a01b14-9ef1-7082-99e7-1885d5d90235)
	auto restartScreenCaptureAction = controlsToolBar->addAction(QStringLiteral("🖥️🔄"));
	restartScreenCaptureAction->setToolTip(QString::fromUtf8(obs_module_text("RestartScreenCaptureTooltip")));
	auto *restartScreenCaptureButton = (QToolButton *)controlsToolBar->widgetForAction(restartScreenCaptureAction);
	restartScreenCaptureButton->setAccessibleName(QString::fromUtf8(obs_module_text("RestartScreenCapture")));
	restartScreenCaptureButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	QObject::connect(restartScreenCaptureAction, &QAction::triggered, restart_macos_screen_captures);

	auto restartCameraServicesAction = controlsToolBar->addAction(QStringLiteral("📷🔄"));
	restartCameraServicesAction->setToolTip(QString::fromUtf8(obs_module_text("RestartCameraServicesTooltip")));
	auto *restartCameraServicesButton = (QToolButton *)controlsToolBar->widgetForAction(restartCameraServicesAction);
	restartCameraServicesButton->setAccessibleName(QString::fromUtf8(obs_module_text("RestartCameraServices")));
	restartCameraServicesButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	QObject::connect(restartCameraServicesAction, &QAction::triggered, restart_macos_camera_services);

	auto restartOBSAction = controlsToolBar->addAction(QStringLiteral("🎬🔄"));
	restartOBSAction->setToolTip(QString::fromUtf8(obs_module_text("RestartOBSTooltip")));
	auto *restartOBSButton = (QToolButton *)controlsToolBar->widgetForAction(restartOBSAction);
	restartOBSButton->setAccessibleName(QString::fromUtf8(obs_module_text("RestartOBS")));
	restartOBSButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	QObject::connect(restartOBSAction, &QAction::triggered, restart_obs_via_obscene);
#endif

	studioModeAction = controlsToolBar->addAction(QString::fromUtf8(obs_module_text("StudioMode")));

	studioModeAction->setCheckable(true);
	studioModeAction->setIcon(create2StateIcon(":/aitum/media/studio_mode_on.svg", ":/aitum/media/studio_mode_off.svg"));
	controlsToolBar->widgetForAction(studioModeAction)->setStyleSheet("QAbstractButton:checked{background: rgb(158,0,89);}");
	((QToolButton *)controlsToolBar->widgetForAction(studioModeAction))->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	QObject::connect(studioModeAction, SIGNAL(triggered()), main_window, SLOT(TogglePreviewProgramMode()));

	//https://coolors.co/00d299-ff0000-1a57ff-c08000-9e0059

	auto settingsButton = controlsToolBar->addAction(QString::fromUtf8(obs_module_text("Settings")));
	settingsButton->setProperty("themeID", "configIconSmall");
	settingsButton->setProperty("class", "icon-gear");
	controlsToolBar->widgetForAction(settingsButton)->setProperty("themeID", "configIconSmall");
	controlsToolBar->widgetForAction(settingsButton)->setProperty("class", "icon-gear");
	((QToolButton *)controlsToolBar->widgetForAction(settingsButton))->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	QObject::connect(settingsButton, SIGNAL(triggered()), main_window, SLOT(on_action_Settings_triggered()));

	//auto action = controlsToolBar->addAction("");
	//action->setCheckable(true);
	//action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::InsertLink));
	//action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::CameraWeb));
	//action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditUndo));

	// Aitum++: turning the main OBS preview into a dock replaces the normal
	// OBS working layout, so it is opt-in (Aitum/MainCanvasDock=true in the
	// user config) instead of the default.
	auto cw = main_window->centralWidget();
	if (user_config && config_get_bool(user_config, "Aitum", "MainCanvasDock") && cw &&
	    cw->objectName() == "centralwidget" && cw->findChild<QWidget *>("canvasEditor") != nullptr) {
		obs_frontend_add_dock_by_id("AitumStreamSuiteMainCanvas", obs_module_text("AitumStreamSuiteMainCanvas"), cw);
		cw = new QWidget();
		cw->setContentsMargins(0, 0, 0, 0);
		cw->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		main_window->setCentralWidget(cw);
	}

	output_dock = new OutputDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteOutput", obs_module_text("AitumStreamSuiteOutput"), output_dock);
	//component_dock = new CanvasDock("Components", main_window);
	//obs_frontend_add_dock_by_id("AitumStreamSuiteComponent", obs_module_text("AitumStreamSuiteComponent"), component_dock);
	properties_dock = new PropertiesDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteProperties", obs_module_text("AitumStreamSuiteProperties"), properties_dock);
	filters_dock = new FiltersDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteFilters", obs_module_text("AitumStreamSuiteFilters"), filters_dock);
	transform_dock = new TransformDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteTransform", obs_module_text("AitumStreamSuiteTransform"), transform_dock);
	live_scenes_dock = new LiveScenesDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteLiveScenes", obs_module_text("AitumStreamSuiteLiveScenes"), live_scenes_dock);
	auto capture_dock = new CaptureDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteCapture", obs_module_text("AitumStreamSuiteCapture"), capture_dock);
	stats_dock = new StatsDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteStats", obs_module_text("AitumStreamSuiteStats"), stats_dock);
	scenes_dock = new ScenesDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteScenes", obs_module_text("AitumStreamSuiteScenes"), scenes_dock);
	sources_dock = new SourcesDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteSources", obs_module_text("AitumStreamSuiteSources"), sources_dock);
	transitions_dock = new TransitionsDock(main_window);
	obs_frontend_add_dock_by_id("AitumStreamSuiteTransitions", obs_module_text("AitumStreamSuiteTransitions"),
				    transitions_dock);

	// Aitum++: compact entry points in the conventional Tools menu.
	// "Aitum++ Controls" toggles the minimum useful controls (extra
	// canvases + outputs dock), "Aitum++ Workspaces" is the advanced
	// workspace toolbar, and "Aitum++ Settings" opens the config dialog.
	aitumPPControlsAction =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("AitumPlusPlusControls")));
	if (aitumPPControlsAction) {
		aitumPPControlsAction->setCheckable(true);
		QObject::connect(aitumPPControlsAction, &QAction::toggled,
				 [](bool checked) { aitum_pp_set_controls_active(checked); });
	}
	aitumPPWorkspacesAction =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("AitumPlusPlusWorkspaces")));
	if (aitumPPWorkspacesAction) {
		aitumPPWorkspacesAction->setCheckable(true);
		QObject::connect(aitumPPWorkspacesAction, &QAction::toggled,
				 [](bool checked) { aitum_pp_set_workspaces_active(checked); });
	}
	auto aitumPPSettingsAction =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("AitumPlusPlusSettings")));
	if (aitumPPSettingsAction) {
		QObject::connect(aitumPPSettingsAction, &QAction::triggered, [] { open_config_dialog(0, nullptr); });
	}

	std::string url = "https://api.aitum.tv/plugin/streamsuite";
	const char *pguid = config_get_string(obs_frontend_get_app_config(), "General", "InstallGUID");
	if (pguid) {
		url += "?uuid=";
		url += pguid;
	}

	// Keep partner blocks, extensions, and overlays, but do not offer upstream Aitum binaries for this fork.
	plugin_metadata_download_info = download_info_create_single(
		"[Aitum Stream Suite]", "OBS", url.c_str(), plugin_metadata_downloaded, nullptr);
	return true;
}

void TabToolBar::checkOrientation() const
{
	const auto main_window = static_cast<QMainWindow *>(parent());
	auto area = main_window->toolBarArea(this);
	if (orientation() == Qt::Orientation::Vertical) {
		if (area == Qt::RightToolBarArea) {
			if (tabs->shape() != QTabBar::Shape::RoundedEast) {
				tabs->setShape(QTabBar::Shape::RoundedEast);
			}
		} else {
			if (tabs->shape() != QTabBar::Shape::RoundedWest) {
				tabs->setShape(QTabBar::Shape::RoundedWest);
			}
		}
	} else {
		if (area == Qt::BottomToolBarArea) {
			if (tabs->shape() != QTabBar::Shape::RoundedSouth) {
				tabs->setShape(QTabBar::Shape::RoundedSouth);
			}
		} else {
			if (tabs->shape() != QTabBar::Shape::RoundedNorth) {
				tabs->setShape(QTabBar::Shape::RoundedNorth);
			}
		}
	}
}

void TabToolBar::resizeEvent(QResizeEvent *event)
{
	load_dock_state_timer.stop();
	if (!isFloating()) {
		auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (!main_window) {
			return;
		}
		auto docks = main_window->findChildren<QDockWidget *>();
		QList<QString> current_docks;
		for (auto &dock : docks) {
			if (dock->isVisible()) {
				current_docks.append(dock->objectName());
			}
		}
		if (current_docks == loaded_docks) {
			load_dock_state_timer.start();
		} else if (main_window->isVisible() && current_profile_config &&
			   !obs_data_get_bool(current_profile_config, "dock_mode_manual_save")) {
			auto index = modesTabBar->currentIndex();
			if (index >= 0) {
				auto d = modesTabBar->tabData(index);
				if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
					save_dock_state(d.toString());
				} else {
					save_dock_state(modesTabBar->tabText(index));
				}
			}
			loaded_docks = current_docks;
		}
	}
	checkOrientation();
	QToolBar::resizeEvent(event);
}

TabToolBar::TabToolBar(QTabBar *tabs_) : QToolBar(), tabs(tabs_)
{
	connect(this, &QToolBar::orientationChanged, [&] { checkOrientation(); });
}

/*
QSize TabToolBar::minimumSizeHint() const{
	checkOrientation();
	auto size = QToolBar::minimumSizeHint();
	if (orientation() == Qt::Orientation::Vertical) {
		auto t = tabs->minimumSizeHint();
		int i = 0;
	}
	//auto size2 = QToolBar::minimumSizeHint();
	return size;
}*/

static void save_load(obs_data_t *save_data, bool saving, void *private_data)
{
	UNUSED_PARAMETER(save_data);
	UNUSED_PARAMETER(private_data);
	if (saving) {
		save_current_profile_config(!scene_collection_changing);
	}
}

void load_obs_websocket();

void obs_module_post_load()
{
	load_dock_state_timer.setInterval(100);
	load_dock_state_timer.setSingleShot(true);
	QObject::connect(&load_dock_state_timer, &QTimer::timeout, []() {
		auto index = modesTabBar->currentIndex();
		if (index >= 0) {
			auto d = modesTabBar->tabData(index);
			if (!d.isNull() && d.isValid() && !d.toString().isEmpty()) {
				load_dock_state(d.toString());
			} else {
				load_dock_state(modesTabBar->tabText(index));
			}
		}
	});

	obs_frontend_add_save_callback(save_load, nullptr);

	load_obs_websocket();
}

void unload_obs_websocket();

void obs_module_unload()
{
	aitumPPControlsAction = nullptr;
	aitumPPWorkspacesAction = nullptr;
	unload_obs_websocket();
	obs_frontend_remove_save_callback(save_load, nullptr);
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	if (plugin_metadata_download_info) {
		download_info_destroy(plugin_metadata_download_info);
		plugin_metadata_download_info = nullptr;
	}
	if (current_profile_config) {
		obs_data_release(current_profile_config);
		current_profile_config = nullptr;
	}

	obs_frontend_remove_dock("AitumStreamSuiteOutput");
	obs_frontend_remove_dock("AitumStreamSuiteProperties");
	obs_frontend_remove_dock("AitumStreamSuiteFilters");
	obs_frontend_remove_dock("AitumStreamSuiteTransform");
	obs_frontend_remove_dock("AitumStreamSuiteLiveScenes");
	obs_frontend_remove_dock("AitumStreamSuiteCapture");
	obs_frontend_remove_dock("AitumStreamSuiteStats");
	obs_frontend_remove_dock("AitumStreamSuiteScenes");
	obs_frontend_remove_dock("AitumStreamSuiteSources");
	obs_frontend_remove_dock("AitumStreamSuiteTransitions");

	obs_frontend_remove_dock("AitumStreamSuiteChat");
	obs_frontend_remove_dock("AitumStreamSuiteActivity");
	obs_frontend_remove_dock("AitumStreamSuiteInfo");
	obs_frontend_remove_dock("AitumStreamSuitePortal");
	obs_frontend_remove_dock("AitumStreamSuiteSelect");
	obs_frontend_remove_dock("AitumStreamSuiteOverlays");

	if (output_dock) {
		delete output_dock;
	}
	for (auto &it : empty_docks) {
		obs_frontend_remove_dock(it->parentWidget()->objectName().toUtf8().constData());
	}
	empty_docks.clear();

	DestroyPanelCookieManager();
	if (cef) {
		delete cef;
		cef = nullptr;
	}
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("AitumStreamSuite");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("AitumStreamSuite");
}

extern "C" void show_component_editor(const char *name);

void show_component_editor(const char *name)
{
	if (!component_dock) {
		return;
	}
	QMetaObject::invokeMethod(component_dock, "SwitchScene", Q_ARG(QString, QString::fromUtf8(name)));
	QMetaObject::invokeMethod(component_dock, [] {
		auto window = QApplication::activeWindow();
		if (window->objectName() == "OBSBasicProperties") {
			window->close();
		}
		component_dock->parentWidget()->show();
		component_dock->parentWidget()->raise();
		component_dock->parentWidget()->setFocus();
	});
}
