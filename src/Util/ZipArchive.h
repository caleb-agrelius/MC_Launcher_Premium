#pragma once

#include <miniz/miniz.h>

#include <filesystem>
#include <string>

// RAII wrappers for miniz archives. The archive is closed on every way out of the scope that
// owns it (early returns and exceptions included), so callers never pair Open with End by hand.

// A zip file open for reading.
class ZipReader {
public:
	ZipReader() = default;
	~ZipReader() { Close(); }
	ZipReader(const ZipReader&) = delete;
	ZipReader& operator=(const ZipReader&) = delete;

	// Opens the zip at path. False when it can't be read as a zip.
	bool Open(const std::filesystem::path& file)
	{
		Close();
		m_open = mz_zip_reader_init_file(&m_zip, file.string().c_str(), 0) != MZ_FALSE;
		return m_open;
	}

	// Opens a zip held in memory; data must stay valid while the reader is open.
	bool OpenMemory(const void* data, std::size_t size)
	{
		Close();
		m_open = mz_zip_reader_init_mem(&m_zip, data, size, 0) != MZ_FALSE;
		return m_open;
	}

	void Close()
	{
		if (m_open)
			mz_zip_reader_end(&m_zip);
		m_open = false;
		m_zip = mz_zip_archive{};
	}

	bool IsOpen() const { return m_open; }

	// The miniz handle, for the calls this wrapper doesn't cover. Valid while the reader is open.
	mz_zip_archive* Raw() { return &m_zip; }

	// The whole entry called name. False when there is no such entry or it can't be unpacked.
	bool ReadEntry(const char* name, std::string& out)
	{
		std::size_t size = 0;
		void* data = mz_zip_reader_extract_file_to_heap(&m_zip, name, &size, 0);
		if (!data)
			return false;
		out.assign(static_cast<const char*>(data), size);
		mz_free(data);
		return true;
	}

private:
	mz_zip_archive m_zip{};
	bool m_open = false;
};

// A zip file being written. Finalize() commits it; a writer that goes out of scope without
// one leaves a partial file for the caller to delete.
class ZipWriter {
public:
	ZipWriter() = default;
	~ZipWriter() { Close(); }
	ZipWriter(const ZipWriter&) = delete;
	ZipWriter& operator=(const ZipWriter&) = delete;

	bool Open(const std::filesystem::path& file)
	{
		Close();
		m_open = mz_zip_writer_init_file(&m_zip, file.string().c_str(), 0) != MZ_FALSE;
		return m_open;
	}

	// Copies entry index of reader into this archive without recompressing it.
	bool AddFromReader(ZipReader& reader, mz_uint index)
	{
		return m_open && mz_zip_writer_add_from_zip_reader(&m_zip, reader.Raw(), index);
	}

	// Writes the central directory. False if the archive can't be completed.
	bool Finalize() { return m_open && mz_zip_writer_finalize_archive(&m_zip); }

	void Close()
	{
		if (m_open)
			mz_zip_writer_end(&m_zip);
		m_open = false;
		m_zip = mz_zip_archive{};
	}

private:
	mz_zip_archive m_zip{};
	bool m_open = false;
};
