#pragma once


#include <shared_mutex>
#include <sstream>

class Logger
{
public:
	// Numeric values matter: a higher int means more verbose.
	//
	// Values are k-prefixed to avoid collisions with common build-system macros
	// such as -DDEBUG=1, -DNDEBUG, or the <syslog.h> family (LOG_ERR, LOG_INFO…).
	enum class Level : int { ERROR = 0, WARN = 1, INFO = 2, DBG = 3 };

	// Defaults to stderr; auto-detects color support via isatty().
	Logger();
	// Borrows an existing stream. Lifetime of `stream` must exceed the Logger's.
	explicit Logger(std::ostream &stream, bool use_color = false);
	// Opens a file for appending; falls back to stderr on failure.
	explicit Logger(const std::string &file_path);

	~Logger();

	// Logger is not copyable or movable (owns a mutex and possibly a stream).
	Logger(const Logger &)            = delete;
	Logger &operator=(const Logger &) = delete;
	Logger(Logger &&)                 = delete;
	Logger &operator=(Logger &&)      = delete;

	void  set_level(Level level) noexcept;
	Level get_level()            const noexcept;
	bool  use_color()            const noexcept;
	void  set_show_timestamp(bool show) noexcept;

	template<typename... Args>
	void write_log(
	    Level       level,
	    const char *file,
	    int         line,
	    const char *func,
	    bool        new_line,
	    Args &&...  args)
	{
		std::unique_lock lock(mutex_);

		if (static_cast<int>(level) > static_cast<int>(level_))
			return;
		if (!stream_)
			return;

		// Buffer the entire line in a local ostringstream first, then write it
		// to the real stream in one shot. This prevents interleaved output from
		// concurrent threads and lets us use natural operator<< throughout.
		std::ostringstream line_buf;

#ifdef LOG_SHOW_TIME_STAMP
		if (show_timestamp_)
			write_timestamp(line_buf);
#endif
		write_prefix(line_buf, level);
		write_source_location(line_buf, file, line, func);
		(line_buf << ... << std::forward<Args>(args));

		if (new_line)
			line_buf << '\n';

		*stream_ << line_buf.str();
		stream_->flush();
	}

private:
	mutable std::shared_mutex     mutex_;
	Level                         level_          = Level::INFO;
	std::ostream                 *stream_         = nullptr;
	std::unique_ptr<std::ostream> owned_stream_;
	bool                          use_color_      = false;
	bool                          show_timestamp_ = true;

	void init_stream(const std::string &file_path);

	// All write_* helpers are called while mutex_ is already held.
	void write_prefix(std::ostream &os, Level level)                                           const;
	void write_source_location(std::ostream &os, const char *file, int line, const char *func) const;
	void write_timestamp(std::ostream &os)                                                     const;
};

// Global default logger (defined in log.cpp).
extern Logger g_logger;

// ====================== Macros ======================
//
// Define LOG_SHOW_SOURCE_LOCATION before including this header to embed
// __FILE__ / __LINE__ / __func__ in every log line.
//
// Define LOG_SHOW_TIME_STAMP before including this header to prepend a
// timestamp to every log line.

#ifdef LOG_SHOW_SOURCE_LOCATION

	#define LOG_CUSTOM(LEVEL, NEW_LINE, ...)  \
		g_logger.write_log(LEVEL, __FILE__, __LINE__, __func__, NEW_LINE, __VA_ARGS__)

	#define LOG_PERROR(...)                                   \
		do {                                                  \
			g_logger.write_log(Logger::Level::ERROR,          \
			                   __FILE__, __LINE__, __func__,  \
			                   true,                          \
			                   __VA_ARGS__);                  \
			std::perror(" ");                                 \
		} while (0);


	#define LOG_ERROR(...)  \
		g_logger.write_log(Logger::Level::ERROR, __FILE__, __LINE__, __func__, true, __VA_ARGS__)
	#define LOG_WARN(...)   \
		g_logger.write_log(Logger::Level::WARN,  __FILE__, __LINE__, __func__, true, __VA_ARGS__)
	#define LOG_INFO(...)   \
		g_logger.write_log(Logger::Level::INFO,  __FILE__, __LINE__, __func__, true, __VA_ARGS__)
	#define LOG_DEBUG(...)  \
		g_logger.write_log(Logger::Level::DBG,   __FILE__, __LINE__, __func__, true, __VA_ARGS__)

#else

	#define LOG_CUSTOM(LEVEL, NEW_LINE, ...)  \
		g_logger.write_log(LEVEL, nullptr, 0, nullptr, NEW_LINE, __VA_ARGS__)

	#define LOG_PERROR(...)                                   \
		do {                                                  \
			g_logger.write_log(Logger::Level::ERROR,          \
			                   nullptr, 0, nullptr,           \
			                   false,                         \
			                   __VA_ARGS__);                  \
			std::perror(" ");                                 \
		} while (0);

	#define LOG_ERROR(...)  \
		g_logger.write_log(Logger::Level::ERROR, nullptr, 0, nullptr, true, __VA_ARGS__)
	#define LOG_WARN(...)   \
		g_logger.write_log(Logger::Level::WARN,  nullptr, 0, nullptr, true, __VA_ARGS__)
	#define LOG_INFO(...)   \
		g_logger.write_log(Logger::Level::INFO,  nullptr, 0, nullptr, true, __VA_ARGS__)
	#define LOG_DEBUG(...)  \
		g_logger.write_log(Logger::Level::DBG,   nullptr, 0, nullptr, true, __VA_ARGS__)

#endif  // LOG_SHOW_SOURCE_LOCATION
