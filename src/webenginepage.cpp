#include "webenginepage.h"

QWebEngineView *WebEnginePage::view() const {
    return qobject_cast<QWebEngineView *>(parent());
}

WebEnginePage::WebEnginePage(QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent) {

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

  connect(this, &QWebEnginePage::certificateError, this, [this](QWebEngineCertificateError error) {
      QWidget *mainWindow = view()->window();
      if (error.isOverridable()) {
        QDialog dialog(mainWindow);
        dialog.setModal(true);
        dialog.setWindowFlags(dialog.windowFlags() &
                              ~Qt::WindowContextHelpButtonHint);
        Ui::CertificateErrorDialog certificateDialog;
        certificateDialog.setupUi(&dialog);
        certificateDialog.m_iconLabel->setText(QString());
        QIcon icon(mainWindow->style()->standardIcon(QStyle::SP_MessageBoxWarning,
                                                     nullptr, mainWindow));
        certificateDialog.m_iconLabel->setPixmap(icon.pixmap(32, 32));
        certificateDialog.m_errorLabel->setText(error.description());
        dialog.setWindowTitle(tr("Certificate Error"));
        if (dialog.exec() == QDialog::Accepted) {
            error.acceptCertificate();
            return;
        }
      }

      QMessageBox::critical(mainWindow, tr("Certificate Error"),
                            error.description());
      error.rejectCertificate();
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
    injectInputFocusKeeper();
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
  Q_UNUSED(level);
  Q_UNUSED(message);
  Q_UNUSED(lineId);
  Q_UNUSED(sourceId);
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

void WebEnginePage::injectInputFocusKeeper() {
  QString js = R"(
    (function() {
      if (window._whatsieFocusKeeperV4) return;
      window._whatsieFocusKeeperV4 = true;

      let enabled = true;
      let lastModalState = false;

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

      // Check if image/media editor overlay is open (paste image, send file, etc)
      function hasMediaEditor() {
        // The image editor toolbar/overlay
        if (document.querySelector('[data-testid="media-editor"]')) return true;
        if (document.querySelector('[data-testid="image-editor"]')) return true;
        // The draw/edit tools bar visible in screenshot
        if (document.querySelector('[data-testid="media-canvas-container"]')) return true;
        // Media preview panel (when you paste/attach image)
        if (document.querySelector('[data-testid="media-preview"]')) return true;
        // Check for the X button that closes media editor
        if (document.querySelector('[data-testid="x-viewer"]')) return true;
        // Any overlay with image editing tools
        const overlay = document.querySelector('#app > div > span:nth-child(4) > div');
        if (overlay && overlay.querySelector('[data-testid="send"]')) return true;
        return false;
      }

      // Only block focus when these specific modals are open
      function hasBlockingModal() {
        // Media editor - user might want to type caption
        if (hasMediaEditor()) return true;
        if (document.querySelector('[data-testid="media-editor-modal"]')) return true;
        if (document.querySelector('[data-testid="media-picker-modal"]')) return true;
        if (document.querySelector('[data-testid="forward-message-modal"]')) return true;
        if (document.querySelector('[data-testid="popup-contents"]')) return true;
        if (document.querySelector('[role="dialog"][aria-modal="true"]')) return true;
        // Generic overlay detection - if there's a modal-like overlay
        const appOverlays = document.querySelectorAll('#app > div > span > div[tabindex="-1"]');
        for (const ov of appOverlays) {
          if (ov.querySelector('input, textarea, [contenteditable="true"]')) return true;
        }
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

      // BRUTAL force focus with WebKit workaround
      function brutalFocus() {
        if (!enabled) return;
        if (hasBlockingModal()) return;
        if (isOnOtherInput()) return;

        const input = getInput();
        if (!input) return;

        const active = document.activeElement;

        // Already on target
        if (active === input) return;
        if (input.contains(active)) return;

        // Use WebKit workaround
        webkitFocusFix(input);
      }

      // Ultra aggressive loop - 25ms interval
      let intervalId = setInterval(brutalFocus, 25);

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
        if (hasBlockingModal()) return;

        const input = getInput();
        if (!input) return;

        const main = document.querySelector('#main');
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
      document.addEventListener('mouseup', () => {
        if (hasBlockingModal()) return;
        setTimeout(brutalFocus, 0);
        setTimeout(brutalFocus, 50);
      }, true);

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
        if (e.key === 'Escape' && !hasBlockingModal()) {
          setTimeout(brutalFocus, 0);
        }
      }, true);

      // Debug controls
      window._whatsieFocusControl = {
        enable: () => {
          enabled = true;
          rafEnabled = true;
          clearInterval(intervalId);
          intervalId = setInterval(brutalFocus, 25);
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
