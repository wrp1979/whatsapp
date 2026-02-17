#ifndef NOTIFICATIONPOPUP_H
#define NOTIFICATIONPOPUP_H

#include "settingsmanager.h"

#include <QApplication>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QParallelAnimationGroup>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineNotification>

#include <memory>

// Posições possíveis para a notificação
enum class NotificationPosition {
  TopRight = 0,
  TopLeft = 1,
  BottomRight = 2,
  BottomLeft = 3
};

class NotificationPopup : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)

  QLabel *m_avatar;
  QLabel *m_title;
  QLabel *m_message;
  QProgressBar *m_progressBar;
  QWidget *m_contentWidget;
  QTimer *m_timer;
  QTimer *m_progressTimer;

  std::unique_ptr<QWebEngineNotification> notification;

  qreal m_opacity = 1.0;
  int m_duration = 9000;
  int m_elapsed = 0;
  bool m_hovered = false;
  bool m_mousePressed = false;
  NotificationPosition m_position = NotificationPosition::TopRight;

  // Constantes de design
  static constexpr int POPUP_WIDTH = 340;
  static constexpr int POPUP_HEIGHT = 90;
  static constexpr int AVATAR_SIZE = 52;
  static constexpr int BORDER_RADIUS = 14;
  static constexpr int SHADOW_RADIUS = 20;
  static constexpr int MARGIN = 16;

public:
  explicit NotificationPopup(QWidget *parent, bool deleteOnClose = true)
      : QWidget(parent) {
    // Window sem borda, sempre no topo, transparente
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                   Qt::ToolTip | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose, deleteOnClose);

    setFixedSize(POPUP_WIDTH + SHADOW_RADIUS * 2,
                 POPUP_HEIGHT + SHADOW_RADIUS * 2);

    // Carregar posição das settings
    int pos = SettingsManager::instance()
                  .settings()
                  .value("notificationPosition", 0)
                  .toInt();
    m_position = static_cast<NotificationPosition>(pos);

    setupUI();
    setupTimers();
  }

private:
  void setupUI() {
    // Container principal com margem para sombra
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(SHADOW_RADIUS, SHADOW_RADIUS,
                                   SHADOW_RADIUS, SHADOW_RADIUS);

    // Widget de conteúdo (onde desenhamos o fundo arredondado)
    m_contentWidget = new QWidget(this);
    m_contentWidget->setFixedSize(POPUP_WIDTH, POPUP_HEIGHT);
    m_contentWidget->setObjectName("notificationContent");
    mainLayout->addWidget(m_contentWidget);

    // Efeito de sombra
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(SHADOW_RADIUS);
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setOffset(0, 4);
    m_contentWidget->setGraphicsEffect(shadow);

    // Layout do conteúdo
    auto *contentLayout = new QHBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(MARGIN, MARGIN, MARGIN, MARGIN - 4);
    contentLayout->setSpacing(12);

    // Avatar circular
    m_avatar = new QLabel(m_contentWidget);
    m_avatar->setFixedSize(AVATAR_SIZE, AVATAR_SIZE);
    m_avatar->setAlignment(Qt::AlignCenter);
    m_avatar->setStyleSheet(QString(
      "QLabel {"
      "  background-color: rgba(255, 255, 255, 0.1);"
      "  border-radius: %1px;"
      "}"
    ).arg(AVATAR_SIZE / 2));
    contentLayout->addWidget(m_avatar, 0, Qt::AlignTop);

    // Container de texto
    auto *textContainer = new QWidget(m_contentWidget);
    auto *textLayout = new QVBoxLayout(textContainer);
    textLayout->setContentsMargins(0, 2, 0, 0);
    textLayout->setSpacing(4);

    // Título
    m_title = new QLabel(textContainer);
    m_title->setStyleSheet(
      "QLabel {"
      "  color: white;"
      "  font-size: 14px;"
      "  font-weight: 600;"
      "  background: transparent;"
      "}"
    );
    m_title->setWordWrap(false);
    textLayout->addWidget(m_title);

    // Mensagem
    m_message = new QLabel(textContainer);
    m_message->setStyleSheet(
      "QLabel {"
      "  color: rgba(255, 255, 255, 0.8);"
      "  font-size: 13px;"
      "  background: transparent;"
      "}"
    );
    m_message->setWordWrap(true);
    m_message->setMaximumHeight(40);
    textLayout->addWidget(m_message);

    textLayout->addStretch();
    contentLayout->addWidget(textContainer, 1);

    // Botao de fechar
    auto *closeBtn = new QPushButton(m_contentWidget);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
      "QPushButton {"
      "  background-color: transparent;"
      "  border: none;"
      "  border-radius: 12px;"
      "  color: rgba(255, 255, 255, 0.5);"
      "  font-size: 16px;"
      "  font-weight: bold;"
      "}"
      "QPushButton:hover {"
      "  background-color: rgba(255, 255, 255, 0.1);"
      "  color: rgba(255, 255, 255, 0.9);"
      "}"
      "QPushButton:pressed {"
      "  background-color: rgba(255, 255, 255, 0.15);"
      "}"
    );
    closeBtn->setText(QString::fromUtf8("\u00D7")); // Unicode multiplication sign (cleaner X)
    connect(closeBtn, &QPushButton::clicked, this, &NotificationPopup::onClosed);
    contentLayout->addWidget(closeBtn, 0, Qt::AlignTop);

    // Barra de progresso no fundo
    m_progressBar = new QProgressBar(m_contentWidget);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(3);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(100);
    m_progressBar->setStyleSheet(
      "QProgressBar {"
      "  background-color: rgba(255, 255, 255, 0.1);"
      "  border: none;"
      "  border-radius: 1px;"
      "}"
      "QProgressBar::chunk {"
      "  background-color: #25D366;"
      "  border-radius: 1px;"
      "}"
    );

    // Posicionar progress bar no fundo
    m_progressBar->setParent(m_contentWidget);
    m_progressBar->setGeometry(MARGIN, POPUP_HEIGHT - 8,
                                POPUP_WIDTH - MARGIN * 2, 3);
  }

  void setupTimers() {
    m_duration = SettingsManager::instance()
                     .settings()
                     .value("notificationTimeOut", 9000)
                     .toInt();

    // Timer principal para fechar
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &NotificationPopup::onClosed);

    // Timer para atualizar progress bar
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(50);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
      if (!m_hovered) {
        m_elapsed += 50;
        int progress = 100 - (m_elapsed * 100 / m_duration);
        m_progressBar->setValue(qMax(0, progress));
      }
    });
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Área do conteúdo (sem sombra)
    QRect contentRect(SHADOW_RADIUS, SHADOW_RADIUS, POPUP_WIDTH, POPUP_HEIGHT);

    // Fundo com gradiente sutil
    QPainterPath path;
    path.addRoundedRect(contentRect, BORDER_RADIUS, BORDER_RADIUS);

    // Gradiente escuro elegante
    QLinearGradient gradient(contentRect.topLeft(), contentRect.bottomRight());
    gradient.setColorAt(0, QColor(40, 44, 52, 245));
    gradient.setColorAt(1, QColor(30, 33, 40, 250));

    painter.fillPath(path, gradient);

    // Borda sutil
    painter.setPen(QPen(QColor(255, 255, 255, 20), 1));
    painter.drawPath(path);

    // Linha de destaque no topo (WhatsApp green)
    QPainterPath topLine;
    topLine.moveTo(contentRect.left() + BORDER_RADIUS, contentRect.top());
    topLine.lineTo(contentRect.right() - BORDER_RADIUS, contentRect.top());
    painter.setPen(QPen(QColor(37, 211, 102), 2));
    painter.drawPath(topLine);
  }

  void enterEvent(QEnterEvent *) override {
    m_hovered = true;
    m_timer->stop();

    // Efeito de hover - aumentar opacidade da borda
    update();
  }

  void leaveEvent(QEvent *) override {
    m_hovered = false;

    // Retomar timer com tempo restante
    int remaining = m_duration - m_elapsed;
    if (remaining > 0) {
      m_timer->start(remaining);
    } else {
      onClosed();
    }

    update();
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      m_mousePressed = rect().contains(event->pos());
    }
    QWidget::mousePressEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    QWidget::mouseReleaseEvent(event);
    const bool shouldHandleClick =
        event->button() == Qt::LeftButton && m_mousePressed &&
        rect().contains(event->pos());
    m_mousePressed = false;
    if (shouldHandleClick) {
      emit notification_clicked();
      if (notification)
        notification->click();
      onClosed();
    }
  }

  qreal popupOpacity() const { return m_opacity; }

  void setPopupOpacity(qreal opacity) {
    m_opacity = opacity;
    setWindowOpacity(opacity);
  }

public:
  void present(QScreen *screen, QString title, QString message,
               const QPixmap image) {
    m_title->setText(elidedText(title, m_title->font(), POPUP_WIDTH - AVATAR_SIZE - MARGIN * 3));
    m_message->setText(message);
    setAvatar(image);

    m_elapsed = 0;
    m_progressBar->setValue(100);
    m_timer->start(m_duration);
    m_progressTimer->start();

    animateIn(screen);
  }

  void present(QScreen *screen,
               std::unique_ptr<QWebEngineNotification> &newNotification) {
    if (notification) {
      notification->close();
      notification.reset();
    }

    notification.swap(newNotification);

    m_title->setText(elidedText(notification->title(), m_title->font(),
                                POPUP_WIDTH - AVATAR_SIZE - MARGIN * 3));
    m_message->setText(notification->message());
    setAvatar(QPixmap::fromImage(notification->icon()));

    notification->show();


    m_elapsed = 0;
    m_progressBar->setValue(100);
    m_timer->start(m_duration);
    m_progressTimer->start();

    animateIn(screen);
  }

  void setPosition(NotificationPosition pos) {
    m_position = pos;
    SettingsManager::instance().settings().setValue("notificationPosition",
                                                     static_cast<int>(pos));
  }

private:
  void setAvatar(const QPixmap &pixmap) {
    if (pixmap.isNull()) {
      m_avatar->clear();
      return;
    }

    // Criar avatar circular
    QPixmap scaled = pixmap.scaled(AVATAR_SIZE, AVATAR_SIZE,
                                   Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);

    QPixmap circular(AVATAR_SIZE, AVATAR_SIZE);
    circular.fill(Qt::transparent);

    QPainter painter(&circular);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addEllipse(0, 0, AVATAR_SIZE, AVATAR_SIZE);
    painter.setClipPath(path);

    // Centralizar a imagem
    int x = (AVATAR_SIZE - scaled.width()) / 2;
    int y = (AVATAR_SIZE - scaled.height()) / 2;
    painter.drawPixmap(x, y, scaled);

    m_avatar->setPixmap(circular);
  }

  QString elidedText(const QString &text, const QFont &font, int maxWidth) {
    QFontMetrics metrics(font);
    return metrics.elidedText(text, Qt::ElideRight, maxWidth);
  }

  QPoint calculatePosition(QScreen *screen) {
    if (!screen) return QPoint(0, 0);

    QRect screenRect = screen->availableGeometry();
    int padding = 20;
    int x = 0, y = 0;

    switch (m_position) {
      case NotificationPosition::TopLeft:
        x = screenRect.x() + padding;
        y = screenRect.y() + padding;
        break;
      case NotificationPosition::TopRight:
      default:
        x = screenRect.x() + screenRect.width() - width() - padding + SHADOW_RADIUS;
        y = screenRect.y() + padding;
        break;
      case NotificationPosition::BottomLeft:
        x = screenRect.x() + padding;
        y = screenRect.y() + screenRect.height() - height() - padding + SHADOW_RADIUS;
        break;
      case NotificationPosition::BottomRight:
        x = screenRect.x() + screenRect.width() - width() - padding + SHADOW_RADIUS;
        y = screenRect.y() + screenRect.height() - height() - padding + SHADOW_RADIUS;
        break;
    }

    return QPoint(x, y);
  }

  QPoint getSlideStartOffset() {
    int offset = 30;
    switch (m_position) {
      case NotificationPosition::TopLeft:
        return QPoint(-offset, 0);
      case NotificationPosition::TopRight:
        return QPoint(offset, 0);
      case NotificationPosition::BottomLeft:
        return QPoint(-offset, 0);
      case NotificationPosition::BottomRight:
        return QPoint(offset, 0);
    }
    return QPoint(offset, 0);
  }

protected slots:
  void animateIn(QScreen *screen) {
    if (!screen) return;

    QPoint finalPos = calculatePosition(screen);
    QPoint startPos = finalPos + getSlideStartOffset();

    // Posição inicial
    move(startPos);
    setWindowOpacity(0);
    show();

    // Animação paralela: fade + slide
    auto *group = new QParallelAnimationGroup(this);

    // Fade in
    auto *fadeIn = new QPropertyAnimation(this, "popupOpacity");
    fadeIn->setDuration(200);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(fadeIn);

    // Slide in
    auto *slideIn = new QPropertyAnimation(this, "pos");
    slideIn->setDuration(250);
    slideIn->setStartValue(startPos);
    slideIn->setEndValue(finalPos);
    slideIn->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(slideIn);

    group->start(QPropertyAnimation::DeleteWhenStopped);
  }

  void onClosed() {
    m_timer->stop();
    m_progressTimer->stop();

    QPoint currentPos = pos();
    QPoint endPos = currentPos + getSlideStartOffset();

    // Animação de saída
    auto *group = new QParallelAnimationGroup(this);

    // Fade out
    auto *fadeOut = new QPropertyAnimation(this, "popupOpacity");
    fadeOut->setDuration(150);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InCubic);
    group->addAnimation(fadeOut);

    // Slide out
    auto *slideOut = new QPropertyAnimation(this, "pos");
    slideOut->setDuration(150);
    slideOut->setStartValue(currentPos);
    slideOut->setEndValue(endPos);
    slideOut->setEasingCurve(QEasingCurve::InCubic);
    group->addAnimation(slideOut);

    connect(group, &QParallelAnimationGroup::finished, this, [this]() {
      if (notification) {
        notification->close();
        notification.reset();
      }
      close();
    });

    group->start(QPropertyAnimation::DeleteWhenStopped);
  }

signals:
  void notification_clicked();
};

#endif // NOTIFICATIONPOPUP_H
