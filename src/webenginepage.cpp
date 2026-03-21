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
      let lastTypingTime = 0;
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

      // Focus target directly (Qt WebEngine = Chromium, no WebKit trick needed)
      function focusTarget(target) {
        target.focus({ preventScroll: true });
      }

      function pauseFocusFor(ms) {
        const until = Date.now() + ms;
        if (until > wheelPauseUntil) wheelPauseUntil = until;
      }

      function isTypingRecently() {
        return (Date.now() - lastTypingTime) < 400;
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
        focusTarget(input);
        if (pausedByModifier) {
          pausedByModifier = false;
          showFocusToast('Focus restored', 'resume');
        }
      }

      // Keep polling only as a low-frequency fallback; the event hooks handle
      // the interactive cases immediately.
      let intervalId = setInterval(brutalFocus, 500);

      // Detect modal close and focus (debounced via rAF to avoid jank during typing)
      let observerPending = false;
      const observer = new MutationObserver(() => {
        if (observerPending) return;
        observerPending = true;
        requestAnimationFrame(() => {
          observerPending = false;
          if (isTypingRecently()) return;
          const currentModalState = hasBlockingModal();
          if (lastModalState && !currentModalState) {
            brutalFocus();
            setTimeout(brutalFocus, 50);
            setTimeout(brutalFocus, 150);
            setTimeout(brutalFocus, 300);
          }
          lastModalState = currentModalState;
        });
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
            setTimeout(brutalFocus, 50);
            setTimeout(brutalFocus, 100);
          }
        }
      }, true);

      // Keyboard shortcut to force focus (Escape key when not in modal)
      document.addEventListener('keydown', (e) => {
        // Track active typing to prevent focus interference during keystrokes
        if (!e.ctrlKey && !e.altKey && !e.metaKey &&
            (e.key.length === 1 || e.key === 'Backspace' ||
             e.key === 'Delete' || e.key === 'Enter')) {
          lastTypingTime = Date.now();
        }
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
          clearInterval(intervalId);
          intervalId = setInterval(brutalFocus, 500);
          console.log('[Whatsie] Focus keeper re-enabled after paste');
        } else {
          // Modal still open — check again later
          pasteTimeout = setTimeout(reenableFocusAfterPaste, 1000);
        }
      }
      document.addEventListener('paste', (e) => {
        // Block duplicate native paste when C++ bridge already injected one
        if (window._whatsieInjectingPaste && !e._whatsieInjected) {
          e.preventDefault();
          e.stopImmediatePropagation();
          console.log('[Whatsie] Blocked duplicate native paste');
          return;
        }

        // Check if paste contains files/images
        const hasFiles = e.clipboardData && (
          e.clipboardData.files.length > 0 ||
          Array.from(e.clipboardData.items || []).some(item => item.type.startsWith('image/'))
        );

        if (hasFiles) {
          console.log('[Whatsie] Image paste detected - pausing focus keeper for 3s');
          enabled = false;
          clearInterval(intervalId);

          if (pasteTimeout) clearTimeout(pasteTimeout);
          pasteTimeout = setTimeout(reenableFocusAfterPaste, 3000);
        }
      }, true);

      // Debug controls
      window._whatsieFocusControl = {
        enable: () => {
          enabled = true;
          clearInterval(intervalId);
          intervalId = setInterval(brutalFocus, 500);
          console.log('[Whatsie] Focus keeper enabled');
        },
        disable: () => {
          enabled = false;
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
  window._whatsieCaptured = null;  // {buf, mime, t}
  window._whatsieCaptureNotifyPending = false;
  window._whatsieLastClick = {el: null, t: 0};

  // Record last clicked element (capture phase = fires before WhatsApp handlers)
  document.addEventListener('click', function(e) {
    window._whatsieLastClick = {el: e.target, t: Date.now()};
  }, true);

  function saveBuf(buf, mime) {
    try {
      var copy = buf;
      if (buf && typeof ArrayBuffer !== 'undefined' &&
          buf instanceof ArrayBuffer && buf.slice) {
        copy = buf.slice(0);
      } else if (buf && typeof ArrayBuffer !== 'undefined' &&
                 typeof ArrayBuffer.isView === 'function' &&
                 ArrayBuffer.isView(buf) && buf.buffer) {
        copy = buf.buffer.slice ? buf.buffer.slice(0) : buf.buffer;
      } else if (buf && buf.slice) {
        copy = buf.slice(0);
      }
      window._whatsieCaptured = {
        buf: copy,
        mime: mime || 'audio/ogg',
        t: Date.now()
      };
      if (window._whatsieCaptureNotifyPending) return;
      window._whatsieCaptureNotifyPending = true;
      setTimeout(function() {
        window._whatsieCaptureNotifyPending = false;
        if (typeof window._whatsieOnCapturedAudio === 'function') {
          try { window._whatsieOnCapturedAudio(); } catch(e) {}
        }
      }, 0);
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
      var DEBUG = true;
      var LOG = function(msg) {
        if (DEBUG) console.log('[Whatsie] ' + msg);
      };

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
            removeSavedBuffer(msgId);
          });
          bridge.transcriptionError.connect(function(msgId, errText) {
            showTranscription(msgId, '\u26a0 ' + errText, true);
            removeSavedBuffer(msgId);
          });
          LOG('Bridge ready');
        });
      }

      // -----------------------------------------------------------------
      // Saved audio buffers: key = msgId, value = {buf, mime}
      // -----------------------------------------------------------------
      var MAX_SAVED_BUFFERS = 4;
      var MAX_SAVED_BUFFER_BYTES = 32 * 1024 * 1024;
      var savedBuffers = {};
      var savedBufferOrder = [];
      var savedBufferBytes = 0;
      var lastMsgId    = null;
      var capturedFlushTimer = null;
      var mainRoot = null;
      var ariaObserverActive = false;
      var ariaObserverStopTimer = null;
      var audioObserverActive = false;
      var audioObserverStopTimer = null;

      function getMainRoot() {
        if (mainRoot && document.documentElement &&
            document.documentElement.contains(mainRoot)) {
          return mainRoot;
        }
        mainRoot = document.querySelector('#main') || document.body;
        return mainRoot;
      }

      function removeSavedBuffer(msgId) {
        var saved = savedBuffers[msgId];
        if (!saved) return;
        savedBufferBytes = Math.max(0, savedBufferBytes - (saved.size || 0));
        delete savedBuffers[msgId];
        var idx = savedBufferOrder.indexOf(msgId);
        if (idx >= 0) savedBufferOrder.splice(idx, 1);
        if (lastMsgId === msgId) {
          lastMsgId = savedBufferOrder.length
            ? savedBufferOrder[savedBufferOrder.length - 1]
            : null;
        }
      }

      function rememberSavedBuffer(msgId, buf, mime) {
        removeSavedBuffer(msgId);
        var size = buf && typeof buf.byteLength === 'number'
          ? buf.byteLength
          : 0;
        savedBuffers[msgId] = {
          buf: buf,
          mime: mime || 'audio/ogg',
          size: size,
          pending: false
        };
        savedBufferOrder.push(msgId);
        savedBufferBytes += size;
        lastMsgId = msgId;

        while ((savedBufferOrder.length > MAX_SAVED_BUFFERS ||
                (savedBufferBytes > MAX_SAVED_BUFFER_BYTES &&
                 savedBufferOrder.length > 1))) {
          removeSavedBuffer(savedBufferOrder[0]);
        }
      }

      // -----------------------------------------------------------------
      // Walk up from el to find the WA message container ([data-id] div)
      // -----------------------------------------------------------------
      function findMsgContainer(el) {
        var cur = el;
        for (var i = 0; i < 30 && cur && cur !== document.body; i++) {
          if (cur.dataset && cur.dataset.id) return cur;
          if (cur.getAttribute && cur.getAttribute('role') === 'row') return cur;
          cur = cur.parentElement;
        }
        cur = el;
        for (var j = 0; j < 30 && cur && cur !== document.body; j++) {
          if (cur.getAttribute && cur.getAttribute('tabindex') === '-1' &&
              cur.tagName === 'DIV' && cur.closest && cur.closest('#main'))
            return cur;
          cur = cur.parentElement;
        }
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
      // Check if a message container has audio content
      // -----------------------------------------------------------------
      function hasAudioContent(container) {
        if (!container) return false;
        return !!(
          container.querySelector('[data-icon="ptt-status"]') ||
          container.querySelector('[role="slider"][aria-valuetext]') ||
          container.querySelector('button[aria-label="Play voice message"]') ||
          container.querySelector('audio')
        );
      }

      // -----------------------------------------------------------------
      // Pull captured buffers from the early DocumentCreation interceptor
      // -----------------------------------------------------------------
      function scheduleCapturedFlush() {
        if (capturedFlushTimer) return;
        capturedFlushTimer = setTimeout(function() {
          capturedFlushTimer = null;
          var latest = window._whatsieCaptured;
          if (!latest || !latest.buf) return;
          window._whatsieCaptured = null;

          var msgId = 'wa_' + Math.random().toString(36).slice(2) + '_' + Date.now();
          rememberSavedBuffer(msgId, latest.buf, latest.mime || 'audio/ogg');
          armAriaObserver(3000);
          armAudioObserver(4000);
          LOG('Audio captured: ' +
              Math.round((latest.buf.byteLength || 0) / 1024) + ' KB');
          associateMsgWithBuffer(msgId);
        }, 120);
      }
      window._whatsieOnCapturedAudio = scheduleCapturedFlush;
      scheduleCapturedFlush();
      setInterval(scheduleCapturedFlush, 2000);

      // -----------------------------------------------------------------
      // Aria observer: track play button state changes to find which
      // message is currently playing audio
      // -----------------------------------------------------------------
      var lastAriaChangedEl = null;
      var ariaObserver = new MutationObserver(function(muts) {
        muts.forEach(function(m) {
          if (m.type === 'attributes' && m.attributeName === 'aria-label') {
            lastAriaChangedEl = m.target;
          }
        });
      });
      function armAriaObserver(durationMs) {
        if (ariaObserverStopTimer) clearTimeout(ariaObserverStopTimer);
        if (!ariaObserverActive) {
          ariaObserver.observe(getMainRoot(), {
            attributes: true, subtree: true, attributeFilter: ['aria-label']
          });
          ariaObserverActive = true;
        }
        ariaObserverStopTimer = setTimeout(function() {
          ariaObserver.disconnect();
          ariaObserverActive = false;
        }, durationMs || 3000);
      }

      // -----------------------------------------------------------------
      // Associate a message container with a captured audio buffer
      // (sets data-whatsie-id on the container for later lookup)
      // -----------------------------------------------------------------
      function associateMsgWithBuffer(msgId) {
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
        LOG('associateMsg: el=' + (el ? el.tagName : 'none'));
        if (!el) return;
        var container = findMsgContainer(el);
        if (container) {
          container.dataset.whatsieId = msgId;
          LOG('Associated msg ' + msgId + ' with container');
        }
      }

      // -----------------------------------------------------------------
      // Watch for <audio> elements (fallback for older WA versions)
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
        fetch(src).then(function(r) { return r.arrayBuffer(); }).then(function(buf) {
          rememberSavedBuffer(msgId, buf, audio.type || 'audio/ogg');
          var container = findMsgContainer(audio);
          if (container) container.dataset.whatsieId = msgId;
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
      function armAudioObserver(durationMs) {
        if (audioObserverStopTimer) clearTimeout(audioObserverStopTimer);
        if (!audioObserverActive) {
          audioElObserver.observe(getMainRoot(), {
            childList: true, subtree: true,
            attributes: true, attributeFilter: ['src']
          });
          audioObserverActive = true;
        }
        audioObserverStopTimer = setTimeout(function() {
          audioElObserver.disconnect();
          audioObserverActive = false;
        }, durationMs || 4000);
      }

      // -----------------------------------------------------------------
      // Transcribe by message id (uses saved buffer)
      // -----------------------------------------------------------------
      function transcribeMsg(msgId) {
        var saved = savedBuffers[msgId] || (lastMsgId && savedBuffers[lastMsgId]);
        if (!saved) {
          showTranscription(msgId, '\u26a0 Pressione play primeiro, depois use Transcribe', true);
          return;
        }
        if (!bridge) {
          showTranscription(msgId, '\u26a0 Bridge n\u00e3o pronto, aguarde...', true);
          return;
        }
        if (saved.pending) return;
        saved.pending = true;
        showTranscription(msgId, '\u23f3 Transcrevendo...', false);
        var u8 = new Uint8Array(saved.buf);
        var bin = '';
        for (var i = 0; i < u8.length; i += 8192)
          bin += String.fromCharCode.apply(null, u8.subarray(i, i + 8192));
        try {
          bridge.requestTranscription(btoa(bin), msgId, saved.mime);
        } catch (err) {
          saved.pending = false;
          showTranscription(msgId, '\u26a0 Falha ao iniciar transcri\u00e7\u00e3o', true);
        }
      }

      // -----------------------------------------------------------------
      // Show transcription text below the message container
      // -----------------------------------------------------------------
      function showTranscription(msgId, text, isError) {
        var container = document.querySelector('[data-whatsie-id="' + msgId + '"]');
        if (!container) {
          var all = document.querySelectorAll('[data-whatsie-id]');
          container = all[all.length - 1] || null;
        }
        if (!container) { LOG('showTranscription: no container for ' + msgId); return; }
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

      // =================================================================
      // CONTEXT MENU INTEGRATION
      // Instead of injecting a button into the audio message DOM (which
      // WhatsApp's React re-renders constantly remove), we inject a
      // "Transcribe" item into WhatsApp's own context menu when it
      // appears on an audio message.
      // =================================================================
      var lastClickedMsgContainer = null;

      // -----------------------------------------------------------------
      // MutationObserver: detect when WhatsApp's context menu appears
      // and inject our "Transcribe" item. This is much more reliable
      // than polling after clicks because it fires regardless of how
      // the menu was triggered.
      // -----------------------------------------------------------------
      var menuObserverThrottle = null;
      var contextMenuObserver = new MutationObserver(function() {
        if (menuObserverThrottle) return;
        menuObserverThrottle = setTimeout(function() {
          menuObserverThrottle = null;
          tryInjectTranscribe();
        }, 80);
      });

      function tryInjectTranscribe() {
        // Find a UL outside #main/#side with 3+ li[role="button"] items
        var uls = document.querySelectorAll('ul');
        for (var i = 0; i < uls.length; i++) {
          var ul = uls[i];
          if (ul.closest && ul.closest('#main')) continue;
          if (ul.closest && ul.closest('#side')) continue;
          if (ul.querySelector('.whatsie-transcribe-menu-item')) continue;
          var menuItems = ul.querySelectorAll('li[role="button"]');
          if (menuItems.length >= 3) {
            LOG('Context menu found: ' + menuItems.length + ' items');
            injectTranscribeMenuItem(ul);
            return;
          }
        }
      }

      // Clone an existing menu item, restyle it as "Transcribe", and
      // insert it into the context menu.
      function injectTranscribeMenuItem(menuUl) {
        if (menuUl.querySelector('.whatsie-transcribe-menu-item')) return;

        // WhatsApp menu structure:
        //   <ul> -> <div>(wrapper) -> <div><li role="button"><div>[icon+text]</div></li></div> x N
        var wrapper = menuUl.children[0];
        if (!wrapper || !wrapper.children.length) {
          LOG('inject: no wrapper div');
          return;
        }

        // Find the first item wrapper (a <div> containing a <li>)
        var templateItem = null;
        for (var i = 0; i < wrapper.children.length; i++) {
          if (wrapper.children[i].querySelector &&
              wrapper.children[i].querySelector('li[role="button"]')) {
            templateItem = wrapper.children[i];
            break;
          }
        }
        if (!templateItem) { LOG('inject: no template item'); return; }

        var newItem = templateItem.cloneNode(true);
        newItem.classList.add('whatsie-transcribe-menu-item');

        // Strip data attributes to avoid WhatsApp React interference
        newItem.querySelectorAll('[data-testid]').forEach(function(el) {
          el.removeAttribute('data-testid');
        });
        newItem.querySelectorAll('[data-icon]').forEach(function(el) {
          el.removeAttribute('data-icon');
        });

        // Replace text: find the <span> with text content
        var textFound = false;
        var allEls = newItem.querySelectorAll('span');
        for (var i = allEls.length - 1; i >= 0; i--) {
          var el = allEls[i];
          if (el.children.length === 0 && el.textContent.trim().length > 0) {
            el.textContent = 'Transcribe';
            textFound = true;
            break;
          }
        }
        if (!textFound) { LOG('inject: no text element found'); return; }

        // Replace icon: swap SVG for emoji
        var svg = newItem.querySelector('svg');
        if (svg) {
          var iconParent = svg.parentElement;
          svg.remove();
          var iconSpan = document.createElement('span');
          iconSpan.textContent = '\uD83D\uDCDD';
          iconSpan.style.cssText = 'font-size:18px;line-height:1;';
          iconParent.appendChild(iconSpan);
        }

        // Wire up the transcription click handler
        var li = newItem.querySelector('li');
        if (li) {
          li.addEventListener('click', function(e) {
            e.stopPropagation();
            e.preventDefault();

            // Close the context menu via Escape
            document.dispatchEvent(new KeyboardEvent('keydown', {
              key: 'Escape', code: 'Escape', keyCode: 27,
              bubbles: true, cancelable: true
            }));

            // Find the audio message container
            var audioContainer = lastClickedMsgContainer;
            if (!audioContainer || !hasAudioContent(audioContainer)) {
              LOG('Transcribe clicked but no audio message found');
              return;
            }
            var msgId = audioContainer.dataset.whatsieId;
            if (!msgId) {
              msgId = 'wa_menu_' + Date.now();
              audioContainer.dataset.whatsieId = msgId;
            }
            transcribeMsg(msgId);
          }, true);
        }

        // Insert before the <hr> separator, or at the end of the wrapper
        var hr = wrapper.querySelector('hr');
        if (hr) {
          wrapper.insertBefore(newItem, hr);
        } else {
          wrapper.appendChild(newItem);
        }
        LOG('Transcribe menu item injected');
      }

      // Start observing for context menus
      contextMenuObserver.observe(document.documentElement, {
        childList: true, subtree: true
      });

      // -----------------------------------------------------------------
      // Click listener: track last clicked message + arm audio observers
      // -----------------------------------------------------------------
      document.addEventListener('click', function(e) {
        try {
          if (e.target && e.target.closest && e.target.closest('#main')) {
            armAriaObserver(3000);
            armAudioObserver(4000);
          }
          var msg = findMsgContainer(e.target);
          if (msg) lastClickedMsgContainer = msg;
        } catch(err) {}
      }, true);

      initChannel();
      LOG('Audio transcriber v4 active - context menu integration');

      window._whatsieTranscriber = {
        status: function() {
          return {
            bridge: !!bridge, v: 4,
            buffers: Object.keys(savedBuffers).length,
            last: lastMsgId
          };
        }
      };
    })();
  )";
  this->runJavaScript(js);
}
