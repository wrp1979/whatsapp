#include "webview.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMimeData>
#include <QWebEngineProfile>
#include <QWebEngineContextMenuRequest>
#include <mainwindow.h>

WebView::WebView(QWidget *parent, QStringList dictionaries)
    : QWebEngineView(parent), m_dictionaries(dictionaries) {

  QObject *parentMainWindow = this->parent();
  while (!parentMainWindow->objectName().contains("MainWindow")) {
    parentMainWindow = parentMainWindow->parent();
  }
  MainWindow *mainWindow = dynamic_cast<MainWindow *>(parentMainWindow);

  connect(this, &WebView::titleChanged, mainWindow,
          &MainWindow::handleWebViewTitleChanged);
  connect(this, &WebView::loadFinished, mainWindow,
          &MainWindow::handleLoadFinished);
  // Intercept Ctrl+V to bridge image clipboard from system → JS.
  // Qt WebEngine on Linux often fails to pass image data from the X11/Wayland
  // clipboard into JavaScript's clipboardData, so we read it via QClipboard
  // and inject a synthetic ClipboardEvent with the image as a File blob.
  // QShortcut doesn't work here (rendering widget eats key events),
  // so we install an app-level event filter instead.
  qApp->installEventFilter(this);

  connect(this, &WebView::renderProcessTerminated,
          [this](QWebEnginePage::RenderProcessTerminationStatus termStatus,
                 int statusCode) {
            QString status;
            switch (termStatus) {
            case QWebEnginePage::NormalTerminationStatus:
              status = tr("Render process normal exit");
              break;
            case QWebEnginePage::AbnormalTerminationStatus:
              status = tr("Render process abnormal exit");
              break;
            case QWebEnginePage::CrashedTerminationStatus:
              status = tr("Render process crashed");
              break;
            case QWebEnginePage::KilledTerminationStatus:
              status = tr("Render process killed");
              break;
            }
            QMessageBox::StandardButton btn =
                QMessageBox::question(window(), status,
                                      tr("Render process exited with code: %1\n"
                                         "Do you want to reload the page ?")
                                          .arg(statusCode));
            if (btn == QMessageBox::Yes)
              QTimer::singleShot(0, this, [this] { this->reload(); });
          });
}

void WebView::wheelEvent(QWheelEvent *event) {
  bool controlKeyIsHeld =
      QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
  // this doesn't work, (even after checking the global QApplication keyboard
  // modifiers) as expected, the Ctrl+wheel is managed by Chromium
  // WebenginePage directly. So, we manage it by injecting js to page using
  // WebEnginePage::injectPreventScrollWheelZoomHelper
  if ((event->modifiers() & Qt::ControlModifier) != 0 || controlKeyIsHeld) {
    qDebug() << "skipped ctrl + m_wheel event on webengineview";
    event->ignore();
  } else {
    QWebEngineView::wheelEvent(event);
  }
}

void WebView::contextMenuEvent(QContextMenuEvent *event) {

  auto menu = createStandardContextMenu();
  menu->setAttribute(Qt::WA_DeleteOnClose, true);
  // hide reload, back, forward, savepage, copyimagelink menus
  foreach (auto *action, menu->actions()) {
    if (action == page()->action(QWebEnginePage::SavePage) ||
        action == page()->action(QWebEnginePage::Reload) ||
        action == page()->action(QWebEnginePage::Back) ||
        action == page()->action(QWebEnginePage::Forward) ||
        action == page()->action(QWebEnginePage::CopyImageUrlToClipboard)) {
      action->setVisible(false);
    }
  }

  QWebEngineContextMenuRequest *data = lastContextMenuRequest();
  Q_ASSERT(data);

  // allow context menu on image
  if (data->mediaType() == QWebEngineContextMenuRequest::MediaTypeImage) {
    QWebEngineView::contextMenuEvent(event);
    return;
  }
  // if content is not editable
  if (data->selectedText().isEmpty() && !data->isContentEditable()) {
    event->ignore();
    return;
  }

  auto pageWebengineProfile = page()->profile();
  const QStringList &languages = pageWebengineProfile->spellCheckLanguages();
  menu->addSeparator();
  auto *spellcheckAction = new QAction(tr("Check Spelling"), menu);
  spellcheckAction->setCheckable(true);
  spellcheckAction->setChecked(pageWebengineProfile->isSpellCheckEnabled());
  connect(spellcheckAction, &QAction::toggled, this,
          [pageWebengineProfile](bool toogled) {
            pageWebengineProfile->setSpellCheckEnabled(toogled);
            SettingsManager::instance().settings().setValue("sc_enabled",
                                                            toogled);
          });
  menu->addAction(spellcheckAction);

  if (pageWebengineProfile->isSpellCheckEnabled()) {
    auto subMenu = menu->addMenu(tr("Select Language"));
    for (const QString &dict : qAsConst(m_dictionaries)) {
      auto action = subMenu->addAction(dict);
      action->setCheckable(true);
      action->setChecked(languages.contains(dict));
      connect(
          action, &QAction::triggered, this, [pageWebengineProfile, dict]() {
            pageWebengineProfile->setSpellCheckLanguages(QStringList() << dict);
            SettingsManager::instance().settings().setValue("sc_dict", dict);
          });
    }
  }
  connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
  menu->popup(event->globalPos());
}

bool WebView::eventFilter(QObject *watched, QEvent *event) {
  Q_UNUSED(watched);
  if (event->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(event);
    if (ke->matches(QKeySequence::Paste)) {
      // Ignore auto-repeat (user holding Ctrl+V) — one injection is enough
      if (ke->isAutoRepeat())
        return true;
      // Debounce: skip if we already injected a paste recently.
      // The app-level filter may fire for multiple watched objects on the
      // same physical key press, and Chromium's renderer may also produce
      // delayed paste events.  2 s covers all of these.
      if (m_lastPasteInjectTime.isValid() &&
          m_lastPasteInjectTime.elapsed() < 2000)
        return true;
      QWidget *fw = QApplication::focusWidget();
      if (fw && (fw == this || isAncestorOf(fw)) && injectImagePaste()) {
        m_lastPasteInjectTime.start();
        return true; // consumed — image injected via JS
      }
    }
  }
  return QWebEngineView::eventFilter(watched, event);
}

bool WebView::injectImagePaste() {
  if (!page())
    return false;

  const QClipboard *clipboard = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();
  if (!mimeData)
    return false;

  // Try to get image from clipboard (multiple strategies)
  QImage image;
  if (mimeData->hasImage()) {
    image = qvariant_cast<QImage>(mimeData->imageData());
  }
  if (image.isNull()) {
    for (const char *fmt : {"image/png", "image/jpeg", "image/webp", "image/gif"}) {
      if (mimeData->hasFormat(QString::fromLatin1(fmt))) {
        QByteArray raw = mimeData->data(QString::fromLatin1(fmt));
        if (!raw.isEmpty() && image.loadFromData(raw))
          break;
      }
    }
  }
  if (image.isNull())
    return false;

  // Encode as PNG → base64
  QByteArray pngBytes;
  QBuffer buffer(&pngBytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  buffer.close();
  QString base64 = QString::fromLatin1(pngBytes.toBase64());

  // Inject synthetic ClipboardEvent with the image as a File blob.
  // WhatsApp Web's paste handler picks up the File from clipboardData
  // and opens the image preview / send modal.
  // ev._whatsieInjected marks our synthetic event so the JS paste handler
  // (in focus keeper) can distinguish it from native Chromium pastes.
  QString js = QString(R"JS(
    (function() {
      try {
        var b64 = '%1';
        var bin = atob(b64);
        var u8 = new Uint8Array(bin.length);
        for (var i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
        var blob = new Blob([u8], { type: 'image/png' });
        var file = new File([blob], 'image.png', { type: 'image/png' });
        var dt = new DataTransfer();
        dt.items.add(file);
        var ev = new ClipboardEvent('paste', {
          bubbles: true, cancelable: true, clipboardData: dt
        });
        ev._whatsieInjected = true;
        var el = document.querySelector(
                   '[contenteditable="true"][data-tab="10"]') ||
                 document.querySelector(
                   '[data-testid="conversation-compose-box-input"]') ||
                 document.activeElement;
        if (el) {
          el.focus();
          el.dispatchEvent(ev);
        }
      } catch(e) {
        console.error('[Whatsie] Image paste injection failed:', e);
      }
    })();
  )JS").arg(base64);

  page()->runJavaScript(js);
  return true;
}
