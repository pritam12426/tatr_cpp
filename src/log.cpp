#include "log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <system_error>
#include <unistd.h>

namespace {

constexpr std::string_view COLOR_RESET       = "\x1b[0m";
constexpr std::string_view COLOR_BOLD_RED    = "\x1b[1;31m";
constexpr std::string_view COLOR_BOLD_GREEN  = "\x1b[1;32m";
constexpr std::string_view COLOR_BOLD_YELLOW = "\x1b[1;33m";
constexpr std::string_view COLOR_BOLD_CYAN   = "\x1b[1;36m";
constexpr std::string_view COLOR_DIM         = "\x1b[2m";

} // anonymous namespace

// Global default logger
Logger g_logger;

// ====================== Constructors / Destructor ======================

Logger::Logger()
    : stream_(&std::cerr)
{
	use_color_ = (isatty(fileno(stderr)) == 1);
}

Logger::Logger(std::ostream &stream, bool use_color)
    : stream_(&stream)
    , use_color_(use_color)
{
}

Logger::Logger(const std::string &file_path)
{
	init_stream(file_path);
}

Logger::~Logger() = default;

// ====================== Private Helpers ======================

void Logger::init_stream(const std::string &file_path)
{
	auto fs = std::make_unique<std::ofstream>(file_path, std::ios::app);
	if (fs->is_open()) {
		owned_stream_ = std::move(fs);
		stream_       = owned_stream_.get();
		use_color_    = false;
	} else {
		std::cerr << "[LOG] warning: could not open log file '" << file_path << "': "
		          << std::error_code{errno, std::generic_category()}.message()
		          << " (falling back to stderr)\n";
		stream_    = &std::cerr;
		use_color_ = (isatty(fileno(stderr)) == 1);
	}
}

void Logger::write_prefix(std::ostream &os, Level level) const
{
	if (use_color_) {
		switch (level) {
		case Level::ERROR: os << "🚨 [" << COLOR_BOLD_RED    << "ERROR" << COLOR_RESET << ']'; break;
		case Level::WARN:  os << "⚠️  [" << COLOR_BOLD_YELLOW << "WARN " << COLOR_RESET << ']'; break;
		case Level::INFO:  os << "ℹ️  [" << COLOR_BOLD_GREEN  << "INFO " << COLOR_RESET << ']'; break;
		case Level::DBG:   os << "🛠️  [" << COLOR_BOLD_CYAN   << "DEBUG" << COLOR_RESET << ']'; break;
		default:            os << "[?????]";                                                      break;
		}
	} else {
		switch (level) {
		case Level::ERROR: os << "[ERROR]"; break;
		case Level::WARN:  os << "[WARN ]"; break;
		case Level::INFO:  os << "[INFO ]"; break;
		case Level::DBG:   os << "[DEBUG]"; break;
		default:           os << "[?????]"; break;
		}
	}
	os << ' ';
}

void Logger::write_source_location(std::ostream &os, const char *file, int line, const char *func) const
{
	if (!file)
		return;

	if (use_color_)
		os << COLOR_DIM;

	os << '[' << file << ':' << line << ':' << (func ? func : "?") << ']';

	if (use_color_)
		os << COLOR_RESET;

	os << ' ';
}

// ====================== Public Interface ======================

void Logger::set_level(Level level) noexcept
{
	std::unique_lock lock(mutex_);
	level_ = level;
}

Logger::Level Logger::get_level() const noexcept
{
	std::shared_lock lock(mutex_);
	return level_;
}

bool Logger::use_color() const noexcept
{
	std::shared_lock lock(mutex_);
	return use_color_;
}

void Logger::set_show_timestamp(bool show) noexcept
{
	std::unique_lock lock(mutex_);
	show_timestamp_ = show;
}

// ====================== Timestamp ======================

void Logger::write_timestamp(std::ostream &os) const
{
	const auto now = std::chrono::system_clock::now();
	const auto tt  = std::chrono::system_clock::to_time_t(now);

	std::tm tm{};
	localtime_r(&tt, &tm);

	const auto us =
	    std::chrono::duration_cast<std::chrono::microseconds>(
	        now.time_since_epoch() % std::chrono::seconds{1})
	    .count();

	// %H = 24-hour clock (00–23), unambiguous without AM/PM.
	char buf[32];
	const std::size_t n = std::strftime(buf, sizeof(buf), "%d-%b-%Y %H:%M:%S", &tm);
	if (n == 0) {
		os << "[--date unavailable--] ";
		return;
	}
	buf[n] = '\0';

	if (use_color_)
		os << COLOR_DIM;

	os << '[' << buf << '.' << std::setfill('0') << std::setw(6) << us << "] ";

	if (use_color_)
		os << COLOR_RESET;
}
