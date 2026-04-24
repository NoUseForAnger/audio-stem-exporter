// Audio Stem Exporter for OBS — mp3-writer-filter.cpp
// Records any OBS audio source directly to MP3, WAV, or AIFF in a
// low-latency background thread with lock-free SPSC ring buffer.

#include "mp3-writer-filter.hpp"

#include <obs-module.h>
#ifdef MW_ENABLE_FRONTEND
#include <obs-frontend-api.h>
#endif
#ifdef MW_ENABLE_QT
#include "mw-dock.hpp"
#endif
#include <media-io/audio-io.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

// ─── Settings keys ────────────────────────────────────────────────────────────
#define S_FOLDER        "folder"
#define S_FILENAME_FMT  "filename_fmt"
#define S_FORMAT        "format"
#define S_BITRATE       "bitrate"
#define S_TRIGGER       "trigger"
#define S_STATUS        "status_display"

#define FORMAT_WAV      "wav"
#define FORMAT_MP3      "mp3"
#define FORMAT_AIFF     "aiff"

#define TRIGGER_MANUAL    "manual"
#define TRIGGER_RECORDING "recording"
#define TRIGGER_STREAMING "streaming"
#define TRIGGER_BOTH      "both"

#define T_(k)           obs_module_text(k)
#define T_NAME          T_("Mp3WriterFilter.Name")
#define T_FOLDER        T_("Mp3WriterFilter.Folder")
#define T_FILENAME_FMT  T_("Mp3WriterFilter.FilenameFormat")
#define T_FORMAT        T_("Mp3WriterFilter.Format")
#define T_BITRATE       T_("Mp3WriterFilter.Bitrate")
#define T_TRIGGER       T_("Mp3WriterFilter.Trigger")
#define T_STATUS        T_("Mp3WriterFilter.Status")
#define T_START         T_("Mp3WriterFilter.Start")
#define T_STOP          T_("Mp3WriterFilter.Stop")

// ─────────────────────────────────────────────────────────────────────────────
// Global auto-trigger registry
// ─────────────────────────────────────────────────────────────────────────────

static std::mutex            g_registry_mtx;
static std::vector<MwFilter*> g_registry;

static void registry_add(MwFilter *f)
{
	std::lock_guard<std::mutex> lk(g_registry_mtx);
	g_registry.push_back(f);
}

static void registry_remove(MwFilter *f)
{
	std::lock_guard<std::mutex> lk(g_registry_mtx);
	g_registry.erase(std::remove(g_registry.begin(), g_registry.end(), f),
			 g_registry.end());
}

// Forward declarations needed by registry callbacks
static void start_recording(MwFilter *f);
static void stop_recording(MwFilter *f);

static void registry_on_event(bool is_streaming, bool started)
{
	std::lock_guard<std::mutex> lk(g_registry_mtx);
	for (MwFilter *f : g_registry) {
		bool relevant =
			(f->trigger == MwTrigger::Both) ||
			(is_streaming  && f->trigger == MwTrigger::Streaming) ||
			(!is_streaming && f->trigger == MwTrigger::Recording);
		if (!relevant)
			continue;

		if (started) {
			// Ref-count: start file only on first trigger
			if (f->trigger_count.fetch_add(1) == 0)
				start_recording(f);
		} else {
			// Stop file only when all triggers have stopped
			if (f->trigger_count.fetch_sub(1) == 1)
				stop_recording(f);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Public registry helpers (used by dock)
// ─────────────────────────────────────────────────────────────────────────────

void mw_registry_snapshot(std::vector<MwFilter*> &out)
{
	std::lock_guard<std::mutex> lk(g_registry_mtx);
	out = g_registry;
}

void mw_start_all()
{
	// Snapshot under lock, then operate without holding the lock
	// (start_recording may join a thread — blocking with lock held risks deadlock)
	std::vector<MwFilter*> snapshot;
	{ std::lock_guard<std::mutex> lk(g_registry_mtx); snapshot = g_registry; }
	for (MwFilter *f : snapshot)
		start_recording(f);
}

void mw_stop_all()
{
	std::vector<MwFilter*> snapshot;
	{ std::lock_guard<std::mutex> lk(g_registry_mtx); snapshot = g_registry; }
	for (MwFilter *f : snapshot)
		stop_recording(f);
}

void mw_start_recording_one(MwFilter *f)
{
	start_recording(f);
	obs_source_update_properties(f->context);
}

void mw_stop_recording_one(MwFilter *f)
{
	stop_recording(f);
	obs_source_update_properties(f->context);
}

// ─────────────────────────────────────────────────────────────────────────────
// Filename helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string sanitize_for_path(const std::string &s)
{
	std::string r = s;
	for (char &c : r)
		if (c == '\\' || c == '/' || c == ':' || c == '*' ||
		    c == '?'  || c == '"' || c == '<' || c == '>' || c == '|')
			c = '_';
	return r;
}

static void replace_token(std::string &s, const char *token,
			   const std::string &val)
{
	size_t pos = 0;
	while ((pos = s.find(token, pos)) != std::string::npos) {
		s.replace(pos, strlen(token), val);
		pos += val.size();
	}
}

static const char *format_ext(MwFormat fmt)
{
	switch (fmt) {
	case MwFormat::MP3:  return "mp3";
	case MwFormat::AIFF: return "aiff";
	default:             return "wav";
	}
}

static std::string build_output_path(MwFilter *f)
{
	auto now = std::chrono::system_clock::now();
	time_t t = std::chrono::system_clock::to_time_t(now);
	struct tm tm_buf = {};
#ifdef _WIN32
	localtime_s(&tm_buf, &t);
#else
	localtime_r(&t, &tm_buf);
#endif
	char date[16], dateeu[16], timebuf[16];
	strftime(date,    sizeof(date),    "%Y-%m-%d", &tm_buf);
	strftime(dateeu,  sizeof(dateeu),  "%y%m%d",   &tm_buf);
	strftime(timebuf, sizeof(timebuf), "%H-%M-%S", &tm_buf);

	obs_source_t *parent_src = obs_filter_get_parent(f->context);
	const char *src = parent_src ? obs_source_get_name(parent_src)
	                              : obs_source_get_name(f->context);
	std::string result = f->filename_fmt;
	replace_token(result, "%SRC%",    sanitize_for_path(src ? src : "source"));
	replace_token(result, "%DATE%",   date);
	replace_token(result, "%DATEEU%", dateeu);
	replace_token(result, "%TIME%",   timebuf);

	// Block path traversal — ensure final path stays inside the chosen folder
	namespace fs = std::filesystem;
	fs::path safe_folder = fs::weakly_canonical(fs::u8path(f->folder));
	fs::path candidate   = fs::weakly_canonical(safe_folder / fs::u8path(result));
	if (candidate.string().rfind(safe_folder.string(), 0) != 0) {
		blog(LOG_ERROR, "[obs-mp3-writer] Path traversal blocked in filename format");
		return "";
	}

	std::string base = (safe_folder / fs::u8path(result)).string();
	std::string path = base + "." + format_ext(f->format);
	// Cap collision counter to prevent infinite loop / int overflow
	for (int i = 1; i <= 9999 && std::filesystem::exists(path); ++i)
		path = base + "_" + std::to_string(i) + "." + format_ext(f->format);
	if (std::filesystem::exists(path)) {
		blog(LOG_ERROR, "[obs-mp3-writer] Could not find unused filename after 9999 attempts");
		return "";
	}
	return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// FFmpeg pipeline helpers
// ─────────────────────────────────────────────────────────────────────────────

static bool setup_swr(MwFilter *f, int channels, int sample_rate,
		      AVSampleFormat out_fmt)
{
	AVChannelLayout layout = {};
	av_channel_layout_default(&layout, channels);
	int ret = swr_alloc_set_opts2(&f->swr_ctx,
				      &layout, out_fmt,       sample_rate,
				      &layout, AV_SAMPLE_FMT_FLTP, sample_rate,
				      0, nullptr);
	av_channel_layout_uninit(&layout);
	if (ret < 0 || !f->swr_ctx) {
		blog(LOG_ERROR, "[obs-mp3-writer] swr_alloc_set_opts2 failed (%d)", ret);
		return false;
	}
	ret = swr_init(f->swr_ctx);
	if (ret < 0) {
		blog(LOG_ERROR, "[obs-mp3-writer] swr_init failed (%d)", ret);
		return false;
	}
	return true;
}

// PCM encoder — used for both WAV (S16LE) and AIFF (S16BE)
static bool ffmpeg_open_pcm(MwFilter *f, const std::string &path,
			     int sample_rate, int channels,
			     const char *muxer, AVCodecID codec_id)
{
	int ret = avformat_alloc_output_context2(&f->fmt_ctx, nullptr,
						 muxer, path.c_str());
	if (ret < 0 || !f->fmt_ctx) {
		blog(LOG_ERROR, "[obs-mp3-writer] avformat_alloc_output_context2 failed (%d)", ret);
		return false;
	}

	const AVCodec *codec = avcodec_find_encoder(codec_id);
	if (!codec) {
		blog(LOG_ERROR, "[obs-mp3-writer] PCM encoder not found");
		return false;
	}

	f->stream    = avformat_new_stream(f->fmt_ctx, nullptr);
	f->codec_ctx = avcodec_alloc_context3(codec);
	if (!f->stream || !f->codec_ctx)
		return false;

	AVChannelLayout chl = {};
	av_channel_layout_default(&chl, channels);
	av_channel_layout_copy(&f->codec_ctx->ch_layout, &chl);
	av_channel_layout_uninit(&chl);

	f->codec_ctx->sample_fmt  = AV_SAMPLE_FMT_S16;
	f->codec_ctx->sample_rate = sample_rate;
	f->codec_ctx->time_base   = {1, sample_rate};
	if (f->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		f->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	ret = avcodec_open2(f->codec_ctx, codec, nullptr);
	if (ret < 0) {
		blog(LOG_ERROR, "[obs-mp3-writer] avcodec_open2 failed (%d)", ret);
		return false;
	}
	if (avcodec_parameters_from_context(f->stream->codecpar, f->codec_ctx) < 0) {
		blog(LOG_ERROR, "[obs-mp3-writer] avcodec_parameters_from_context failed");
		return false;
	}
	f->stream->time_base = f->codec_ctx->time_base;

	if (!setup_swr(f, channels, sample_rate, AV_SAMPLE_FMT_S16))
		return false;

	f->frame  = av_frame_alloc();
	f->packet = av_packet_alloc();
	if (!f->frame || !f->packet)
		return false;

	if (!(f->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&f->fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			blog(LOG_ERROR, "[obs-mp3-writer] avio_open failed (%d)", ret);
			return false;
		}
	}
	ret = avformat_write_header(f->fmt_ctx, nullptr);
	if (ret < 0) {
		blog(LOG_ERROR, "[obs-mp3-writer] avformat_write_header failed (%d)", ret);
		return false;
	}
	f->pts = 0;
	return true;
}

// MP3 encoder via libmp3lame
static bool ffmpeg_open_mp3(MwFilter *f, const std::string &path,
			     int sample_rate, int channels)
{
	int ret = avformat_alloc_output_context2(&f->fmt_ctx, nullptr,
						 "mp3", path.c_str());
	if (ret < 0 || !f->fmt_ctx) {
		blog(LOG_ERROR, "[obs-mp3-writer] mp3 context failed (%d)", ret);
		return false;
	}

	const AVCodec *codec = avcodec_find_encoder_by_name("libmp3lame");
	if (!codec) {
		blog(LOG_ERROR,
		     "[obs-mp3-writer] libmp3lame not found — "
		     "is FFmpeg compiled with --enable-libmp3lame?");
		return false;
	}

	f->stream    = avformat_new_stream(f->fmt_ctx, nullptr);
	f->codec_ctx = avcodec_alloc_context3(codec);
	if (!f->stream || !f->codec_ctx)
		return false;

	AVChannelLayout chl = {};
	av_channel_layout_default(&chl, channels);
	av_channel_layout_copy(&f->codec_ctx->ch_layout, &chl);
	av_channel_layout_uninit(&chl);

	// libmp3lame native sample format is S16P (planar 16-bit)
	f->codec_ctx->sample_fmt  = AV_SAMPLE_FMT_S16P;
	f->codec_ctx->sample_rate = sample_rate;
	f->codec_ctx->bit_rate    = (int64_t)f->bitrate * 1000;
	f->codec_ctx->time_base   = {1, sample_rate};
	if (f->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		f->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	ret = avcodec_open2(f->codec_ctx, codec, nullptr);
	if (ret < 0) {
		blog(LOG_ERROR, "[obs-mp3-writer] avcodec_open2 (mp3) failed (%d)", ret);
		return false;
	}
	blog(LOG_INFO, "[obs-mp3-writer] MP3 encoder ready (%lld kbps)",
	     (long long)(f->codec_ctx->bit_rate / 1000));
	if (avcodec_parameters_from_context(f->stream->codecpar, f->codec_ctx) < 0) {
		blog(LOG_ERROR, "[obs-mp3-writer] avcodec_parameters_from_context (mp3) failed");
		return false;
	}
	f->stream->time_base = f->codec_ctx->time_base;

	// FLTP → S16P (planar float → planar 16-bit; no layout change needed)
	if (!setup_swr(f, channels, sample_rate, AV_SAMPLE_FMT_S16P))
		return false;

	// libmp3lame needs exactly frame_size (1152) samples per send_frame call.
	// Buffer incoming chunks in a FIFO and drain in frame_size batches.
	f->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16P, channels,
				      f->codec_ctx->frame_size * 4);
	if (!f->fifo) {
		blog(LOG_ERROR, "[obs-mp3-writer] av_audio_fifo_alloc failed");
		return false;
	}

	f->frame  = av_frame_alloc();
	f->packet = av_packet_alloc();
	if (!f->frame || !f->packet)
		return false;

	if (!(f->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&f->fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			blog(LOG_ERROR, "[obs-mp3-writer] avio_open (mp3) failed (%d)", ret);
			return false;
		}
	}
	ret = avformat_write_header(f->fmt_ctx, nullptr);
	if (ret < 0) {
		blog(LOG_ERROR, "[obs-mp3-writer] avformat_write_header (mp3) failed (%d)", ret);
		return false;
	}
	f->pts = 0;
	return true;
}

// Format dispatcher
static bool ffmpeg_open(MwFilter *f, const std::string &path,
			int sample_rate, int channels)
{
	switch (f->format) {
	case MwFormat::WAV:
		return ffmpeg_open_pcm(f, path, sample_rate, channels,
				       "wav", AV_CODEC_ID_PCM_S16LE);
	case MwFormat::AIFF:
		return ffmpeg_open_pcm(f, path, sample_rate, channels,
				       "aiff", AV_CODEC_ID_PCM_S16BE);
	case MwFormat::MP3:
		return ffmpeg_open_mp3(f, path, sample_rate, channels);
	}
	return false;
}

// Flush, write trailer, free all FFmpeg resources
static void ffmpeg_close(MwFilter *f)
{
	if (!f->fmt_ctx)
		return;

	if (f->codec_ctx && f->frame && f->packet) {
		// Drain any samples still sitting in the FIFO (MP3 path)
		if (f->fifo) {
			int remaining = av_audio_fifo_size(f->fifo);
			if (remaining > 0) {
				av_frame_unref(f->frame);
				f->frame->format      = f->codec_ctx->sample_fmt;
				f->frame->sample_rate = f->codec_ctx->sample_rate;
				f->frame->nb_samples  = remaining;
				f->frame->pts         = f->pts;
				f->pts               += remaining;

				AVChannelLayout chl = {};
				av_channel_layout_default(&chl,
					f->codec_ctx->ch_layout.nb_channels);
				av_channel_layout_copy(&f->frame->ch_layout, &chl);
				av_channel_layout_uninit(&chl);

				if (av_frame_get_buffer(f->frame, 0) == 0 &&
				    av_audio_fifo_read(f->fifo,
						      (void **)f->frame->data,
						      remaining) == remaining) {
					if (avcodec_send_frame(f->codec_ctx, f->frame) == 0) {
						while (avcodec_receive_packet(
							       f->codec_ctx, f->packet) == 0) {
							av_packet_rescale_ts(
								f->packet,
								f->codec_ctx->time_base,
								f->stream->time_base);
							f->packet->stream_index =
								f->stream->index;
							if (av_interleaved_write_frame(
								f->fmt_ctx, f->packet) < 0)
								blog(LOG_ERROR, "[obs-mp3-writer] write_frame failed at close");
							av_packet_unref(f->packet);
						}
					}
				}
			}
			av_audio_fifo_free(f->fifo);
			f->fifo = nullptr;
		}

		// Flush the codec
		avcodec_send_frame(f->codec_ctx, nullptr);
		while (avcodec_receive_packet(f->codec_ctx, f->packet) == 0) {
			av_packet_rescale_ts(f->packet,
					     f->codec_ctx->time_base,
					     f->stream->time_base);
			f->packet->stream_index = f->stream->index;
			av_interleaved_write_frame(f->fmt_ctx, f->packet);
			av_packet_unref(f->packet);
		}
	}
	av_write_trailer(f->fmt_ctx);
	if (!(f->fmt_ctx->oformat->flags & AVFMT_NOFILE))
		avio_closep(&f->fmt_ctx->pb);

	avformat_free_context(f->fmt_ctx); f->fmt_ctx  = nullptr;
	f->stream = nullptr;
	if (f->codec_ctx) { avcodec_free_context(&f->codec_ctx); }
	if (f->swr_ctx)   { swr_free(&f->swr_ctx); }
	if (f->frame)     { av_frame_free(&f->frame); }
	if (f->packet)    { av_packet_free(&f->packet); }
	if (f->fifo)      { av_audio_fifo_free(f->fifo); f->fifo = nullptr; }
	f->pts = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Encode one chunk  (worker thread only)
// ─────────────────────────────────────────────────────────────────────────────

// Write packets from the codec to the file.
// Returns true if at least one packet was written.
static bool flush_codec_packets(MwFilter *f)
{
	bool wrote = false;
	while (avcodec_receive_packet(f->codec_ctx, f->packet) == 0) {
		av_packet_rescale_ts(f->packet,
				     f->codec_ctx->time_base,
				     f->stream->time_base);
		f->packet->stream_index = f->stream->index;
		int wr = av_interleaved_write_frame(f->fmt_ctx, f->packet);
		if (wr < 0)
			blog(LOG_ERROR,
			     "[obs-mp3-writer] av_interleaved_write_frame failed (%d)", wr);
		av_packet_unref(f->packet);
		wrote = true;
	}
	if (wrote && f->fmt_ctx->pb)
		avio_flush(f->fmt_ctx->pb);
	return wrote;
}

static void encode_chunk(MwFilter *f, const MwChunk &chunk)
{
	const uint8_t *in_data[MW_MAX_CHANNELS] = {};
	const int ch = std::min(chunk.channels, MW_MAX_CHANNELS);
	for (int c = 0; c < ch; ++c)
		in_data[c] = reinterpret_cast<const uint8_t *>(chunk.data[c]);

	const int frame_size = f->codec_ctx->frame_size;

	if (frame_size == 0) {
		// ── PCM path (WAV / AIFF): direct encode, any number of samples ──
		av_frame_unref(f->frame);
		f->frame->format      = f->codec_ctx->sample_fmt;
		f->frame->sample_rate = chunk.sample_rate;
		f->frame->nb_samples  = (int)chunk.frames;
		f->frame->pts         = f->pts;
		f->pts               += chunk.frames;

		AVChannelLayout chl = {};
		av_channel_layout_default(&chl, ch);
		av_channel_layout_copy(&f->frame->ch_layout, &chl);
		av_channel_layout_uninit(&chl);

		if (av_frame_get_buffer(f->frame, 0) < 0) {
			blog(LOG_ERROR, "[obs-mp3-writer] av_frame_get_buffer failed");
			return;
		}

		int converted = swr_convert(f->swr_ctx,
					    f->frame->data,  (int)chunk.frames,
					    in_data,         (int)chunk.frames);
		if (converted < 0) {
			blog(LOG_ERROR, "[obs-mp3-writer] swr_convert failed (%d)", converted);
			return;
		}

		int send_ret = avcodec_send_frame(f->codec_ctx, f->frame);
		if (send_ret < 0) {
			char errbuf[64] = {};
			av_strerror(send_ret, errbuf, sizeof(errbuf));
			blog(LOG_ERROR, "[obs-mp3-writer] avcodec_send_frame failed: %s (%d)",
			     errbuf, send_ret);
			return;
		}
		flush_codec_packets(f);

	} else {
		// ── MP3 path: buffer through FIFO, drain in frame_size batches ──
		//
		// libmp3lame requires exactly frame_size (1152) samples per call.
		// OBS delivers 1024 per callback, so we accumulate in an AVAudioFifo
		// and only send to the codec when a full frame is available.

		// Step 1: convert FLTP → S16P into a temporary frame
		AVFrame *tmp = av_frame_alloc();
		if (!tmp) return;

		tmp->format      = f->codec_ctx->sample_fmt;
		tmp->sample_rate = chunk.sample_rate;
		tmp->nb_samples  = (int)chunk.frames;

		AVChannelLayout chl = {};
		av_channel_layout_default(&chl, ch);
		av_channel_layout_copy(&tmp->ch_layout, &chl);
		av_channel_layout_uninit(&chl);

		if (av_frame_get_buffer(tmp, 0) < 0) {
			blog(LOG_ERROR, "[obs-mp3-writer] av_frame_get_buffer (tmp) failed");
			av_frame_free(&tmp);
			return;
		}

		int converted = swr_convert(f->swr_ctx,
					    tmp->data,        (int)chunk.frames,
					    in_data,          (int)chunk.frames);
		if (converted < 0) {
			blog(LOG_ERROR, "[obs-mp3-writer] swr_convert failed (%d)", converted);
			av_frame_free(&tmp);
			return;
		}

		// Step 2: push converted samples into the FIFO
		av_audio_fifo_write(f->fifo, (void **)tmp->data, converted);
		av_frame_free(&tmp);

		// Step 3: drain FIFO in exact frame_size chunks
		while (av_audio_fifo_size(f->fifo) >= frame_size) {
			av_frame_unref(f->frame);
			f->frame->format      = f->codec_ctx->sample_fmt;
			f->frame->sample_rate = chunk.sample_rate;
			f->frame->nb_samples  = frame_size;
			f->frame->pts         = f->pts;
			f->pts               += frame_size;

			AVChannelLayout chl2 = {};
			av_channel_layout_default(&chl2, ch);
			av_channel_layout_copy(&f->frame->ch_layout, &chl2);
			av_channel_layout_uninit(&chl2);

			if (av_frame_get_buffer(f->frame, 0) < 0) break;

			if (av_audio_fifo_read(f->fifo, (void **)f->frame->data,
					       frame_size) < frame_size)
				break;

			int send_ret = avcodec_send_frame(f->codec_ctx, f->frame);
			if (send_ret < 0) {
				char errbuf[64] = {};
				av_strerror(send_ret, errbuf, sizeof(errbuf));
				blog(LOG_ERROR,
				     "[obs-mp3-writer] avcodec_send_frame failed: %s (%d)",
				     errbuf, send_ret);
				break;
			}
			flush_codec_packets(f);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Worker thread
// ─────────────────────────────────────────────────────────────────────────────

static void update_status(MwFilter *f)
{
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - f->start_time).count();
	int m = (int)(elapsed / 60);
	int s = (int)(elapsed % 60);
	char buf[32];
	snprintf(buf, sizeof(buf), "● REC  %02d:%02d", m, s);

	obs_data_t *cfg = obs_source_get_settings(f->context);
	obs_data_set_string(cfg, S_STATUS, buf);
	obs_data_release(cfg);
	obs_source_update_properties(f->context);
}

static void worker_loop(MwFilter *f)
{
	blog(LOG_INFO, "[obs-mp3-writer] Worker started (%s, %d kbps)",
	     format_ext(f->format),
	     f->format == MwFormat::MP3 ? f->bitrate : 0);

	auto last_tick = std::chrono::steady_clock::now();

	while (!f->stop_requested.load(std::memory_order_relaxed)) {
		const uint32_t wp = f->ring_write.load(std::memory_order_acquire);
		const uint32_t rp = f->ring_read.load(std::memory_order_relaxed);

		if (wp == rp) {
			std::unique_lock<std::mutex> lk(f->ring_cv_mtx);
			f->ring_cv.wait_for(lk, std::chrono::milliseconds(100), [&] {
				return f->ring_write.load(std::memory_order_acquire) != wp
				    || f->stop_requested.load(std::memory_order_relaxed);
			});
			continue;
		}

		const MwChunk &chunk = f->ring[rp];

		if (!f->fmt_ctx) {
			const std::string path = build_output_path(f);
			if (!ffmpeg_open(f, path, chunk.sample_rate, chunk.channels)) {
				blog(LOG_ERROR, "[obs-mp3-writer] Failed to open output");
				f->stop_requested.store(true, std::memory_order_relaxed);
				break;
			}
			blog(LOG_INFO, "[obs-mp3-writer] Writing to: %s", path.c_str());
		}

		encode_chunk(f, chunk);
		f->ring_read.store((rp + 1) & (MW_RING_SIZE - 1),
				   std::memory_order_release);

		// Update status timer roughly once per second
		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(
			    now - last_tick).count() >= 1000) {
			last_tick = now;
			update_status(f);
		}
	}

	// Drain remaining frames
	{
		uint32_t wp = f->ring_write.load(std::memory_order_acquire);
		uint32_t rp = f->ring_read.load(std::memory_order_relaxed);
		while (wp != rp && f->fmt_ctx) {
			encode_chunk(f, f->ring[rp]);
			rp = (rp + 1) & (MW_RING_SIZE - 1);
			f->ring_read.store(rp, std::memory_order_release);
			wp = f->ring_write.load(std::memory_order_acquire);
		}
	}

	ffmpeg_close(f);
	blog(LOG_INFO, "[obs-mp3-writer] Worker stopped");
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording start / stop
// ─────────────────────────────────────────────────────────────────────────────

static void start_recording(MwFilter *f)
{
	if (f->active.load(std::memory_order_relaxed))
		return;

	f->stop_requested.store(false, std::memory_order_relaxed);
	f->ring_write.store(0, std::memory_order_relaxed);
	f->ring_read.store(0, std::memory_order_relaxed);
	f->start_time = std::chrono::steady_clock::now();

	f->worker = std::thread(worker_loop, f);
	f->active.store(true, std::memory_order_release);

	obs_data_t *settings = obs_source_get_settings(f->context);
	obs_data_set_string(settings, S_STATUS, "Recording");
	obs_data_release(settings);

	blog(LOG_INFO, "[obs-mp3-writer] Recording started");
}

static void stop_recording(MwFilter *f)
{
	if (!f->active.load(std::memory_order_relaxed))
		return;

	f->active.store(false, std::memory_order_release);
	f->stop_requested.store(true, std::memory_order_relaxed);
	f->ring_cv.notify_one();

	if (f->worker.joinable())
		f->worker.join();

	obs_data_t *settings = obs_source_get_settings(f->context);
	obs_data_set_string(settings, S_STATUS, "Idle");
	obs_data_release(settings);

	blog(LOG_INFO, "[obs-mp3-writer] Recording stopped");
}

// ─────────────────────────────────────────────────────────────────────────────
// OBS filter_audio  — MUST stay under 1 ms
// ─────────────────────────────────────────────────────────────────────────────

static struct obs_audio_data *filter_audio(void *data,
					   struct obs_audio_data *audio)
{
	MwFilter *f = static_cast<MwFilter *>(data);

	if (!f->active.load(std::memory_order_relaxed))
		return audio;

	const uint32_t wp   = f->ring_write.load(std::memory_order_relaxed);
	const uint32_t next = (wp + 1) & (MW_RING_SIZE - 1);

	if (next == f->ring_read.load(std::memory_order_acquire)) {
		static auto last_warn = std::chrono::steady_clock::now();
		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(
			    now - last_warn).count() >= 1) {
			last_warn = now;
			blog(LOG_WARNING,
			     "[obs-mp3-writer] Ring full — dropping frame");
		}
		return audio;
	}

	MwChunk &slot    = f->ring[wp];
	slot.frames      = std::min(audio->frames, (uint32_t)MW_CHUNK_FRAMES);
	audio_t *ao      = obs_get_audio();
	slot.channels    = (int)audio_output_get_channels(ao);
	slot.sample_rate = (int)audio_output_get_sample_rate(ao);

	const int ch = std::min(slot.channels, MW_MAX_CHANNELS);
	for (int c = 0; c < ch; ++c)
		if (audio->data[c])
			memcpy(slot.data[c], audio->data[c],
			       slot.frames * sizeof(float)); // use clamped value, not raw audio->frames

	f->ring_write.store(next, std::memory_order_release);
	f->ring_cv.notify_one();

	return audio;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frontend event callback (Record / Stream start + stop)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef MW_ENABLE_FRONTEND
static void frontend_event_cb(obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		registry_on_event(false, true);  break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		registry_on_event(false, false); break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		registry_on_event(true, true);   break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		registry_on_event(true, false);  break;
	default: break;
	}
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Property callbacks
// ─────────────────────────────────────────────────────────────────────────────

static bool on_format_changed(obs_properties_t *props, obs_property_t *,
			      obs_data_t *settings)
{
	bool is_mp3 = strcmp(obs_data_get_string(settings, S_FORMAT),
			     FORMAT_MP3) == 0;
	obs_property_set_visible(obs_properties_get(props, S_BITRATE), is_mp3);
	return true;
}

static void apply_recording_state(obs_properties_t *props, bool is_on)
{
	obs_property_set_enabled(obs_properties_get(props, "start"),        !is_on);
	obs_property_set_enabled(obs_properties_get(props, "stop"),          is_on);
	obs_property_set_enabled(obs_properties_get(props, S_FORMAT),       !is_on);
	obs_property_set_enabled(obs_properties_get(props, S_BITRATE),      !is_on);
	obs_property_set_enabled(obs_properties_get(props, S_FOLDER),       !is_on);
	obs_property_set_enabled(obs_properties_get(props, S_FILENAME_FMT), !is_on);
	obs_property_set_enabled(obs_properties_get(props, S_TRIGGER),      !is_on);
}

static bool on_start_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
	MwFilter *f = static_cast<MwFilter *>(data);
	start_recording(f);
	apply_recording_state(props, true);
	return true;
}

static bool on_stop_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
	MwFilter *f = static_cast<MwFilter *>(data);
	stop_recording(f);
	apply_recording_state(props, false);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// OBS source callbacks
// ─────────────────────────────────────────────────────────────────────────────

static const char *get_name(void *) { return T_NAME; }

static void *create(obs_data_t *settings, obs_source_t *source)
{
	MwFilter *f    = new MwFilter{};
	f->context     = source;
	f->folder      = obs_data_get_string(settings, S_FOLDER);
	f->filename_fmt= obs_data_get_string(settings, S_FILENAME_FMT);
	f->bitrate     = (int)obs_data_get_int(settings, S_BITRATE);

	const char *fmt = obs_data_get_string(settings, S_FORMAT);
	if      (strcmp(fmt, FORMAT_MP3)  == 0) f->format = MwFormat::MP3;
	else if (strcmp(fmt, FORMAT_AIFF) == 0) f->format = MwFormat::AIFF;
	else                                     f->format = MwFormat::WAV;

	const char *trg = obs_data_get_string(settings, S_TRIGGER);
	if      (strcmp(trg, TRIGGER_RECORDING) == 0) f->trigger = MwTrigger::Recording;
	else if (strcmp(trg, TRIGGER_STREAMING) == 0) f->trigger = MwTrigger::Streaming;
	else if (strcmp(trg, TRIGGER_BOTH)      == 0) f->trigger = MwTrigger::Both;
	else                                           f->trigger = MwTrigger::Manual;

	// Always reset status on load — recording state never persists across sessions
	obs_data_set_string(settings, S_STATUS, "Idle");

	registry_add(f);
	return f;
}

static void destroy(void *data)
{
	MwFilter *f = static_cast<MwFilter *>(data);
	registry_remove(f);
	stop_recording(f);
	delete f;
}

static void update(void *data, obs_data_t *settings)
{
	MwFilter *f = static_cast<MwFilter *>(data);
	if (f->active.load(std::memory_order_relaxed))
		return; // don't change config mid-recording

	f->folder       = obs_data_get_string(settings, S_FOLDER);
	f->filename_fmt = obs_data_get_string(settings, S_FILENAME_FMT);
	f->bitrate      = (int)obs_data_get_int(settings, S_BITRATE);

	const char *fmt = obs_data_get_string(settings, S_FORMAT);
	if      (strcmp(fmt, FORMAT_MP3)  == 0) f->format = MwFormat::MP3;
	else if (strcmp(fmt, FORMAT_AIFF) == 0) f->format = MwFormat::AIFF;
	else                                     f->format = MwFormat::WAV;

	const char *trg = obs_data_get_string(settings, S_TRIGGER);
	if      (strcmp(trg, TRIGGER_RECORDING) == 0) f->trigger = MwTrigger::Recording;
	else if (strcmp(trg, TRIGGER_STREAMING) == 0) f->trigger = MwTrigger::Streaming;
	else if (strcmp(trg, TRIGGER_BOTH)      == 0) f->trigger = MwTrigger::Both;
	else                                           f->trigger = MwTrigger::Manual;
}

static obs_properties_t *get_properties(void *data)
{
	MwFilter *f     = static_cast<MwFilter *>(data);
	bool      is_on = f->active.load(std::memory_order_relaxed);

	obs_properties_t *props = obs_properties_create();

	// ── Status indicator ──────────────────────────────────────────────────
	obs_property_t *status = obs_properties_add_text(
		props, S_STATUS, T_STATUS, OBS_TEXT_INFO);
	obs_property_text_set_info_type(status,
		is_on ? OBS_TEXT_INFO_ERROR : OBS_TEXT_INFO_NORMAL);

	// ── Format ────────────────────────────────────────────────────────────
	obs_property_t *fmt = obs_properties_add_list(
		props, S_FORMAT, T_FORMAT,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(fmt, T_("Mp3WriterFilter.FormatWAV"),  FORMAT_WAV);
	obs_property_list_add_string(fmt, T_("Mp3WriterFilter.FormatMP3"),  FORMAT_MP3);
	obs_property_list_add_string(fmt, T_("Mp3WriterFilter.FormatAIFF"), FORMAT_AIFF);
	obs_property_set_modified_callback(fmt, on_format_changed);

	// ── Bitrate (MP3 only) ────────────────────────────────────────────────
	obs_property_t *br = obs_properties_add_list(
		props, S_BITRATE, T_BITRATE,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(br, "128 kbps", 128);
	obs_property_list_add_int(br, "192 kbps", 192);
	obs_property_list_add_int(br, "256 kbps", 256);
	obs_property_list_add_int(br, "320 kbps", 320);

	// ── Output folder + filename ──────────────────────────────────────────
	obs_properties_add_path(props, S_FOLDER, T_FOLDER,
				OBS_PATH_DIRECTORY, nullptr, nullptr);
	obs_properties_add_text(props, S_FILENAME_FMT, T_FILENAME_FMT,
				OBS_TEXT_DEFAULT);

	// ── Auto-start trigger ────────────────────────────────────────────────
	obs_property_t *trg = obs_properties_add_list(
		props, S_TRIGGER, T_TRIGGER,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(trg, T_("Mp3WriterFilter.TriggerManual"),    TRIGGER_MANUAL);
	obs_property_list_add_string(trg, T_("Mp3WriterFilter.TriggerRecording"), TRIGGER_RECORDING);
	obs_property_list_add_string(trg, T_("Mp3WriterFilter.TriggerStreaming"), TRIGGER_STREAMING);
	obs_property_list_add_string(trg, T_("Mp3WriterFilter.TriggerBoth"),      TRIGGER_BOTH);

	// ── Start / Stop buttons — disabled based on current state ────────────
	obs_property_t *start_btn =
		obs_properties_add_button(props, "start", T_START, on_start_clicked);
	obs_property_t *stop_btn =
		obs_properties_add_button(props, "stop", T_STOP, on_stop_clicked);

	obs_property_set_enabled(start_btn, !is_on);
	obs_property_set_enabled(stop_btn,   is_on);

	// Disable format/bitrate/folder/trigger while recording
	obs_property_set_enabled(fmt, !is_on);
	obs_property_set_enabled(br,  !is_on);
	obs_property_set_enabled(obs_properties_get(props, S_FOLDER),       !is_on);
	obs_property_set_enabled(obs_properties_get(props, S_FILENAME_FMT), !is_on);
	obs_property_set_enabled(trg, !is_on);

	return props;
}

static void get_defaults(obs_data_t *settings)
{
	char videos[512] = {};
#ifdef _WIN32
	const char *home = getenv("USERPROFILE");
	if (home)
		snprintf(videos, sizeof(videos), "%s\\Videos", home);
	else
		strncpy(videos, "C:\\Users\\Public\\Videos", sizeof(videos) - 1);
#else
	const char *home = getenv("HOME");
	if (home)
		snprintf(videos, sizeof(videos), "%s/Movies", home);
	else
		strncpy(videos, "/tmp", sizeof(videos) - 1);
#endif

	obs_data_set_default_string(settings, S_FOLDER,       videos);
	obs_data_set_default_string(settings, S_FILENAME_FMT, "%SRC%_%DATE%_%TIME%");
	obs_data_set_default_string(settings, S_FORMAT,       FORMAT_WAV);
	obs_data_set_default_int   (settings, S_BITRATE,      192);
	obs_data_set_default_string(settings, S_TRIGGER,      TRIGGER_MANUAL);
	obs_data_set_default_string(settings, S_STATUS,       "Idle");
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void mp3_writer_filter_register()
{
	struct obs_source_info info = {};
	info.id             = "mp3_writer_filter";
	info.type           = OBS_SOURCE_TYPE_FILTER;
	info.output_flags   = OBS_SOURCE_AUDIO;
	info.get_name       = get_name;
	info.create         = create;
	info.destroy        = destroy;
	info.update         = update;
	info.get_properties = get_properties;
	info.get_defaults   = get_defaults;
	info.filter_audio   = filter_audio;
	obs_register_source(&info);

#ifdef MW_ENABLE_FRONTEND
	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
#endif
#ifdef MW_ENABLE_QT
	mw_dock_register();
#endif
}
