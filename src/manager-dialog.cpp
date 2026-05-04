#include <QDialog>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QFont>
#include <QFrame>
#include <QMessageBox>
#include <QApplication>
#include <QThread>
#include <QTimer>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <obs-frontend-api.h>

extern "C" {
#include "auth.h"
#include "downloader.h"
#include "oauth.h"
}

#include "manager-dialog.hpp"

static bool is_de(const char *locale)
{
	return locale && locale[0] == 'd' && locale[1] == 'e';
}

class ManagerDialog : public QDialog {
public:
	ManagerDialog(QWidget *parent, const char *locale)
		: QDialog(parent), m_locale(locale ? locale : "en")
	{
		bool de = is_de(locale);
		setWindowTitle("stools Plugin Manager");
		setMinimumSize(680, 450);
		resize(700, 480);

		auto *mainLayout = new QVBoxLayout(this);
		mainLayout->setContentsMargins(16, 16, 16, 16);
		mainLayout->setSpacing(12);

		/* ---- Header ---- */
		auto *header = new QLabel("stools Plugin Manager");
		QFont headerFont = header->font();
		headerFont.setPointSize(14);
		headerFont.setBold(true);
		header->setFont(headerFont);
		mainLayout->addWidget(header);

		/* ---- Auth section ---- */
		m_authFrame = new QFrame();
		auto *authLayout = new QHBoxLayout(m_authFrame);
		authLayout->setContentsMargins(0, 0, 0, 0);

		m_statusLabel = new QLabel();
		authLayout->addWidget(m_statusLabel, 1);

		m_loginBtn = new QPushButton(
			de ? "Mit stools.cc anmelden" : "Login with stools.cc");
		connect(m_loginBtn, &QPushButton::clicked, this,
			&ManagerDialog::onLogin);
		authLayout->addWidget(m_loginBtn);

		m_logoutBtn = new QPushButton(de ? "Abmelden" : "Logout");
		connect(m_logoutBtn, &QPushButton::clicked, this,
			&ManagerDialog::onLogout);
		authLayout->addWidget(m_logoutBtn);

		mainLayout->addWidget(m_authFrame);

		/* ---- Separator ---- */
		auto *sep = new QFrame();
		sep->setFrameShape(QFrame::HLine);
		mainLayout->addWidget(sep);

		/* ---- Plugin table ---- */
		m_table = new QTableWidget(0, 5);
		m_table->setHorizontalHeaderLabels(
			{de ? "Plugin" : "Plugin",
			 de ? "Installiert" : "Installed",
			 de ? "Verfügbar" : "Available", "", ""});
		m_table->horizontalHeader()->setStretchLastSection(false);
		m_table->horizontalHeader()->setSectionResizeMode(
			0, QHeaderView::Stretch);
		m_table->horizontalHeader()->setSectionResizeMode(
			1, QHeaderView::Fixed);
		m_table->horizontalHeader()->setSectionResizeMode(
			2, QHeaderView::Fixed);
		m_table->horizontalHeader()->setSectionResizeMode(
			3, QHeaderView::Fixed);
		m_table->horizontalHeader()->setSectionResizeMode(
			4, QHeaderView::Fixed);
		m_table->setColumnWidth(1, 120);
		m_table->setColumnWidth(2, 90);
		m_table->setColumnWidth(3, 110);
		m_table->setColumnWidth(4, 110);
		m_table->verticalHeader()->setVisible(false);
		m_table->setSelectionMode(QAbstractItemView::NoSelection);
		m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		m_table->setShowGrid(false);
		m_table->setAlternatingRowColors(true);
		mainLayout->addWidget(m_table, 1);

		/* ---- Refresh button ---- */
		auto *bottomLayout = new QHBoxLayout();
		m_refreshBtn = new QPushButton(
			de ? "Aktualisieren" : "Refresh");
		connect(m_refreshBtn, &QPushButton::clicked, this,
			&ManagerDialog::onRefresh);
		bottomLayout->addStretch();
		bottomLayout->addWidget(m_refreshBtn);
		mainLayout->addLayout(bottomLayout);

		/* ---- Info label ---- */
		m_infoLabel = new QLabel(
			de ? "Änderungen werden nach OBS-Neustart wirksam."
			   : "Changes take effect after restarting OBS.");
		QFont infoFont = m_infoLabel->font();
		infoFont.setItalic(true);
		m_infoLabel->setFont(infoFont);
		m_infoLabel->setStyleSheet("color: gray;");
		mainLayout->addWidget(m_infoLabel);

		updateAuthUI();

		if (auth_is_logged_in())
			onRefresh();
	}

private:
	QString m_locale;
	QFrame *m_authFrame;
	QLabel *m_statusLabel;
	QPushButton *m_loginBtn;
	QPushButton *m_logoutBtn;
	QTableWidget *m_table;
	QPushButton *m_refreshBtn;
	QLabel *m_infoLabel;
	struct plugin_list m_plugins = {};

	void updateAuthUI()
	{
		bool de = is_de(m_locale.toUtf8().constData());
		bool logged_in = auth_is_logged_in();

		m_loginBtn->setVisible(!logged_in);
		m_logoutBtn->setVisible(logged_in);
		m_refreshBtn->setEnabled(logged_in);
		m_table->setEnabled(logged_in);

		if (logged_in) {
			m_statusLabel->setText(
				QString(de ? "Angemeldet als: %1"
					   : "Logged in as: %1")
					.arg(auth_get_username()));
			m_statusLabel->setStyleSheet("color: green;");
		} else {
			m_statusLabel->setText(
				de ? "Nicht angemeldet"
				   : "Not logged in");
			m_statusLabel->setStyleSheet("color: red;");
		}
	}

	void onLogin()
	{
		bool de = is_de(m_locale.toUtf8().constData());

		m_loginBtn->setEnabled(false);
		m_loginBtn->setText(de ? "Browser öffnet..." : "Opening browser...");
		QApplication::processEvents();

		char token[512] = "";
		bool got_token = oauth_start_flow(token, sizeof(token));

		if (got_token && token[0]) {
			bool ok = auth_login(token);
			if (ok) {
				updateAuthUI();
				onRefresh();
			} else {
				QMessageBox::warning(
					this, "stools Plugin Manager",
					de ? "Token-Validierung fehlgeschlagen."
					   : "Token validation failed.");
			}
		} else {
			QMessageBox::warning(
				this, "stools Plugin Manager",
				de ? "Login abgebrochen oder fehlgeschlagen."
				   : "Login cancelled or failed.");
		}

		m_loginBtn->setEnabled(true);
		m_loginBtn->setText(
			de ? "Mit Twitch anmelden" : "Login with Twitch");
	}

	void onLogout()
	{
		auth_logout();
		m_table->setRowCount(0);
		m_plugins.count = 0;
		updateAuthUI();
	}

	void onRefresh()
	{
		if (!auth_is_logged_in()) return;

		m_refreshBtn->setEnabled(false);
		m_refreshBtn->setText("...");
		QApplication::processEvents();

		bool de = is_de(m_locale.toUtf8().constData());

		if (downloader_fetch_plugin_list(auth_get_token(), &m_plugins)) {
			char obs_dir[512];
			if (downloader_get_obs_plugin_dir(obs_dir, sizeof(obs_dir)))
				downloader_detect_installed(&m_plugins, obs_dir);
			populateTable();
		} else {
			QMessageBox::warning(
				this, "stools Plugin Manager",
				de ? "Plugin-Liste konnte nicht geladen werden."
				   : "Failed to fetch plugin list.");
		}

		m_refreshBtn->setEnabled(true);
		m_refreshBtn->setText(de ? "Aktualisieren" : "Refresh");
	}

	void populateTable()
	{
		bool de = is_de(m_locale.toUtf8().constData());
		m_table->setRowCount(m_plugins.count);

		for (int i = 0; i < m_plugins.count; i++) {
			struct plugin_info *pi = &m_plugins.items[i];

			m_table->setItem(i, 0,
					 new QTableWidgetItem(pi->name));

			QString installed_ver =
				pi->installed
					? QString(pi->installed_version)
					: (de ? "Nicht installiert"
					       : "Not installed");
			m_table->setItem(i, 1,
					 new QTableWidgetItem(installed_ver));

			m_table->setItem(
				i, 2,
				new QTableWidgetItem(pi->latest_version));

			QString btn_text;
			if (!pi->installed)
				btn_text = de ? "Installieren" : "Install";
			else if (pi->update_available)
				btn_text = de ? "Aktualisieren" : "Update";
			else
				btn_text = de ? "Aktuell" : "Up to date";

			auto *btn = new QPushButton(btn_text);
			btn->setEnabled(!pi->installed ||
					pi->update_available);
			btn->setProperty("slug",
					 QString(pi->slug));
			btn->setProperty("version",
					 QString(pi->latest_version));
			connect(btn, &QPushButton::clicked, this,
				&ManagerDialog::onInstallClicked);
			m_table->setCellWidget(i, 3, btn);

			if (pi->installed) {
				auto *delBtn = new QPushButton(
					de ? "Deinstallieren" : "Uninstall");
				delBtn->setProperty("slug", QString(pi->slug));
				delBtn->setStyleSheet(
					"QPushButton { color: #cc3333; }");
				connect(delBtn, &QPushButton::clicked, this,
					&ManagerDialog::onUninstallClicked);
				m_table->setCellWidget(i, 4, delBtn);
			}
		}
	}

	void onInstallClicked()
	{
		auto *btn = qobject_cast<QPushButton *>(sender());
		if (!btn) return;

		QString slug = btn->property("slug").toString();
		QString version = btn->property("version").toString();
		bool de = is_de(m_locale.toUtf8().constData());

		btn->setEnabled(false);
		btn->setText("...");
		QApplication::processEvents();

		char obs_dir[512];
		if (!downloader_get_obs_plugin_dir(obs_dir, sizeof(obs_dir))) {
			QMessageBox::critical(
				this, "stools Plugin Manager",
				de ? "OBS Plugin-Verzeichnis nicht gefunden."
				   : "OBS plugin directory not found.");
			btn->setEnabled(true);
			return;
		}

		int asset_id = 0;
		for (int i = 0; i < m_plugins.count; i++) {
			if (slug == m_plugins.items[i].slug) {
				asset_id = m_plugins.items[i].download_asset_id;
				break;
			}
		}

		bool ok = downloader_install_plugin(
			auth_get_token(), slug.toUtf8().constData(),
			asset_id, version.toUtf8().constData(), obs_dir);

		if (ok) {

			btn->setText(de ? "Aktuell" : "Up to date");
			btn->setEnabled(false);
			onRefresh();

			auto answer = QMessageBox::question(
				this, "stools Plugin Manager",
				de ? "Installation erfolgreich!\nOBS jetzt neu starten?"
				   : "Installation successful!\nRestart OBS now?",
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::Yes);

			if (answer == QMessageBox::Yes) {
				char exe[512];
#ifdef _WIN32
				DWORD pid = GetCurrentProcessId();
				GetModuleFileNameA(NULL, exe, sizeof(exe));
				char obs_bin[512];
				snprintf(obs_bin, sizeof(obs_bin), "%s", exe);
				char *ls = strrchr(obs_bin, '\\');
				if (ls) *ls = '\0';
				char tmp[512];
				GetTempPathA(sizeof(tmp), tmp);
				char ps_path[512];
				snprintf(ps_path, sizeof(ps_path), "%sst_pm_restart.ps1", tmp);
				FILE *bf = fopen(ps_path, "w");
				if (bf) {
					fprintf(bf, "do { Start-Sleep -Seconds 1 } while (Get-Process -Id %lu -ErrorAction SilentlyContinue)\n",
						(unsigned long)pid);
					fprintf(bf, "Start-Sleep -Seconds 2\n");
					fprintf(bf, "Start-Process -FilePath '%s' -WorkingDirectory '%s'\n", exe, obs_bin);
					fprintf(bf, "Remove-Item -LiteralPath '%s' -Force\n", ps_path);
					fclose(bf);
					char ps_args[2048];
					snprintf(ps_args, sizeof(ps_args),
						 "-NoProfile -WindowStyle Hidden "
						 "-ExecutionPolicy Bypass -File \"%s\"",
						 ps_path);
					ShellExecuteA(NULL, "open", "powershell.exe",
						      ps_args, NULL, SW_HIDE);
				}
#else
				{
					pid_t pid = getpid();
					char tmp_dir[256] = "/tmp";
					char script[512];
					snprintf(script, sizeof(script),
						 "%s/st_pm_restart.sh", tmp_dir);
					FILE *sf = fopen(script, "w");
					if (sf) {
						fprintf(sf, "#!/bin/sh\n");
						fprintf(sf, "while kill -0 %d 2>/dev/null; do sleep 1; done\n", pid);
						fprintf(sf, "sleep 2\n");
#ifdef __APPLE__
						fprintf(sf, "open -a OBS\n");
#else
						char self[512] = "";
						ssize_t l = readlink("/proc/self/exe", self, sizeof(self)-1);
						if (l > 0) { self[l] = '\0'; fprintf(sf, "nohup '%s' >/dev/null 2>&1 &\n", self); }
#endif
						fprintf(sf, "rm -f '%s'\n", script);
						fclose(sf);
						chmod(script, 0755);
						if (fork() == 0) {
							setsid();
							execl("/bin/sh", "sh", script, (char *)NULL);
							_exit(1);
						}
					}
				}
#endif
				/* Gracefully close OBS via main window */
				QMainWindow *main = (QMainWindow *)obs_frontend_get_main_window();
				if (main)
					main->close();
			}
		} else {
			const char *err = downloader_last_error();
			QString detail = err && err[0]
				? QString::fromUtf8(err)
				: (de ? "Unbekannter Fehler" : "Unknown error");

			QMessageBox::critical(
				this, "stools Plugin Manager",
				QString(de ? "Installation von %1 fehlgeschlagen:\n%2"
					   : "Installation of %1 failed:\n%2")
					.arg(slug, detail));

			btn->setText(de ? "Erneut versuchen" : "Retry");
			btn->setEnabled(true);
		}
	}

	void onUninstallClicked()
	{
		auto *btn = qobject_cast<QPushButton *>(sender());
		if (!btn) return;

		QString slug = btn->property("slug").toString();
		bool de = is_de(m_locale.toUtf8().constData());

		auto answer = QMessageBox::question(
			this, "stools Plugin Manager",
			QString(de ? "%1 deinstallieren?\nOBS wird dafür neu gestartet."
				   : "Uninstall %1?\nOBS will restart for this.").arg(slug),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);

		if (answer != QMessageBox::Yes) return;

		char obs_dir[512];
		if (!downloader_get_obs_plugin_dir(obs_dir, sizeof(obs_dir))) {
			QMessageBox::critical(
				this, "stools Plugin Manager",
				de ? "OBS Plugin-Verzeichnis nicht gefunden."
				   : "OBS plugin directory not found.");
			return;
		}

		bool ok = downloader_uninstall_plugin(
			slug.toUtf8().constData(), obs_dir);

		if (ok) {
			/* Script is running in background, waiting for OBS to exit.
			   Close OBS gracefully - script will delete files then restart. */
			QMainWindow *main = (QMainWindow *)obs_frontend_get_main_window();
			if (main)
				main->close();
		} else {
			const char *err = downloader_last_error();
			QString detail = err && err[0]
				? QString::fromUtf8(err)
				: (de ? "Unbekannter Fehler" : "Unknown error");
			QMessageBox::critical(
				this, "stools Plugin Manager",
				QString(de ? "Deinstallation von %1 fehlgeschlagen:\n%2"
					   : "Uninstall of %1 failed:\n%2")
					.arg(slug, detail));
		}
	}
};

void manager_dialog_show(const char *locale)
{
	QMainWindow *main_window =
		(QMainWindow *)obs_frontend_get_main_window();
	auto *dialog = new ManagerDialog(main_window, locale);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->show();
}
