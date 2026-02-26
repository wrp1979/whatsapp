#include "webenginepage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QMetaEnum>
#include <QStandardPaths>
#include <QTextStream>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

namespace {
QString certErrorTypeToString(QWebEngineCertificateError::Type type) {
  const QMetaEnum metaEnum =
      QMetaEnum::fromType<QWebEngineCertificateError::Type>();
  const char *key = metaEnum.valueToKey(type);
  if (key) {
    return QString::fromLatin1(key);
  }
  return QString::number(static_cast<int>(type));
}

QString certLogPath() {
  const QString dirPath =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (dirPath.isEmpty()) {
    return QString();
  }
  QDir dir(dirPath);
  if (!dir.exists()) {
    dir.mkpath(".");
  }
  return dir.filePath("cert_errors.log");
}

QString summarizeCertChain(const QList<QSslCertificate> &chain) {
  QStringList parts;
  parts.reserve(chain.size());
  for (const QSslCertificate &cert : chain) {
    const QStringList cnList =
        cert.subjectInfo(QSslCertificate::CommonName);
    const QString cn = cnList.isEmpty() ? QString("unknown") : cnList.first();
    parts.append(cn);
  }
  return parts.join(" -> ");
}

void logCertificateError(const QWebEngineCertificateError &error,
                         const QString &action) {
  const QString url = error.url().toString();
  const QString host = error.url().host();
  const QString type = certErrorTypeToString(error.type());
  const QString desc = error.description();
  const QString chainSummary = summarizeCertChain(error.certificateChain());
  const QString message = QString("[%1] cert_error action=%2 url=%3 host=%4 "
                                  "type=%5 overridable=%6 desc=%7 chain=%8")
                              .arg(QDateTime::currentDateTime().toString(
                                   Qt::ISODate))
                              .arg(action, url, host, type)
                              .arg(error.isOverridable())
                              .arg(desc, chainSummary);

  qWarning().noquote() << message;

  const QString path = certLogPath();
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    return;
  }
  QTextStream out(&file);
  out << message << "\n";
}
} // namespace

QWebEngineView *WebEnginePage::view() const {
    return qobject_cast<QWebEngineView *>(parent());
}

WebEnginePage::WebEnginePage(QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent) {

  // Set up audio transcription via QWebChannel
  m_transcriber = new AudioTranscriber(this);
  m_bridge = new TranscriberBridge(m_transcriber, this);
  m_channel = new QWebChannel(this);
  m_channel->registerObject(QStringLiteral("transcriber"), m_bridge);
  this->setWebChannel(m_channel);
  setupWebChannel(profile);

  auto userAgent = profile->httpUserAgent();
  qDebug() << "WebEnginePage::Profile::UserAgent" << userAgent;
  auto webengineversion =
      userAgent.split("QtWebEngine").last().split(" ").first();
  auto toRemove = "QtWebEngine" + webengineversion;
  auto cleanUserAgent = userAgent.remove(toRemove).replace("  ", " ");
  profile->setHttpUserAgent(cleanUserAgent);

  connect(this, &QWebEnginePage::loadFinished, this,
          &WebEnginePage::handleLoadFinished);
  connect(this, &QWebEnginePage::authenticationRequired, this,
          &WebEnginePage::handleAuthenticationRequired);
  connect(this, &QWebEnginePage::featurePermissionRequested, this,
          &WebEnginePage::handleFeaturePermissionRequested);
  connect(this, &QWebEnginePage::proxyAuthenticationRequired, this,
          &WebEnginePage::handleProxyAuthenticationRequired);
  connect(this, &QWebEnginePage::registerProtocolHandlerRequested, this,
          &WebEnginePage::handleRegisterProtocolHandlerRequested);

#if !defined(QT_NO_SSL) || QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
  connect(this, &QWebEnginePage::selectClientCertificate, this,
          &WebEnginePage::handleSelectClientCertificate);
#endif

  connect(this, &QWebEnginePage::certificateError, this,
          [this](QWebEngineCertificateError error) {
            if (error.isOverridable()) {
              logCertificateError(error, "auto-accept");
              error.acceptCertificate();
            } else {
              logCertificateError(error, "reject-not-overridable");
              error.rejectCertificate();
            }
          });
}

bool WebEnginePage::acceptNavigationRequest(const QUrl &url,
                                            QWebEnginePage::NavigationType type,
                                            bool isMainFrame) {
  if (QWebEnginePage::NavigationType::NavigationTypeLinkClicked == type) {
    QDesktopServices::openUrl(url);
    return false;
  }

  return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

QWebEnginePage *
WebEnginePage::createWindow(QWebEnginePage::WebWindowType type) {
  Q_UNUSED(type);
  return new WebEnginePage(this->profile());
}

inline QString questionForFeature(QWebEnginePage::Feature feature) {
  switch (feature) {
  case QWebEnginePage::Geolocation:
    return WebEnginePage::tr("Allow %1 to access your location information?");
  case QWebEnginePage::MediaAudioCapture:
    return WebEnginePage::tr("Allow %1 to access your microphone?");
  case QWebEnginePage::MediaVideoCapture:
    return WebEnginePage::tr("Allow %1 to access your webcam?");
  case QWebEnginePage::MediaAudioVideoCapture:
    return WebEnginePage::tr("Allow %1 to access your microphone and webcam?");
  case QWebEnginePage::MouseLock:
    return WebEnginePage::tr("Allow %1 to lock your mouse cursor?");
  case QWebEnginePage::DesktopVideoCapture:
    return WebEnginePage::tr("Allow %1 to capture video of your desktop?");
  case QWebEnginePage::DesktopAudioVideoCapture:
    return WebEnginePage::tr(
        "Allow %1 to capture audio and video of your desktop?");
  case QWebEnginePage::Notifications:
    return WebEnginePage::tr("Allow %1 to show notification on your desktop?");
  }
  return QString();
}

void WebEnginePage::handleFeaturePermissionRequested(const QUrl &securityOrigin,
                                                     Feature feature) {
  bool autoPlay = true;
  if (SettingsManager::instance().settings().value("autoPlayMedia").isValid())
    autoPlay = SettingsManager::instance()
                   .settings()
                   .value("autoPlayMedia", false)
                   .toBool();
  if (autoPlay && (feature == QWebEnginePage::MediaVideoCapture ||
                   feature == QWebEnginePage::MediaAudioVideoCapture)) {
    QWebEngineProfile *defProfile = QWebEngineProfile::defaultProfile();
    auto *webSettings = defProfile->settings();
    webSettings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture,
                              false);

    profile()->settings()->setAttribute(
        QWebEngineSettings::PlaybackRequiresUserGesture, false);
  }

  QString title = tr("Permission Request");
  QString question = questionForFeature(feature).arg(securityOrigin.host());

  QString featureStr = QVariant::fromValue(feature).toString();
  SettingsManager::instance().settings().beginGroup("permissions");
  if (SettingsManager::instance()
          .settings()
          .value(featureStr, false)
          .toBool()) {
    setFeaturePermission(
        securityOrigin, feature,
        QWebEnginePage::PermissionPolicy::PermissionGrantedByUser);
  } else {
    if (!question.isEmpty() &&
        QMessageBox::question(view()->window(), title, question) ==
            QMessageBox::Yes) {
      setFeaturePermission(
          securityOrigin, feature,
          QWebEnginePage::PermissionPolicy::PermissionGrantedByUser);
      SettingsManager::instance().settings().setValue(featureStr, true);
    } else {
      setFeaturePermission(
          securityOrigin, feature,
          QWebEnginePage::PermissionPolicy::PermissionDeniedByUser);
      SettingsManager::instance().settings().setValue(featureStr, false);
    }
  }
  SettingsManager::instance().settings().endGroup();
}

void WebEnginePage::handleLoadFinished(bool ok) {

  // turn on Notification settings by default
  if (SettingsManager::instance()
          .settings()
          .value("permissions/Notifications")
          .isValid() == false) {
    SettingsManager::instance().settings().beginGroup("permissions");
    SettingsManager::instance().settings().setValue("Notifications", true);
    setFeaturePermission(
        QUrl("https://web.whatsapp.com/"),
        QWebEnginePage::Feature::Notifications,
        QWebEnginePage::PermissionPolicy::PermissionGrantedByUser);
    SettingsManager::instance().settings().endGroup();
  } else if (SettingsManager::instance()
                 .settings()
                 .value("permissions/Notifications", true)
                 .toBool()) {
    setFeaturePermission(
        QUrl("https://web.whatsapp.com/"),
        QWebEnginePage::Feature::Notifications,
        QWebEnginePage::PermissionPolicy::PermissionGrantedByUser);
  }

  if (ok) {
    injectPreventScrollWheelZoomHelper();
    injectFullWidthJavaScript();
    injectClassChangeObserver();
    injectNewChatJavaScript();
    injectVisibilityOverride();
    injectInputFocusKeeper();
    injectAudioTranscriber();
  }
}

void WebEnginePage::fullScreenRequestedByPage(
    QWebEngineFullScreenRequest request) {
  request.accept();
}

QStringList WebEnginePage::chooseFiles(QWebEnginePage::FileSelectionMode mode,
                                       const QStringList &oldFiles,
                                       const QStringList &acceptedMimeTypes) {
  qDebug() << mode << oldFiles << acceptedMimeTypes;
  QFileDialog::FileMode dialogMode;
  if (mode == QWebEnginePage::FileSelectOpen) {
    dialogMode = QFileDialog::ExistingFile;
  } else {
    dialogMode = QFileDialog::ExistingFiles;
  }

  QFileDialog *dialog = new QFileDialog();
  bool usenativeFileDialog = SettingsManager::instance()
                                 .settings()
                                 .value("useNativeFileDialog", false)
                                 .toBool();

  if (usenativeFileDialog == false) {
    dialog->setOption(QFileDialog::DontUseNativeDialog, true);
  }
  dialog->setFileMode(dialogMode);
  QStringList mimeFilters;
  mimeFilters.append("application/octet-stream"); // to show All files(*)
  mimeFilters.append(acceptedMimeTypes);

  if (acceptedMimeTypes.contains("image/*")) {
    foreach (QByteArray mime, QImageReader::supportedImageFormats()) {
      mimeFilters.append("image/" + mime);
    }
  }

  mimeFilters.sort(Qt::CaseSensitive);
  dialog->setMimeTypeFilters(mimeFilters);

  QStringList selectedFiles;
  if (dialog->exec()) {
    selectedFiles = dialog->selectedFiles();
  }
  dialog->deleteLater();
  return selectedFiles;
}

void WebEnginePage::handleAuthenticationRequired(const QUrl &requestUrl,
                                                 QAuthenticator *auth) {
  QWidget *mainWindow = view()->window();
  QDialog dialog(mainWindow);
  dialog.setModal(true);
  dialog.setWindowFlags(dialog.windowFlags() &
                        ~Qt::WindowContextHelpButtonHint);

  Ui::PasswordDialog passwordDialog;
  passwordDialog.setupUi(&dialog);

  passwordDialog.m_iconLabel->setText(QString());
  QIcon icon(mainWindow->style()->standardIcon(QStyle::SP_MessageBoxQuestion,
                                               nullptr, mainWindow));
  passwordDialog.m_iconLabel->setPixmap(icon.pixmap(32, 32));

  QString introMessage(
      tr("Enter username and password for \"%1\" at %2")
          .arg(auth->realm(), requestUrl.toString().toHtmlEscaped()));
  passwordDialog.m_infoLabel->setText(introMessage);
  passwordDialog.m_infoLabel->setWordWrap(true);

  if (dialog.exec() == QDialog::Accepted) {
    auth->setUser(passwordDialog.m_userNameLineEdit->text());
    auth->setPassword(passwordDialog.m_passwordLineEdit->text());
  } else {
    // Set authenticator null if dialog is cancelled
    *auth = QAuthenticator();
  }
}

void WebEnginePage::handleProxyAuthenticationRequired(
    const QUrl &, QAuthenticator *auth, const QString &proxyHost) {
  QWidget *mainWindow = view()->window();
  QDialog dialog(mainWindow);
  dialog.setModal(true);
  dialog.setWindowFlags(dialog.windowFlags() &
                        ~Qt::WindowContextHelpButtonHint);

  Ui::PasswordDialog passwordDialog;
  passwordDialog.setupUi(&dialog);

  passwordDialog.m_iconLabel->setText(QString());
  QIcon icon(mainWindow->style()->standardIcon(QStyle::SP_MessageBoxQuestion,
                                               nullptr, mainWindow));
  passwordDialog.m_iconLabel->setPixmap(icon.pixmap(32, 32));

  QString introMessage = tr("Connect to proxy \"%1\" using:");
  introMessage = introMessage.arg(proxyHost.toHtmlEscaped());
  passwordDialog.m_infoLabel->setText(introMessage);
  passwordDialog.m_infoLabel->setWordWrap(true);

  if (dialog.exec() == QDialog::Accepted) {
    auth->setUser(passwordDialog.m_userNameLineEdit->text());
    auth->setPassword(passwordDialog.m_passwordLineEdit->text());
  } else {
    // Set authenticator null if dialog is cancelled
    *auth = QAuthenticator();
  }
}

//! [registerProtocolHandlerRequested]
void WebEnginePage::handleRegisterProtocolHandlerRequested(
    QWebEngineRegisterProtocolHandlerRequest request) {
  auto answer = QMessageBox::question(
      view()->window(), tr("Permission Request"),
      tr("Allow %1 to open all %2 links?")
          .arg(request.origin().host(), request.scheme()));
  if (answer == QMessageBox::Yes)
    request.accept();
  else
    request.reject();
}
//! [registerProtocolHandlerRequested]

#if !defined(QT_NO_SSL) || QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
void WebEnginePage::handleSelectClientCertificate(
    QWebEngineClientCertificateSelection selection) {
  // Just select one.
  selection.select(selection.certificates().at(0));

  qDebug() << __FUNCTION__;
  auto certificates = selection.certificates();
  for (const QSslCertificate &cert : qAsConst(certificates)) {
    qDebug() << cert;
    selection.select(cert); // select the first available cert
    break;
  }
  qDebug() << selection.host();
}
#endif

void WebEnginePage::javaScriptConsoleMessage(
    WebEnginePage::JavaScriptConsoleMessageLevel level, const QString &message,
    int lineId, const QString &sourceId) {
  Q_UNUSED(lineId);
  Q_UNUSED(sourceId);
  if (message.startsWith("[Whatsie]"))
    qInfo().noquote() << message;
}

void WebEnginePage::injectPreventScrollWheelZoomHelper() {
  QString js = R"(
                    (function () {
                        const SSWZ = function () {
                            this.keyScrollHandler = function (e) {
                                if (e.ctrlKey) {
                                    e.preventDefault();
                                    return false;
                                }
                            }
                        };
                        if (window === top) {
                            const sswz = new SSWZ();
                            window.addEventListener('wheel', sswz.keyScrollHandler, {
                                passive: false
                            });
                        }
                    })();
                )";
  this->runJavaScript(js);
}

void WebEnginePage::injectClassChangeObserver() {
  QString js =
      R"(
        var cc_observer = new MutationObserver(() => {
            var haveFullView = document.body.classList.contains('whatsie-full-view');
            var container = document.querySelector('#app > .app-wrapper-web > .two');
            if(container){
                if(haveFullView){
                    container.style.width = '100%';
                    container.style.height = '100%';
                    container.style.top = '0';
                    container.style.maxWidth = 'unset';
                }else{
                    container.style.width = null;
                    container.style.height = null;
                    container.style.top = null;
                    container.style.maxWidth = null;
                }
                cc_observer.disconnect();
            }
        });
        cc_observer.observe(document.body, {
            attributes: true,
            attributeFilter: ['class'],
            childList: false,
            characterData: false
        });
        )";
    this->runJavaScript(js);
}

void WebEnginePage::injectFullWidthJavaScript() {
    if (!SettingsManager::instance().settings().value("fullWidthView", true).toBool())
        return;

    QString js =
        R"(function updateFullWidthView(element) {
                var container = document.querySelector('#app > .app-wrapper-web > .two');
                container.style.width = '100%';
                container.style.height = '100%';
                container.style.top = '0';
                container.style.maxWidth = 'unset';
                fw_observer.disconnect();
            }
            var fw_observer = new MutationObserver(mutations => {
                const element = document.querySelector('#pane-side');
                if (element) {
                    updateFullWidthView({ selector: '#pane-side', element });
                }
            });
            fw_observer.observe(document.documentElement, {
                childList: true,
                subtree: true
            });
           )";
    this->runJavaScript(js);
}

void WebEnginePage::injectNewChatJavaScript() {
  QString js = R"(const openNewChatWhatsie = (phone,text) => {
                    const link = document.createElement('a');
                    link.setAttribute('href',
                    `whatsapp://send/?phone=${phone}&text=${text}`);
                    document.body.append(link);
                    link.click();
                    document.body.removeChild(link);
                };
                function openNewChatWhatsieDefined()
                {
                    return (openNewChatWhatsie != 'undefined');
                })";
  this->runJavaScript(js);
}

void WebEnginePage::injectVisibilityOverride() {
  QString js = R"(
    (function() {
      if (window._whatsieVisibilityOverride) return;
      window._whatsieVisibilityOverride = true;

      function isInactive() {
        return window._whatsieWindowActive === false;
      }

      const docProto = Document.prototype;
      const hiddenDesc = Object.getOwnPropertyDescriptor(docProto, 'hidden');
      const visibilityDesc = Object.getOwnPropertyDescriptor(docProto, 'visibilityState');
      const origHidden =
        hiddenDesc && hiddenDesc.get ? hiddenDesc.get.bind(document) : () => false;
      const origVisibility =
        visibilityDesc && visibilityDesc.get ? visibilityDesc.get.bind(document) : () => 'visible';

      function getHidden() {
        if (isInactive()) return true;
        return origHidden();
      }

      function getVisibility() {
        if (isInactive()) return 'hidden';
        return origVisibility();
      }

      try {
        Object.defineProperty(document, 'hidden', {
          configurable: true,
          get: getHidden
        });
      } catch (e) {}

      try {
        Object.defineProperty(document, 'visibilityState', {
          configurable: true,
          get: getVisibility
        });
      } catch (e) {}

      const origHasFocus = document.hasFocus ? document.hasFocus.bind(document) : null;
      document.hasFocus = function() {
        if (isInactive()) return false;
        return origHasFocus ? origHasFocus() : true;
      };

      window._whatsieUpdateVisibility = function() {
        document.dispatchEvent(new Event('visibilitychange'));
        if (isInactive()) {
          window.dispatchEvent(new Event('blur'));
        } else {
          window.dispatchEvent(new Event('focus'));
        }
      };
    })();
  )";
  this->runJavaScript(js);
}

void WebEnginePage::injectInputFocusKeeper() {
  QString js = R"(
    (function() {
      if (window._whatsieFocusKeeperV4) return;
      window._whatsieFocusKeeperV4 = true;

      let enabled = true;
      let lastModalState = false;
      let wheelPauseUntil = 0;
      let mouseDownInMain = false;
      let mouseDownInFooter = false;
      let modifierHeld = false;
      let pausedByModifier = false;
      let manualPause = false;
      let lastCtrlTapAt = 0;
      let ctrlTapInterrupted = false;
      const CTRL_DOUBLE_TAP_MS = 350;
      let toastTimer = null;
      let lastToast = null;
      let hintTimer = null;
      const HINT_KEY = 'whatsieFocusHintSeen_v1';

      // Hidden input for WebKit focus workaround
      const hiddenInput = document.createElement('input');
      hiddenInput.style.cssText = 'position:fixed;top:-9999px;left:-9999px;opacity:0;pointer-events:none;';
      hiddenInput.setAttribute('tabindex', '-1');
      hiddenInput.setAttribute('aria-hidden', 'true');
      document.body.appendChild(hiddenInput);

      // All possible selectors for the message input field
      const INPUT_SELECTORS = [
        '#main footer div[contenteditable="true"][data-tab="10"]',
        '#main div[contenteditable="true"][data-tab="10"]',
        'div[contenteditable="true"][data-tab="10"]',
        '[data-testid="conversation-compose-box-input"]',
        '#main footer div[contenteditable="true"]'
      ];

      function getInput() {
        for (const sel of INPUT_SELECTORS) {
          const el = document.querySelector(sel);
          if (el && el.offsetParent !== null) return el;
        }
        return null;
      }

      // Check if there's any overlay/modal open (image paste, file send, etc)
      function hasAnyOverlay() {
        // Check for any span overlay in #app (WhatsApp uses spans for modals)
        const spans = document.querySelectorAll('#app > div > span');
        for (const span of spans) {
          // If span has visible content with a send button or close button, it's a modal
          if (span.querySelector('[data-testid="send"]')) return true;
          if (span.querySelector('[data-testid="x-viewer"]')) return true;
          if (span.querySelector('[data-icon="x-viewer"]')) return true;
          if (span.querySelector('[data-icon="x"]')) return true;
          // Check for any contenteditable inside span overlays
          const ce = span.querySelector('[contenteditable="true"]');
          if (ce) {
            // Make sure it's not inside #main (the main chat area)
            const main = document.querySelector('#main');
            if (!main || !main.contains(ce)) return true;
          }
        }
        return false;
      }

      // Only block focus stealing when modals/overlays are open
      function hasBlockingModal() {
        // Generic overlay detection first (catches image paste, file send, etc)
        if (hasAnyOverlay()) return true;
        // Specific modals
        if (document.querySelector('[data-testid="media-editor"]')) return true;
        if (document.querySelector('[data-testid="image-editor"]')) return true;
        if (document.querySelector('[data-testid="media-editor-modal"]')) return true;
        if (document.querySelector('[data-testid="media-picker-modal"]')) return true;
        if (document.querySelector('[data-testid="forward-message-modal"]')) return true;
        if (document.querySelector('[data-testid="popup-contents"]')) return true;
        if (document.querySelector('[role="dialog"][aria-modal="true"]')) return true;
        if (document.querySelector('[data-testid="media-canvas-container"]')) return true;
        return false;
      }

      // Check if focused on search or other valid input
      function isOnOtherInput() {
        const active = document.activeElement;
        if (!active) return false;
        if (active === hiddenInput) return false;

        // Sidebar inputs (search, etc)
        const side = document.querySelector('#side');
        if (side && side.contains(active)) {
          if (active.getAttribute('contenteditable') === 'true' ||
              active.tagName === 'INPUT') {
            return true;
          }
        }

        // Media caption input
        if (active.getAttribute('data-testid') === 'media-caption-input') return true;
        if (active.closest('[data-testid="media-editor"]')) return true;
        if (active.closest('[data-testid="image-editor"]')) return true;
        if (active.closest('[data-testid="popup-contents"]')) return true;

        // Any contenteditable NOT in #main footer (caption fields, etc)
        if (active.getAttribute('contenteditable') === 'true') {
          const mainFooter = document.querySelector('#main footer');
          if (!mainFooter || !mainFooter.contains(active)) {
            return true;
          }
        }

        // Any input/textarea outside main message input
        if (active.tagName === 'INPUT' || active.tagName === 'TEXTAREA') {
          return true;
        }

        return false;
      }

      // WebKit workaround: focus hidden input first, then target
      function webkitFocusFix(target) {
        try {
          hiddenInput.focus();
          hiddenInput.setSelectionRange(0, 0);
        } catch(e) {}
        target.focus({ preventScroll: true, focusVisible: true });
      }

      function pauseFocusFor(ms) {
        const until = Date.now() + ms;
        if (until > wheelPauseUntil) wheelPauseUntil = until;
      }

      function showFocusToast(message, type) {
        const id = 'whatsie-focus-toast';
        let el = document.getElementById(id);
        if (!el) {
          el = document.createElement('div');
          el.id = id;
          el.style.cssText =
            'position:fixed;left:50%;bottom:18px;transform:translateX(-50%);' +
            'padding:8px 12px;border-radius:6px;font:12px/1.4 Arial, sans-serif;' +
            'background:rgba(20,20,20,0.92);color:#fff;z-index:999999;' +
            'box-shadow:0 4px 12px rgba(0,0,0,0.35);pointer-events:none;' +
            'transition:opacity 120ms ease;opacity:0;';
          document.body.appendChild(el);
        }
        if (lastToast === message) return;
        lastToast = message;
        el.textContent = message;
        if (type === 'pause') {
          el.style.background = 'rgba(132, 76, 0, 0.95)';
        } else if (type === 'resume') {
          el.style.background = 'rgba(0, 102, 68, 0.95)';
        } else {
          el.style.background = 'rgba(20,20,20,0.92)';
        }
        el.style.opacity = '1';
        if (toastTimer) clearTimeout(toastTimer);
        toastTimer = setTimeout(() => {
          el.style.opacity = '0';
        }, 1200);
      }

      function showFocusHint() {
        const id = 'whatsie-focus-hint';
        let el = document.getElementById(id);
        if (!el) {
          el = document.createElement('div');
          el.id = id;
          el.style.cssText =
            'position:fixed;left:50%;top:12px;transform:translateX(-50%) translateY(-6px);' +
            'padding:6px 10px;border-radius:999px;font:12px/1.4 Arial, sans-serif;' +
            'background:rgba(0,0,0,0.82);color:#fff;z-index:999999;' +
            'box-shadow:0 4px 12px rgba(0,0,0,0.35);pointer-events:none;' +
            'transition:opacity 160ms ease, transform 160ms ease;opacity:0;';
          document.body.appendChild(el);
        }
        el.textContent = 'Tip: press Ctrl x2 or hold Alt to pause focus for selection.';
        el.style.opacity = '1';
        el.style.transform = 'translateX(-50%) translateY(0)';
        if (hintTimer) clearTimeout(hintTimer);
        hintTimer = setTimeout(() => {
          el.style.opacity = '0';
          el.style.transform = 'translateX(-50%) translateY(-6px)';
        }, 1800);
      }

      function maybeShowFocusHint() {
        let alreadyShown = false;
        try {
          alreadyShown = sessionStorage.getItem(HINT_KEY) === '1';
          if (!alreadyShown) sessionStorage.setItem(HINT_KEY, '1');
        } catch (e) {}
        if (alreadyShown) return;
        setTimeout(() => {
          if (!document.body) return;
          showFocusHint();
        }, 1200);
      }

      function setModifierHeld(next) {
        if (modifierHeld === next) return;
        modifierHeld = next;
        if (modifierHeld) {
          pausedByModifier = true;
          if (!manualPause) {
            showFocusToast('Focus paused (hold Alt)', 'pause');
          }
        }
        if (!modifierHeld) {
          setTimeout(brutalFocus, 0);
          setTimeout(brutalFocus, 50);
        }
      }

      function setManualPause(next) {
        if (manualPause === next) return;
        manualPause = next;
        if (manualPause) {
          showFocusToast('Focus paused (Ctrl x2)', 'pause');
          return;
        }
        if (!modifierHeld) {
          pausedByModifier = false;
          showFocusToast('Focus restored', 'resume');
          setTimeout(brutalFocus, 0);
          setTimeout(brutalFocus, 50);
        }
      }

      maybeShowFocusHint();

      function eventHasModifier(e) {
        if (!e) return modifierHeld;
        if (e.getModifierState) {
          return e.getModifierState('Alt');
        }
        return !!e.altKey;
      }

      function isWindowActive() {
        if (typeof window._whatsieWindowActive === 'boolean') {
          return window._whatsieWindowActive;
        }
        return true;
      }

      function hasActiveMainSelection() {
        const sel = window.getSelection ? window.getSelection() : null;
        if (!sel || sel.rangeCount === 0 || sel.isCollapsed) return false;

        const main = document.querySelector('#main');
        if (!main) return false;

        const footer = document.querySelector('#main footer');
        const anchor = sel.anchorNode;
        const focus = sel.focusNode;
        const inMain = (anchor && main.contains(anchor)) ||
                       (focus && main.contains(focus));
        if (!inMain) return false;

        const inFooter = footer && ((anchor && footer.contains(anchor)) ||
                                    (focus && footer.contains(focus)));
        return !inFooter;
      }

      // BRUTAL force focus with WebKit workaround
      function brutalFocus() {
        if (!isWindowActive()) return;
        if (document.hidden) return;
        if (typeof document.hasFocus === 'function' && !document.hasFocus()) return;
        if (!enabled) return;
        if (hasBlockingModal()) return;
        if (isOnOtherInput()) return;
        if (Date.now() < wheelPauseUntil) return;
        if (mouseDownInMain && !mouseDownInFooter) return;
        if (manualPause) return;
        if (modifierHeld) return;
        if (!pausedByModifier && hasActiveMainSelection()) return;

        const input = getInput();
        if (!input) return;

        const active = document.activeElement;

        // Already on target
        if (active === input) return;
        if (input.contains(active)) return;

        // Use WebKit workaround
        webkitFocusFix(input);
        if (pausedByModifier) {
          pausedByModifier = false;
          showFocusToast('Focus restored', 'resume');
        }
      }

      // Aggressive loop - 50ms interval (balanced between responsiveness and not blocking paste)
      let intervalId = setInterval(brutalFocus, 50);

      // RAF loop for smooth focus
      let rafEnabled = true;
      function rafLoop() {
        if (!rafEnabled) return;
        brutalFocus();
        requestAnimationFrame(rafLoop);
      }
      requestAnimationFrame(rafLoop);

      // Detect modal close and IMMEDIATELY focus with burst
      const observer = new MutationObserver(() => {
        const currentModalState = hasBlockingModal();
        if (lastModalState && !currentModalState) {
          // Modal just closed - FORCE FOCUS NOW with burst
          brutalFocus();
          setTimeout(brutalFocus, 0);
          setTimeout(brutalFocus, 16);
          setTimeout(brutalFocus, 32);
          setTimeout(brutalFocus, 50);
          setTimeout(brutalFocus, 100);
          setTimeout(brutalFocus, 150);
          setTimeout(brutalFocus, 200);
          setTimeout(brutalFocus, 300);
        }
        lastModalState = currentModalState;

        // Also try on any DOM change
        if (!currentModalState) {
          brutalFocus();
        }
      });

      observer.observe(document.body, {
        childList: true,
        subtree: true,
        attributes: true,
        attributeFilter: ['class', 'style', 'aria-hidden', 'data-testid']
      });

      // Prevent mousedown from stealing focus in main area
      document.addEventListener('mousedown', (e) => {
        const main = document.querySelector('#main');
        const footer = document.querySelector('#main footer');
        mouseDownInMain = !!(main && main.contains(e.target));
        mouseDownInFooter = !!(footer && footer.contains(e.target));

        if (hasBlockingModal()) return;

        const input = getInput();
        if (!input) return;

        if (!main || !main.contains(e.target)) return;

        // If clicking on non-interactive element in chat area, prevent focus steal
        const target = e.target;
        const isInteractive = target.closest('button, a, [role="button"], input, textarea, [contenteditable="true"], video, audio, [tabindex]');

        if (!isInteractive && !input.contains(target)) {
          // Don't prevent default (breaks selection), but schedule immediate refocus
          setTimeout(brutalFocus, 0);
          setTimeout(brutalFocus, 10);
        }
      }, true);

      // Pause aggressive focus while the user is scrolling messages with the wheel
      document.addEventListener('wheel', (e) => {
        if (hasBlockingModal()) return;
        const main = document.querySelector('#main');
        if (!main || !main.contains(e.target)) return;

        const footer = document.querySelector('#main footer');
        if (footer && footer.contains(e.target)) return;

        pauseFocusFor(800);
      }, { passive: true, capture: true });

      // On any click in main area, force focus after
      document.addEventListener('click', (e) => {
        if (hasBlockingModal()) return;
        const main = document.querySelector('#main');
        if (main && main.contains(e.target)) {
          setTimeout(brutalFocus, 0);
          setTimeout(brutalFocus, 10);
          setTimeout(brutalFocus, 50);
        }
      }, true);

      // After any mouseup
      document.addEventListener('mouseup', (e) => {
        mouseDownInMain = false;
        mouseDownInFooter = false;
        setModifierHeld(eventHasModifier(e));
        if (hasBlockingModal()) return;
        setTimeout(brutalFocus, 0);
        setTimeout(brutalFocus, 50);
      }, true);

      window.addEventListener('blur', () => {
        mouseDownInMain = false;
        mouseDownInFooter = false;
        modifierHeld = false;
        pausedByModifier = false;
        lastCtrlTapAt = 0;
        ctrlTapInterrupted = false;
      });

      // On window/visibility focus
      window.addEventListener('focus', () => {
        setTimeout(brutalFocus, 0);
        setTimeout(brutalFocus, 50);
        setTimeout(brutalFocus, 100);
      });

      document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') {
          setTimeout(brutalFocus, 0);
          setTimeout(brutalFocus, 100);
        }
      });

      // Intercept all focusout from input
      document.addEventListener('focusout', (e) => {
        const input = getInput();
        if (input && (e.target === input || input.contains(e.target))) {
          if (!hasBlockingModal() && !isOnOtherInput()) {
            setTimeout(brutalFocus, 0);
            setTimeout(brutalFocus, 10);
            setTimeout(brutalFocus, 50);
            setTimeout(brutalFocus, 100);
          }
        }
      }, true);

      // Keyboard shortcut to force focus (Escape key when not in modal)
      document.addEventListener('keydown', (e) => {
        // Pause focus keeper on Ctrl+V / Cmd+V so paste events reach
        // the correct element without webkitFocusFix interference.
        if (!e.repeat && (e.ctrlKey || e.metaKey) && !e.altKey &&
            (e.key === 'v' || e.key === 'V')) {
          const inp = getInput();
          if (inp) {
            const act = document.activeElement;
            if (act !== inp && !inp.contains(act) && !hasBlockingModal()) {
              inp.focus({ preventScroll: true });
            }
          }
          pauseFocusFor(2000);
        }
        if (e.key === 'Control' && !e.repeat) {
          const now = Date.now();
          if (lastCtrlTapAt && !ctrlTapInterrupted &&
              (now - lastCtrlTapAt) <= CTRL_DOUBLE_TAP_MS) {
            lastCtrlTapAt = 0;
            ctrlTapInterrupted = false;
            setManualPause(!manualPause);
          } else {
            lastCtrlTapAt = now;
            ctrlTapInterrupted = false;
          }
        } else if (lastCtrlTapAt) {
          ctrlTapInterrupted = true;
        }
        if (eventHasModifier(e)) setModifierHeld(true);
        if (e.key === 'Escape' && !hasBlockingModal()) {
          setTimeout(brutalFocus, 0);
        }
      }, true);

      document.addEventListener('keyup', (e) => {
        setModifierHeld(eventHasModifier(e));
      }, true);

      // IMPORTANT: Pause focus keeper on paste to allow image modal to appear
      let pasteTimeout = null;
      function reenableFocusAfterPaste() {
        if (!hasBlockingModal()) {
          enabled = true;
          rafEnabled = true;
          intervalId = setInterval(brutalFocus, 50);
          requestAnimationFrame(rafLoop);
          console.log('[Whatsie] Focus keeper re-enabled after paste');
        } else {
          // Modal still open — check again later
          pasteTimeout = setTimeout(reenableFocusAfterPaste, 1000);
        }
      }
      document.addEventListener('paste', (e) => {
        // Check if paste contains files/images
        const hasFiles = e.clipboardData && (
          e.clipboardData.files.length > 0 ||
          Array.from(e.clipboardData.items || []).some(item => item.type.startsWith('image/'))
        );

        if (hasFiles) {
          console.log('[Whatsie] Image paste detected - pausing focus keeper for 3s');
          enabled = false;
          rafEnabled = false;
          clearInterval(intervalId);

          if (pasteTimeout) clearTimeout(pasteTimeout);
          pasteTimeout = setTimeout(reenableFocusAfterPaste, 3000);
        }
      }, true);

      // Debug controls
      window._whatsieFocusControl = {
        enable: () => {
          enabled = true;
          rafEnabled = true;
          clearInterval(intervalId);
          intervalId = setInterval(brutalFocus, 50);
          requestAnimationFrame(rafLoop);
          console.log('[Whatsie] Focus keeper enabled');
        },
        disable: () => {
          enabled = false;
          rafEnabled = false;
          clearInterval(intervalId);
          console.log('[Whatsie] Focus keeper disabled');
        },
        force: brutalFocus,
        status: () => ({
          enabled,
          input: getInput(),
          modal: hasBlockingModal(),
          otherInput: isOnOtherInput(),
          modifierHeld,
          manualPause,
          active: document.activeElement,
          activeTag: document.activeElement?.tagName,
          activeTestId: document.activeElement?.getAttribute('data-testid')
        })
      };

      console.log('[Whatsie] Focus keeper v4 - BRUTAL mode with WebKit fix');
    })();
  )";
  this->runJavaScript(js);
}

void WebEnginePage::setupWebChannel(QWebEngineProfile *profile) {
  // Inject qwebchannel.js from Qt resources so JS can call
  // new QWebChannel(qt.webChannelTransport, ...).
  // Qt ships this file as :/qtwebchannel/qwebchannel.js when linking
  // against the webchannel module.
  const QString scriptName = QStringLiteral("whatsie_qwebchannel_api");
  // Avoid inserting twice if multiple pages share the same profile
  if (!profile->scripts()->find(scriptName).isEmpty())
    return;

  QFile f(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "[Whatsie] Failed to load :/qtwebchannel/qwebchannel.js"
               << "- audio transcription bridge will not work";
    return;
  }

  QWebEngineScript script;
  script.setName(scriptName);
  script.setSourceCode(QString::fromUtf8(f.readAll()));
  script.setInjectionPoint(QWebEngineScript::DocumentCreation);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  profile->scripts()->insert(script);

  // Inject early audio interceptor at DocumentCreation – runs before WhatsApp's
  // own JS so we can patch URL.createObjectURL, Audio constructor, etc.
  const QString interceptName = QStringLiteral("whatsie_audio_intercept");
  if (!profile->scripts()->find(interceptName).isEmpty())
    return;

  const QString interceptSrc = QStringLiteral(R"JS(
(function() {
  if (window._whatsieEarlyIntercept) return;
  window._whatsieEarlyIntercept = true;
  window._whatsieCaptured = [];  // [{buf, mime, t}]
  window._whatsieLastClick = {el: null, t: 0};

  // Record last clicked element (capture phase = fires before WhatsApp handlers)
  document.addEventListener('click', function(e) {
    window._whatsieLastClick = {el: e.target, t: Date.now()};
  }, true);

  function saveBuf(buf, mime) {
    try {
      var copy = buf.slice ? buf.slice(0) : buf;
      window._whatsieCaptured.push({buf: copy, mime: mime || 'audio/ogg', t: Date.now()});
      // Keep at most 10 recent buffers
      if (window._whatsieCaptured.length > 10)
        window._whatsieCaptured.shift();
    } catch(e) {}
  }

  // 1. Intercept URL.createObjectURL (captures blob URL creation)
  var origCOURL = URL.createObjectURL;
  URL.createObjectURL = function(obj) {
    var url = origCOURL.call(URL, obj);
    if (obj && obj instanceof Blob && obj.type && obj.type.indexOf('audio') >= 0) {
      (function(b, u) {
        b.arrayBuffer().then(function(ab) {
          saveBuf(ab, b.type);
          window._whatsieLastBlobUrl = u;
        }).catch(function() {});
      })(obj, url);
    }
    return url;
  };

  // 2. Intercept BaseAudioContext.decodeAudioData
  var origDecode = BaseAudioContext.prototype.decodeAudioData;
  BaseAudioContext.prototype.decodeAudioData = function(buf, ok, err) {
    saveBuf(buf, 'audio/ogg');
    window._whatsieLastDecodeTime = Date.now();
    return origDecode.apply(this, arguments);
  };

  // 3. Intercept Audio constructor (detached audio elements)
  var OrigAudio = window.Audio;
  window.Audio = function(src) {
    var a = src ? new OrigAudio(src) : new OrigAudio();
    var srcDesc = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'src');
    if (srcDesc && srcDesc.set) {
      Object.defineProperty(a, 'src', {
        configurable: true,
        get: srcDesc.get ? srcDesc.get.bind(a) : function() { return ''; },
        set: function(v) {
          srcDesc.set.call(a, v);
          if (v && v.indexOf('blob:') === 0) {
            window._whatsieLastBlobUrl = v;
            fetch(v).then(function(r) { return r.arrayBuffer(); })
              .then(function(ab) { saveBuf(ab, 'audio/ogg'); })
              .catch(function() {});
          }
        }
      });
    }
    return a;
  };
})();
)JS");

  QWebEngineScript iScript;
  iScript.setName(interceptName);
  iScript.setSourceCode(interceptSrc);
  iScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
  iScript.setWorldId(QWebEngineScript::MainWorld);
  iScript.setRunsOnSubFrames(false);
  profile->scripts()->insert(iScript);
}

void WebEnginePage::injectAudioTranscriber() {
  QString js = R"(
    (function() {
      if (window._whatsieAudioTranscriber) return;
      window._whatsieAudioTranscriber = true;

      var bridge = null;
      var LOG = function(msg) { console.log('[Whatsie] ' + msg); };

      // -----------------------------------------------------------------
      // QWebChannel init
      // -----------------------------------------------------------------
      function initChannel() {
        if (typeof QWebChannel === 'undefined' ||
            typeof qt === 'undefined' || !qt.webChannelTransport) {
          setTimeout(initChannel, 200); return;
        }
        new QWebChannel(qt.webChannelTransport, function(channel) {
          bridge = channel.objects.transcriber;
          bridge.transcriptionReady.connect(function(msgId, text) {
            showTranscription(msgId, text, false);
          });
          bridge.transcriptionError.connect(function(msgId, errText) {
            showTranscription(msgId, '\u26a0 ' + errText, true);
          });
          LOG('Bridge ready');
        });
      }

      // -----------------------------------------------------------------
      // Saved audio buffers: key = msgId, value = {buf, mime}
      // -----------------------------------------------------------------
      var savedBuffers = {};
      var lastMsgId    = null;

      // -----------------------------------------------------------------
      // Walk up from el to find the WA message container ([data-id] div)
      // -----------------------------------------------------------------
      function findMsgContainer(el) {
        var cur = el;
        // Pass 1: data-id attribute or role=row (WhatsApp message markers)
        for (var i = 0; i < 30 && cur && cur !== document.body; i++) {
          if (cur.dataset && cur.dataset.id) return cur;
          if (cur.getAttribute && cur.getAttribute('role') === 'row') return cur;
          cur = cur.parentElement;
        }
        // Pass 2: tabindex=-1 div inside #main
        cur = el;
        for (var j = 0; j < 30 && cur && cur !== document.body; j++) {
          if (cur.getAttribute && cur.getAttribute('tabindex') === '-1' &&
              cur.tagName === 'DIV' && cur.closest && cur.closest('#main'))
            return cur;
          cur = cur.parentElement;
        }
        // Pass 3: last resort — take the ancestor at depth 8-12 that is a
        // DIV inside #main.  Not perfect but always finds *something*.
        cur = el;
        for (var k = 0; k < 15 && cur && cur !== document.body; k++) {
          cur = cur.parentElement;
          if (!cur) break;
          if (k >= 7 && cur.tagName === 'DIV' &&
              cur.closest && cur.closest('#main'))
            return cur;
        }
        return null;
      }

      // -----------------------------------------------------------------
      // Add a 📝 transcribe button to a message container
      // -----------------------------------------------------------------
      var processedContainers = new WeakSet();
      function addButtonToContainer(container, msgId) {
        if (processedContainers.has(container)) return;
        processedContainers.add(container);
        container.dataset.whatsieId = msgId;

        var btn = document.createElement('button');
        btn.className = 'whatsie-transcribe-btn';
        btn.title = 'Transcrever \u00e1udio (Whisper)';
        btn.textContent = '\uD83D\uDCDD';
        btn.setAttribute('type', 'button');
        btn.style.cssText = [
          'all:initial',
          'display:inline-flex',
          'align-items:center',
          'justify-content:center',
          'width:28px',
          'height:28px',
          'border:none',
          'border-radius:50%',
          'background:rgba(0,130,0,0.18)',
          'cursor:pointer',
          'font-size:14px',
          'margin-left:6px',
          'vertical-align:middle',
          'flex-shrink:0',
          'transition:background 0.15s',
          'font-family:sans-serif',
          'z-index:9999',
        ].join(';');
        btn.onmouseenter = function() { btn.style.background='rgba(0,130,0,0.35)'; };
        btn.onmouseleave = function() { btn.style.background='rgba(0,130,0,0.18)'; };
        btn.onclick = function(e) {
          e.stopPropagation(); e.preventDefault();
          transcribeById(msgId, btn);
        };
        container.appendChild(btn);
      }

      // -----------------------------------------------------------------
      // Pull captured buffers from the early DocumentCreation interceptor
      // (window._whatsieCaptured) and also keep our own decodeAudioData
      // intercept as a secondary layer.
      // -----------------------------------------------------------------
      function pollCaptured() {
        var caps = window._whatsieCaptured || [];
        if (caps.length > 0) {
          var latest = caps[caps.length - 1];
          var msgId = 'wa_' + Math.random().toString(36).slice(2) + '_' + Date.now();
          savedBuffers[msgId] = {buf: latest.buf, mime: latest.mime || 'audio/ogg'};
          lastMsgId = msgId;
          LOG('Audio pulled from early intercept: ' + Math.round(latest.buf.byteLength / 1024) + ' KB');
          tryAttachButtonForLastDecode(msgId);
          // Clear consumed buffers
          window._whatsieCaptured = [];
        }
      }
      // Poll for new captures every 300ms
      setInterval(pollCaptured, 300);

      // -----------------------------------------------------------------
      // After a decode call, look for the message container that was
      // activated (WhatsApp usually adds a "playing" class or changes
      // aria-label on the play button)
      // -----------------------------------------------------------------
      var lastAriaChangedEl = null;
      var ariaObserver = new MutationObserver(function(muts) {
        muts.forEach(function(m) {
          if (m.type === 'attributes' && m.attributeName === 'aria-label') {
            lastAriaChangedEl = m.target;
          }
        });
      });
      ariaObserver.observe(document.body, {
        attributes: true, subtree: true, attributeFilter: ['aria-label']
      });

      function tryAttachButtonForLastDecode(msgId) {
        // --- pick best anchor element (proper cascade) ---
        var el = null;
        var lc = window._whatsieLastClick || {el: null, t: 0};
        if (lc.el && (Date.now() - lc.t) < 3000 &&
            lc.el !== document.body && lc.el !== document.documentElement) {
          try { if (lc.el.closest && lc.el.closest('#main')) el = lc.el; } catch(e){}
        }
        if (!el && lastAriaChangedEl && lastAriaChangedEl !== document.body) {
          try {
            if (lastAriaChangedEl.closest && lastAriaChangedEl.closest('#main'))
              el = lastAriaChangedEl;
          } catch(e){}
        }
        LOG('tryAttach: el=' + (el ? el.tagName : 'none') +
            ' click_age=' + (Date.now() - lc.t) + 'ms');
        if (!el) { LOG('tryAttach: no anchor'); return; }

        // Walk up from el to find the CONTROLS ROW — the first ancestor with
        // 2+ children that is inside #main.  That is the row that has the
        // play button, waveform and duration side-by-side.
        var insertParent = null;
        var cur = el;
        for (var k = 0; k < 15 && cur && cur !== document.body; k++) {
          var p = cur.parentElement;
          if (!p || p === document.body) break;
          try {
            if (p.children.length >= 2 && p.closest && p.closest('#main')) {
              insertParent = p;
              break;
            }
          } catch(e) {}
          cur = p;
        }

        // Container for data-whatsie-id tracking (higher level)
        var container = findMsgContainer(el);
        LOG('tryAttach: insertParent=' +
            (insertParent ? insertParent.tagName+'['+insertParent.children.length+'ch]' : 'null') +
            ' container=' + (container ? container.tagName : 'null'));

        if (!insertParent && !container) { LOG('tryAttach: nowhere to insert'); return; }

        var tracker = container || insertParent;
        tracker.dataset.whatsieId = msgId;
        if (processedContainers.has(tracker)) { LOG('tryAttach: already done'); return; }
        processedContainers.add(tracker);

        var btn = document.createElement('button');
        btn.className = 'whatsie-transcribe-btn';
        btn.title = 'Transcrever \u00e1udio (Whisper)';
        btn.textContent = '\uD83D\uDCDD';
        btn.setAttribute('type', 'button');
        btn.style.cssText = [
          'all:initial', 'display:inline-flex', 'align-items:center',
          'justify-content:center', 'width:28px', 'height:28px',
          'border:none', 'border-radius:50%', 'background:rgba(0,130,0,0.18)',
          'cursor:pointer', 'font-size:14px', 'margin-left:6px',
          'vertical-align:middle', 'flex-shrink:0', 'transition:background 0.15s',
          'font-family:sans-serif', 'z-index:9999',
        ].join(';');
        btn.onmouseenter = function() { btn.style.background='rgba(0,130,0,0.35)'; };
        btn.onmouseleave = function() { btn.style.background='rgba(0,130,0,0.18)'; };
        btn.onclick = function(e) {
          e.stopPropagation(); e.preventDefault();
          transcribeById(msgId, btn);
        };

        // Insert inline in the controls row (play+waveform+duration row)
        if (insertParent) {
          insertParent.appendChild(btn);
        } else {
          container.appendChild(btn);
        }
      }

      // -----------------------------------------------------------------
      // Also watch for <audio> elements (fallback for older WA versions
      // that do use HTML audio)
      // -----------------------------------------------------------------
      var processedAudio = new WeakSet();
      function tryAttachFromAudio(audio) {
        if (processedAudio.has(audio)) return;
        var src = audio.src || audio.currentSrc || '';
        if (!src) {
          audio.addEventListener('loadstart', function h() {
            audio.removeEventListener('loadstart', h);
            tryAttachFromAudio(audio);
          });
          return;
        }
        if (src.indexOf('blob:') !== 0) return;
        processedAudio.add(audio);

        var msgId = 'wa_audio_' + Math.random().toString(36).slice(2);
        // Lazily fetch the blob and save it
        fetch(src).then(function(r) { return r.arrayBuffer(); }).then(function(buf) {
          savedBuffers[msgId] = {buf: buf, mime: audio.type || 'audio/ogg'};
          lastMsgId = msgId;
          var container = findMsgContainer(audio);
          if (container) addButtonToContainer(container, msgId);
        }).catch(function() {});
      }

      var audioElObserver = new MutationObserver(function(muts) {
        muts.forEach(function(m) {
          m.addedNodes && m.addedNodes.forEach && m.addedNodes.forEach(function(n) {
            if (!n || n.nodeType !== 1) return;
            if (n.tagName === 'AUDIO') tryAttachFromAudio(n);
            else if (n.querySelectorAll) n.querySelectorAll('audio').forEach(tryAttachFromAudio);
          });
          if (m.type === 'attributes' && m.target && m.target.tagName === 'AUDIO')
            tryAttachFromAudio(m.target);
        });
      });
      audioElObserver.observe(document.body, {
        childList: true, subtree: true,
        attributes: true, attributeFilter: ['src']
      });

      // -----------------------------------------------------------------
      // Transcribe by message id (uses saved buffer)
      // -----------------------------------------------------------------
      function transcribeById(msgId, btn) {
        var saved = savedBuffers[msgId] || (lastMsgId && savedBuffers[lastMsgId]);
        if (!saved) {
          showTranscription(msgId, '\u26a0 Pressione play primeiro, depois clique \uD83D\uDCDD', true);
          return;
        }
        if (!bridge) {
          showTranscription(msgId, '\u26a0 Bridge n\u00e3o pronto, aguarde...', true);
          return;
        }
        btn.textContent = '\u23F3'; btn.disabled = true;
        var u8 = new Uint8Array(saved.buf);
        var bin = '';
        for (var i = 0; i < u8.length; i += 8192)
          bin += String.fromCharCode.apply(null, u8.subarray(i, i + 8192));
        bridge.requestTranscription(btoa(bin), msgId, saved.mime);
      }

      // -----------------------------------------------------------------
      // Show transcription text below the message container
      // -----------------------------------------------------------------
      function showTranscription(msgId, text, isError) {
        var container = document.querySelector('[data-whatsie-id="' + msgId + '"]');
        // Fallback: if we don't have the exact container, find last one
        if (!container) {
          var all = document.querySelectorAll('[data-whatsie-id]');
          container = all[all.length - 1] || null;
        }
        if (!container) { LOG('showTranscription: no container for ' + msgId); return; }
        var btn = container.querySelector('.whatsie-transcribe-btn');
        if (btn) { btn.textContent = '\uD83D\uDCDD'; btn.disabled = false; }
        var div = container.querySelector('.whatsie-transcription');
        if (!div) {
          div = document.createElement('div');
          div.className = 'whatsie-transcription';
          div.style.cssText = [
            'margin-top:6px', 'padding:6px 10px', 'border-radius:8px',
            'font-size:13px', 'line-height:1.4', 'background:rgba(0,0,0,0.07)',
            'cursor:text', 'user-select:text', '-webkit-user-select:text',
            'white-space:pre-wrap', 'word-break:break-word', 'display:block',
          ].join(';');
          container.appendChild(div);
        }
        div.style.color = isError ? '#c00' : 'inherit';
        div.textContent = text;
      }

      initChannel();
      LOG('Audio transcriber v3 active - intercepting decodeAudioData');

      window._whatsieTranscriber = {
        status: function() { return {bridge: !!bridge, v: 3, buffers: Object.keys(savedBuffers).length, last: lastMsgId}; }
      };
    })();
  )";
  this->runJavaScript(js);
}
