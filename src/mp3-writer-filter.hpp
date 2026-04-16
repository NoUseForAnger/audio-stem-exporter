#pragma once

#include <obs-module.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

// ─── Ring-buffer constants ────────────────────────────────────────────────────
static constexpr uint32_t MW_RING_SIZE    = 64u;
static constexpr int      MW_CHUNK_FRAMES = 1024;
static constexpr int      MW_MAX_CHANNELS = 8;

// ─── Output format ────────────────────────────────────────────────────────────
enum class MwFormat { WAV, MP3, AIFF };

// ─── Auto-start trigger ───────────────────────────────────────────────────────
enum class MwTrigger { Manual, Recording, Streaming, Both };

// ─── Single audio chunk ───────────────────────────────────────────────────────
struct MwChunk {
	uint32_t frames;
	int      channels;
	int      sample_rate;
	float    data[MW_MAX_CHANNELS][MW_CHUNK_FRAMES];
};

// ─── FFmpeg forward declarations ──────────────────────────────────────────────
struct AVFormatContext;
struct AVStream;
struct AVCodecContext;
struct AVAudioFifo;
struct SwrContext;
struct AVFrame;
struct AVPacket;

// ─── Per-instance filter state ────────────────────────────────────────────────
struct MwFilter {
	obs_source_t *context;

	// Config
	std::string  folder;
	std::string  filename_fmt;
	MwFormat     format{MwFormat::WAV};
	int          bitrate{192};           // kbps — MP3 only
	MwTrigger    trigger{MwTrigger::Manual};

	// Lifecycle
	std::atomic<bool> active{false};
	std::atomic<bool> stop_requested{false};
	std::atomic<int>  trigger_count{0};  // ref-count for Both mode stop logic

	// Start timestamp — written before active=true, read after active=true
	std::chrono::steady_clock::time_point start_time;

	// Lock-free SPSC ring
	MwChunk ring[MW_RING_SIZE];
	alignas(64) std::atomic<uint32_t> ring_write{0};
	alignas(64) std::atomic<uint32_t> ring_read{0};

	// Worker wake-up (cross-platform replacement for WaitOnAddress)
	std::condition_variable ring_cv;
	std::mutex              ring_cv_mtx;

	// Worker
	std::thread worker;

	// FFmpeg (worker thread only)
	AVFormatContext *fmt_ctx{nullptr};
	AVStream        *stream{nullptr};
	AVCodecContext  *codec_ctx{nullptr};
	AVAudioFifo     *fifo{nullptr};     // for codecs with fixed frame_size (e.g. MP3)
	SwrContext      *swr_ctx{nullptr};
	AVFrame         *frame{nullptr};
	AVPacket        *packet{nullptr};
	int64_t          pts{0};
};

void mp3_writer_filter_register();

// ─── Dock / registry helpers ──────────────────────────────────────────────────
#include <vector>
void mw_registry_snapshot(std::vector<MwFilter*> &out);
void mw_start_all();
void mw_stop_all();
void mw_start_recording_one(MwFilter *f);
void mw_stop_recording_one(MwFilter *f);
