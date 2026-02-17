#ifndef TRANSCRIBERBRIDGE_H
#define TRANSCRIBERBRIDGE_H

#include "audiotranscriber.h"

#include <QObject>
#include <QString>

// Exposed to JavaScript via QWebChannel as "transcriber"
class TranscriberBridge : public QObject {
  Q_OBJECT
public:
  explicit TranscriberBridge(AudioTranscriber *transcriber,
                              QObject *parent = nullptr);

  // Called from JavaScript via QWebChannel
  Q_INVOKABLE void requestTranscription(const QString &base64Audio,
                                        const QString &messageId,
                                        const QString &mimeType);

signals:
  // Emitted back to JavaScript via QWebChannel
  void transcriptionReady(const QString &messageId, const QString &text);
  void transcriptionError(const QString &messageId,
                          const QString &errorText);

private:
  AudioTranscriber *m_transcriber;
};

#endif // TRANSCRIBERBRIDGE_H
