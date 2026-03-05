#ifndef AUDIOTRANSCRIBER_H
#define AUDIOTRANSCRIBER_H

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <vector>

struct whisper_context;

class AudioTranscriber : public QObject {
  Q_OBJECT
public:
  explicit AudioTranscriber(QObject *parent = nullptr);
  ~AudioTranscriber();

  bool isModelLoaded() const;

  // Loads the model asynchronously; emits modelStateChanged when done.
  // If modelPath is empty, reads from QSettings "whisperModelPath".
  void loadModel(const QString &modelPath = QString());

  // Returns the default model path to show in UI
  static QString defaultModelPath();

public slots:
  void transcribe(QByteArray audioData, const QString &messageId,
                  const QString &mimeType);

signals:
  void transcriptionReady(const QString &messageId, const QString &text);
  void transcriptionError(const QString &messageId, const QString &errorText);
  // Fired after loadModel() completes (success or failure)
  void modelStateChanged(bool loaded, const QString &message);

private:
  struct whisper_context *m_ctx = nullptr;
  mutable QMutex m_ctxMutex;

  // Decode any audio format to mono f32 PCM at 16 kHz using libavformat.
  // Returns empty vector on error and fills errorOut.
  std::vector<float> decodeAudioToPcm(const QByteArray &data,
                                      QString &errorOut) const;

  // Run whisper inference on already-decoded PCM.
  // Must be called with m_ctxMutex held (use tryLock/lock externally).
  QString runWhisperInference(const std::vector<float> &pcm) const;
};

#endif // AUDIOTRANSCRIBER_H
