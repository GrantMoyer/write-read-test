#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <unistd.h>

constexpr auto USAGE = 1 + R"(
Writes then reads a psuedo-random sequence to disk, checking the read values are correct.

Usage: write-read-test [SEED] SIZE PATH

Arguments:
	SEED - The seed for the psuedo-random number generator.
	SIZE - The size fo the file to write the read.
	PATH - The path to the file to write then read.
)";

std::optional<uint64_t> parse_size(const char* s) {
	uint64_t size = 0;
	const std::string_view sv = std::string_view(s);
	const auto [first_non_number, ec] = std::from_chars(sv.begin(), sv.end(), size);
	if (ec != std::errc{}) return std::nullopt;
	switch (*first_non_number) {
		case 'T': case 't':
			size *= 1024;
		case 'G': case 'g':
			size *= 1024;
		case 'M': case 'm':
			size *= 1024;
		case 'K': case 'k':
			size *= 1024;
		case '\0':
			break;
		default:
			return std::nullopt;
	}
	return {size};
}

typedef std::independent_bits_engine<
	std::mt19937_64,
	std::numeric_limits<std::mt19937_64::result_type>::digits,
	std::mt19937_64::result_type
> Prng;

int main(int argc, char* argv[]) {
	Prng::result_type seed = 0xb473fa49a165403e; // random.org generated number
	uint64_t size = 0;
	std::filesystem::path path{};

	if (argc == 3) {
		const auto parsed = parse_size(argv[1]);
		if (!parsed) {
			std::cerr << "Failed to parse size:" << argv[1] << '\n';
			return 1;
		}
		size = *parsed;

		path = argv[2];
	} else if (argc == 4) {
		const std::string_view arg1 = std::string_view(argv[1]);
		const auto [first_non_number, ec] = std::from_chars(arg1.begin(), arg1.end(), seed);
		if (ec != std::errc{}) {
			std::cerr << "Failed to parse seed:" << arg1 << '\n';
		}

		const auto parsed = parse_size(argv[2]);
		if (!parsed) {
			std::cerr << "Failed to parse size:" << argv[2] << '\n';
			return 1;
		}
		size = *parsed;

		path = argv[3];
	} else {
		std::cerr << USAGE;
		return 1;
	}

	Prng prng{seed};

	std::FILE* f_out = std::fopen(path.c_str(), "wb");
	if (f_out == nullptr) {
		std::cerr << "Failed to open file for writing\n";
		return 1;
	}

	constexpr uint64_t status_interval = 16 * 1024 * 1024;
	std::array<decltype(prng)::result_type, 1024> buffer{};
	uint64_t bytes_left = size;
	while (bytes_left > 0) {
		for (size_t i = 0; i < buffer.size(); ++i) {
			buffer[i] = prng();
		}
		const auto bytes_to_write = std::min(bytes_left, std::span{buffer}.size_bytes());
		if (std::fwrite(buffer.data(), 1, bytes_to_write, f_out) < bytes_to_write) {
			std::cerr << "Failed to write to file\n";
			return 1;
		}
		bytes_left -= bytes_to_write;


		const auto bytes_written = size - bytes_left;
		if (bytes_written % status_interval == 0 || bytes_left == 0) {
			const auto wrote_percent = double(bytes_written) / size * 100.0;
			std::cout << "Writing (" << std::setprecision(1) << std::fixed << wrote_percent << ")\r" << std::flush;
		}
	}
	if (std::fflush(f_out)) {
		std::cerr << "Failed to flush file writes\n";
		return 1;
	}
	const auto fd_out = fileno(f_out);
	if (fd_out == -1) {
		std::cerr << "Failed to get output file descriptor\n";
		return 1;
	}
	if (fsync(fd_out)) {
		std::cerr << "Failed to sync file writes to disk\n";
		return 1;
	}
	if (std::fclose(f_out)) {
		std::cerr << "Failed to close written file\n";
		return 1;
	}

	std::cout << "Wrote " << size << " bytes\n";

	prng.seed(seed);

	std::FILE* f_in = std::fopen(path.c_str(), "rb");
	if (f_in == nullptr) {
		std::cerr << "Failed to open file for reading\n";
		return 1;
	}

	decltype(buffer) read_buffer{};
	uint64_t num_errors = 0;
	bytes_left = size;
	while (bytes_left > 0) {
		for (size_t i = 0; i < buffer.size(); ++i) {
			buffer[i] = prng();
		}
		const auto bytes_to_read = std::min(bytes_left, std::span{read_buffer}.size_bytes());
		if (std::fread(read_buffer.data(), 1, bytes_to_read, f_in) < bytes_to_read) {
			std::cerr << "Failed to read to file\n";
			return 1;
		}
		bytes_left -= bytes_to_read;

		const auto buffer_bytes = std::as_bytes(std::span{buffer});
		const auto read_bytes = std::as_bytes(std::span{read_buffer});
		for(size_t i = 0; i < bytes_to_read; ++i) {
			if (read_bytes[i] != buffer_bytes[i]) ++num_errors;
		}

		const auto bytes_read = size - bytes_left;
		if (bytes_read % status_interval == 0 || bytes_left == 0) {
			const auto read_percent = double(bytes_read) / size * 100.0;
			std::cout << "Reading (" << std::setprecision(1) << std::fixed << read_percent << ")\r" << std::flush;
		}
	}

	const auto error_percent = double(num_errors) / size * 100.0;

	std::cout << "Read " << size << " bytes\n";
	std::cout << "Found " << num_errors << " errors (" << std::setprecision(1) << std::fixed <<  error_percent << ")\n";
}
