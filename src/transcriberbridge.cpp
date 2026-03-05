#include "transcriberbridge.h"

#include <utility>

TranscriberBridge::TranscriberBridge(AudioTranscriber *transcriber,
                                     QObject *parent)
    : QObject(parent), m_transcriber(transcriber) {
  connect(transcriber, &AudioTranscriber::transcriptionReady, this,
          &TranscriberBridge::transcriptionReady);
  connect(transcriber, &AudioTranscriber::transcriptionError, this,
          &TranscriberBridge::transcriptionError);
}

void TranscriberBridge::requestTranscription(const QString &base64Audio,
                                             const QString &messageId,
                                             const QString &mimeType) {
  QByteArray audioData = QByteArray::fromBase64(base64Audio.toUtf8());
  m_transcriber->transcribe(std::move(audioData), messageId, mimeType);
}
