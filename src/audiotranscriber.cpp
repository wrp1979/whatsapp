#include "audiotranscriber.h"
#include "settingsmanager.h"

// whisper.cpp
#include <whisper.h>

// FFmpeg / libav for audio decoding
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <utility>

AudioTranscriber::AudioTranscriber(QObject *parent) : QObject(parent) {}

AudioTranscriber::~AudioTranscriber() {
  QMutexLocker lock(&m_ctxMutex);
  if (m_ctx) {
    whisper_free(m_ctx);
    m_ctx = nullptr;
  }
}

bool AudioTranscriber::isModelLoaded() const {
  QMutexLocker lock(&m_ctxMutex);
  return m_ctx != nullptr;
}

// static
QString AudioTranscriber::defaultModelPath() {
  // 1. Check settings
  const QString saved = SettingsManager::instance()
                            .settings()
                            .value("whisperModelPath")
                            .toString();
  if (!saved.isEmpty() && QFile::exists(saved))
    return saved;

  // 2. Auto-detect in common locations
  const QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  const QStringList candidates = {
      dataDir + "/models/ggml-large-v3-turbo.bin",
      QDir::homePath() + "/.local/share/whatsie/models/ggml-large-v3-turbo.bin",
      QDir::homePath() + "/.local/share/whatsie/models/ggml-large-v3.bin",
      QDir::homePath() + "/.local/share/whatsie/models/ggml-medium.bin",
      QDir::homePath() + "/.local/share/whatsie/models/ggml-small.bin",
      "/usr/local/share/whisper/models/ggml-large-v3-turbo.bin",
      "/usr/share/whisper/models/ggml-large-v3-turbo.bin",
  };
  for (const QString &c : candidates) {
    if (QFile::exists(c))
      return c;
  }
  return QString();
}

void AudioTranscriber::loadModel(const QString &modelPath) {
  QString path = modelPath;
  if (path.isEmpty())
    path = defaultModelPath();

  if (path.isEmpty()) {
    emit modelStateChanged(
        false,
        "Model file not found. Download a GGML model and set its path in "
        "Settings → Transcription.");
    return;
  }
  if (!QFile::exists(path)) {
    emit modelStateChanged(
        false,
        QString("Model file not found: %1").arg(path));
    return;
  }

  // Load model in a background thread (can take a few seconds on first load)
  QString pathCopy = path;
  QtConcurrent::run([this, pathCopy]() {
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu    = true;  // RTX 4090
    cparams.gpu_device = 0;
    cparams.flash_attn = true;  // Flash Attention – big speedup on RTX 4090

    struct whisper_context *ctx =
        whisper_init_from_file_with_params(pathCopy.toUtf8().constData(),
                                           cparams);
    {
      QMutexLocker lock(&m_ctxMutex);
      if (m_ctx) {
        whisper_free(m_ctx);
      }
      m_ctx = ctx;
    }

    if (ctx) {
      emit modelStateChanged(true,
                             QString("Model loaded: %1").arg(
                                 QFileInfo(pathCopy).fileName()));
    } else {
      emit modelStateChanged(
          false,
          QString("Failed to load model: %1").arg(pathCopy));
    }
  });
}

// ---------------------------------------------------------------------------
// Audio decoding: any format → mono f32 at 16 kHz using libavformat/libswresample
// ---------------------------------------------------------------------------
std::vector<float>
AudioTranscriber::decodeAudioToPcm(const QByteArray &data,
                                   QString &errorOut) const {
  // Write raw bytes to a temp file so libavformat can probe the format
  QTemporaryFile tmp;
  tmp.setFileTemplate(QDir::tempPath() + "/whatsie_audio_XXXXXX");
  if (!tmp.open()) {
    errorOut = "Failed to create temp audio file";
    return {};
  }
  tmp.write(data);
  tmp.flush();
  const std::string path = tmp.fileName().toStdString();

  // --- Open container ---
  AVFormatContext *fmtCtx = nullptr;
  if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) < 0) {
    errorOut = "Cannot open audio data";
    return {};
  }

  if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
    avformat_close_input(&fmtCtx);
    errorOut = "Cannot find stream info";
    return {};
  }

  // --- Find first audio stream ---
  int audioIdx = -1;
  const AVCodec *codec = nullptr;
  for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
    if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioIdx = (int)i;
      codec = avcodec_find_decoder(fmtCtx->streams[i]->codecpar->codec_id);
      break;
    }
  }
  if (audioIdx < 0 || !codec) {
    avformat_close_input(&fmtCtx);
    errorOut = "No audio stream found in message";
    return {};
  }

  // --- Open codec ---
  AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codecCtx,
                                 fmtCtx->streams[audioIdx]->codecpar);
  if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    errorOut = "Cannot open audio codec";
    return {};
  }

  // --- Set up resampler: input → 16 kHz mono f32 ---
  SwrContext *swrCtx = nullptr;
  AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_MONO;
  int ret = swr_alloc_set_opts2(
      &swrCtx,
      &outLayout,       AV_SAMPLE_FMT_FLT, 16000,  // output
      &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate,
      0, nullptr);
  if (ret < 0 || swr_init(swrCtx) < 0) {
    swr_free(&swrCtx);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    errorOut = "Failed to init audio resampler";
    return {};
  }

  // --- Decode and resample ---
  std::vector<float> result;
  result.reserve(16000 * 60); // pre-alloc for up to 60 s

  AVPacket *pkt  = av_packet_alloc();
  AVFrame  *frame = av_frame_alloc();

  while (av_read_frame(fmtCtx, pkt) >= 0) {
    if (pkt->stream_index != audioIdx) {
      av_packet_unref(pkt);
      continue;
    }
    if (avcodec_send_packet(codecCtx, pkt) == 0) {
      while (avcodec_receive_frame(codecCtx, frame) == 0) {
        const int64_t delay = swr_get_delay(swrCtx, codecCtx->sample_rate);
        const int outSamples = (int)av_rescale_rnd(
            delay + frame->nb_samples, 16000,
            codecCtx->sample_rate, AV_ROUND_UP);

        const size_t prevSize = result.size();
        result.resize(prevSize + (size_t)outSamples);
        uint8_t *outPtr = reinterpret_cast<uint8_t *>(result.data() + prevSize);

        const int converted = swr_convert(
            swrCtx, &outPtr, outSamples,
            const_cast<const uint8_t **>(frame->data), frame->nb_samples);

        if (converted < outSamples)
          result.resize(prevSize + (size_t)converted);

        av_frame_unref(frame);
      }
    }
    av_packet_unref(pkt);
  }

  // Flush remaining samples from resampler
  {
    const int64_t delay = swr_get_delay(swrCtx, codecCtx->sample_rate);
    if (delay > 0) {
      const int outSamples =
          (int)av_rescale_rnd(delay, 16000, codecCtx->sample_rate, AV_ROUND_UP);
      const size_t prevSize = result.size();
      result.resize(prevSize + (size_t)outSamples);
      uint8_t *outPtr =
          reinterpret_cast<uint8_t *>(result.data() + prevSize);
      const int converted = swr_convert(swrCtx, &outPtr, outSamples, nullptr, 0);
      if (converted < outSamples)
        result.resize(prevSize + (size_t)converted);
    }
  }

  swr_free(&swrCtx);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&codecCtx);
  avformat_close_input(&fmtCtx);

  return result;
}

// ---------------------------------------------------------------------------
// Whisper inference (called from background thread, m_ctxMutex must be locked)
// ---------------------------------------------------------------------------
QString
AudioTranscriber::runWhisperInference(const std::vector<float> &pcm) const {
  whisper_full_params params =
      whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

  const QByteArray langBytes = SettingsManager::instance()
                                  .settings()
                                  .value("whisperLanguage", "auto")
                                  .toString()
                                  .toUtf8();

  params.language        = langBytes.constData(); // must outlive whisper_full()
  params.n_threads       = 4;  // CPU threads for preprocessing
  params.translate       = false;
  params.no_timestamps   = true;
  params.single_segment  = false;
  params.print_progress  = false;
  params.print_special   = false;
  params.print_realtime  = false;
  params.print_timestamps = false;

  if (whisper_full(m_ctx, params, pcm.data(), (int)pcm.size()) != 0) {
    return QString();
  }

  QString result;
  const int n = whisper_full_n_segments(m_ctx);
  for (int i = 0; i < n; ++i) {
    result += QString::fromUtf8(whisper_full_get_segment_text(m_ctx, i));
  }
  return result.trimmed();
}

// ---------------------------------------------------------------------------
// Public slot: transcribe
// ---------------------------------------------------------------------------
void AudioTranscriber::transcribe(QByteArray audioData,
                                  const QString &messageId,
                                  const QString &mimeType) {
  Q_UNUSED(mimeType)

  // Auto-load model on first use
  if (!isModelLoaded()) {
    const QString path = defaultModelPath();
    if (path.isEmpty()) {
      emit transcriptionError(
          messageId,
          "Whisper model not found. Set the path in Settings → Transcription.");
      return;
    }
    // Synchronous load (blocks briefly; model load is one-time)
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu    = true;
    cparams.gpu_device = 0;
    cparams.flash_attn = true;

    struct whisper_context *ctx =
        whisper_init_from_file_with_params(path.toUtf8().constData(), cparams);

    QMutexLocker lock(&m_ctxMutex);
    m_ctx = ctx;

    if (!m_ctx) {
      emit transcriptionError(messageId, "Failed to load Whisper model.");
      return;
    }
    emit modelStateChanged(true, QString("Model loaded: %1")
                                     .arg(QFileInfo(path).fileName()));
  }

  // Dispatch decode + inference to thread pool
  QString msgId = messageId;

  QtConcurrent::run([this, data = std::move(audioData), msgId]() {
    QString decodeError;
    const std::vector<float> pcm = decodeAudioToPcm(data, decodeError);

    if (pcm.empty()) {
      emit transcriptionError(
          msgId,
          decodeError.isEmpty() ? "Audio decode failed" : decodeError);
      return;
    }

    // Lock context for exclusive whisper_full access
    QMutexLocker lock(&m_ctxMutex);
    if (!m_ctx) {
      emit transcriptionError(msgId, "Model unloaded during transcription");
      return;
    }

    const QString text = runWhisperInference(pcm);
    if (text.isEmpty()) {
      emit transcriptionError(msgId, "Whisper returned no text");
    } else {
      emit transcriptionReady(msgId, text);
    }
  });
}
